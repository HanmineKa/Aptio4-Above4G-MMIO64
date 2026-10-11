#include "../action_api.h"

#include <stdio.h>
#include <string.h>

#define TEST_MODULE_SIZE 64
#define TEST_POLICY_SIZE 64
#define TEST_LOG_SIZE 16
#define EXPECTED_GEN4_BASE UINT64_C(0x200000000)
#define EXPECTED_GEN4_WINDOW UINT64_C(0x100000000)

typedef struct {
    action_status_t locate_status;
    action_status_t map_status;
    action_status_t patch_status;
    action_status_t add_status;
    action_status_t set_status;
    action_status_t remove_status;
    action_status_t hook_status;
    action_status_t original_status;
    bool map_safe;
    uint8_t policy_data[TEST_POLICY_SIZE];
    action_policy_view_t policy_view;
    action_guid_t requested_guid;
    unsigned int locate_calls;
    unsigned int map_calls;
    unsigned int patch_calls;
    unsigned int add_calls;
    unsigned int set_calls;
    unsigned int remove_calls;
    unsigned int hook_calls;
    uint32_t memory_type;
    uint64_t memory_base;
    uint64_t memory_length;
    uint64_t add_attributes;
    uint64_t set_attributes;
    char call_log[TEST_LOG_SIZE];
    size_t call_count;
    gen4_get_alloc_attributes_fn installed_hook;
    void *installed_hook_context;
    uint64_t original_attributes;
} mock_services_t;

static int tests_run;
static int tests_failed;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

#define RUN_TEST(test_function) \
    do { \
        ++tests_run; \
        if (!(test_function())) { \
            ++tests_failed; \
        } \
    } while (0)

static void mock_record(mock_services_t *mock, char call)
{
    if (mock->call_count < TEST_LOG_SIZE) {
        mock->call_log[mock->call_count++] = call;
    }
}

static void mock_initialize(mock_services_t *mock)
{
    memset(mock, 0, sizeof(*mock));
    mock->locate_status = ACTION_STATUS_SUCCESS;
    mock->map_status = ACTION_STATUS_SUCCESS;
    mock->patch_status = ACTION_STATUS_SUCCESS;
    mock->add_status = ACTION_STATUS_SUCCESS;
    mock->set_status = ACTION_STATUS_SUCCESS;
    mock->remove_status = ACTION_STATUS_SUCCESS;
    mock->hook_status = ACTION_STATUS_SUCCESS;
    mock->original_status = ACTION_STATUS_SUCCESS;
    mock->map_safe = true;
    mock->policy_view.declared_size = 10;
    mock->policy_view.policy_data = mock->policy_data;
    mock->policy_view.policy_data_size = sizeof(mock->policy_data);
}

static action_status_t mock_locate_protocol(
    void *context,
    const action_guid_t *guid,
    action_policy_view_t *policy_view
)
{
    mock_services_t *mock = (mock_services_t *)context;

    ++mock->locate_calls;
    mock_record(mock, 'L');
    mock->requested_guid = *guid;
    if (mock->locate_status == ACTION_STATUS_SUCCESS) {
        *policy_view = mock->policy_view;
    }
    return mock->locate_status;
}

static action_status_t mock_get_memory_space_map(void *context, bool *safe_to_enable)
{
    mock_services_t *mock = (mock_services_t *)context;

    ++mock->map_calls;
    mock_record(mock, 'M');
    *safe_to_enable = mock->map_safe;
    return mock->map_status;
}

static action_status_t mock_patch_notify_phase(
    void *context,
    uint8_t *module_buffer,
    size_t module_size,
    size_t patch_offset
)
{
    mock_services_t *mock = (mock_services_t *)context;

    (void)module_buffer;
    (void)module_size;
    (void)patch_offset;
    ++mock->patch_calls;
    mock_record(mock, 'P');
    return mock->patch_status;
}

static action_status_t mock_add_memory_space(
    void *context,
    uint32_t memory_type,
    uint64_t base,
    uint64_t length,
    uint64_t attributes
)
{
    mock_services_t *mock = (mock_services_t *)context;

    ++mock->add_calls;
    mock_record(mock, 'A');
    mock->memory_type = memory_type;
    mock->memory_base = base;
    mock->memory_length = length;
    mock->add_attributes = attributes;
    return mock->add_status;
}

static action_status_t mock_set_memory_space_attributes(
    void *context,
    uint64_t base,
    uint64_t length,
    uint64_t attributes
)
{
    mock_services_t *mock = (mock_services_t *)context;

    ++mock->set_calls;
    mock_record(mock, 'S');
    mock->memory_base = base;
    mock->memory_length = length;
    mock->set_attributes = attributes;
    return mock->set_status;
}

static action_status_t mock_remove_memory_space(
    void *context,
    uint32_t memory_type,
    uint64_t base,
    uint64_t length
)
{
    mock_services_t *mock = (mock_services_t *)context;

    ++mock->remove_calls;
    mock_record(mock, 'R');
    mock->memory_type = memory_type;
    mock->memory_base = base;
    mock->memory_length = length;
    return mock->remove_status;
}

static action_status_t mock_original_get_attributes(
    void *context,
    uint64_t *attributes
)
{
    mock_services_t *mock = (mock_services_t *)context;

    if (mock->original_status == ACTION_STATUS_SUCCESS) {
        *attributes = mock->original_attributes;
    }
    return mock->original_status;
}

static action_status_t mock_install_hook(
    void *context,
    gen4_get_alloc_attributes_fn hook,
    gen4_alloc_hook_context_t *hook_context
)
{
    mock_services_t *mock = (mock_services_t *)context;

    ++mock->hook_calls;
    mock_record(mock, 'H');
    if (mock->hook_status == ACTION_STATUS_SUCCESS) {
        hook_context->original = mock_original_get_attributes;
        hook_context->original_context = mock;
        mock->installed_hook = hook;
        mock->installed_hook_context = hook_context;
    }
    return mock->hook_status;
}

static gen23_action_services_t gen23_services(mock_services_t *mock)
{
    gen23_action_services_t services;

    services.context = mock;
    services.locate_protocol = mock_locate_protocol;
    services.get_memory_space_map = mock_get_memory_space_map;
    return services;
}

static gen5_action_services_t gen5_services(mock_services_t *mock)
{
    gen5_action_services_t services;

    services.context = mock;
    services.locate_protocol = mock_locate_protocol;
    return services;
}

static gen4_action_services_t gen4_services(mock_services_t *mock)
{
    gen4_action_services_t services;

    services.context = mock;
    services.patch_notify_phase = mock_patch_notify_phase;
    services.add_memory_space = mock_add_memory_space;
    services.set_memory_space_attributes = mock_set_memory_space_attributes;
    services.remove_memory_space = mock_remove_memory_space;
    services.install_get_alloc_attributes_hook = mock_install_hook;
    return services;
}

static int test_gen23_success_and_guid(void)
{
    mock_services_t mock;
    gen23_action_services_t services;

    mock_initialize(&mock);
    services = gen23_services(&mock);

    CHECK(gen23_apply(&services) == ACTION_STATUS_SUCCESS);
    CHECK(mock.locate_calls == 1);
    CHECK(mock.map_calls == 1);
    CHECK(mock.policy_data[0x11] == 1);
    CHECK(memcmp(&mock.requested_guid, &ACTION_GUID_GEN23_POLICY,
        sizeof(ACTION_GUID_GEN23_POLICY)) == 0);
    CHECK(mock.call_count == 2 && memcmp(mock.call_log, "LM", 2) == 0);
    return 1;
}

static int test_gen23_locate_failure_stops(void)
{
    mock_services_t mock;
    gen23_action_services_t services;

    mock_initialize(&mock);
    mock.locate_status = ACTION_STATUS_NOT_FOUND;
    services = gen23_services(&mock);

    CHECK(gen23_apply(&services) == ACTION_STATUS_NOT_FOUND);
    CHECK(mock.map_calls == 0);
    CHECK(mock.policy_data[0x11] == 0);
    return 1;
}

static int test_gen23_map_failure_stops(void)
{
    mock_services_t mock;
    gen23_action_services_t services;

    mock_initialize(&mock);
    mock.map_status = ACTION_STATUS_DEVICE_ERROR;
    services = gen23_services(&mock);

    CHECK(gen23_apply(&services) == ACTION_STATUS_DEVICE_ERROR);
    CHECK(mock.map_calls == 1);
    CHECK(mock.policy_data[0x11] == 0);
    CHECK(mock.call_count == 2 && memcmp(mock.call_log, "LM", 2) == 0);
    return 1;
}

static int test_gen23_unsafe_map_stops(void)
{
    mock_services_t mock;
    gen23_action_services_t services;

    mock_initialize(&mock);
    mock.map_safe = false;
    services = gen23_services(&mock);

    CHECK(gen23_apply(&services) == ACTION_STATUS_UNSAFE);
    CHECK(mock.policy_data[0x11] == 0);
    return 1;
}

static int test_gen23_invalid_policy_stops(void)
{
    mock_services_t mock;
    gen23_action_services_t services;

    mock_initialize(&mock);
    mock.policy_view.declared_size = 9;
    services = gen23_services(&mock);

    CHECK(gen23_apply(&services) == ACTION_STATUS_INVALID_PARAMETER);
    CHECK(mock.map_calls == 0);
    CHECK(mock.policy_data[0x11] == 0);
    return 1;
}

static int test_gen23_short_payload_stops(void)
{
    mock_services_t mock;
    gen23_action_services_t services;

    mock_initialize(&mock);
    mock.policy_view.policy_data_size = 0x11;
    services = gen23_services(&mock);

    CHECK(gen23_apply(&services) == ACTION_STATUS_INVALID_PARAMETER);
    CHECK(mock.map_calls == 0);
    CHECK(mock.policy_data[0x10] == 0);
    return 1;
}

static int test_gen5_success_and_guid(void)
{
    mock_services_t mock;
    gen5_action_services_t services;

    mock_initialize(&mock);
    mock.policy_view.declared_size = 9;
    services = gen5_services(&mock);

    CHECK(gen5_apply(&services) == ACTION_STATUS_SUCCESS);
    CHECK(mock.locate_calls == 1);
    CHECK(mock.policy_data[0x20] == 1);
    CHECK(memcmp(&mock.requested_guid, &ACTION_GUID_GEN5_POLICY,
        sizeof(ACTION_GUID_GEN5_POLICY)) == 0);
    return 1;
}

static int test_gen5_locate_failure_stops(void)
{
    mock_services_t mock;
    gen5_action_services_t services;

    mock_initialize(&mock);
    mock.locate_status = ACTION_STATUS_NOT_FOUND;
    services = gen5_services(&mock);

    CHECK(gen5_apply(&services) == ACTION_STATUS_NOT_FOUND);
    CHECK(mock.policy_data[0x20] == 0);
    return 1;
}

static int test_gen5_invalid_policy_stops(void)
{
    mock_services_t mock;
    gen5_action_services_t services;

    mock_initialize(&mock);
    mock.policy_view.declared_size = 8;
    services = gen5_services(&mock);

    CHECK(gen5_apply(&services) == ACTION_STATUS_INVALID_PARAMETER);
    CHECK(mock.policy_data[0x20] == 0);
    return 1;
}

static int test_gen5_short_payload_stops(void)
{
    mock_services_t mock;
    gen5_action_services_t services;

    mock_initialize(&mock);
    mock.policy_view.declared_size = 9;
    mock.policy_view.policy_data_size = 0x20;
    services = gen5_services(&mock);

    CHECK(gen5_apply(&services) == ACTION_STATUS_INVALID_PARAMETER);
    CHECK(mock.policy_data[0x1F] == 0);
    return 1;
}

static int test_gen4_placeholder_refuses_without_writing(void)
{
    uint8_t module_buffer[TEST_MODULE_SIZE];
    uint8_t original_buffer[TEST_MODULE_SIZE];

    memset(module_buffer, 0xA5, sizeof(module_buffer));
    memcpy(original_buffer, module_buffer, sizeof(module_buffer));

    CHECK(gen4_notify_phase_patch_placeholder(
        NULL,
        module_buffer,
        sizeof(module_buffer),
        12
    ) == ACTION_STATUS_UNSUPPORTED);
    CHECK(memcmp(module_buffer, original_buffer, sizeof(module_buffer)) == 0);
    CHECK(gen4_notify_phase_patch_placeholder(
        NULL,
        module_buffer,
        sizeof(module_buffer),
        sizeof(module_buffer)
    ) == ACTION_STATUS_INVALID_PARAMETER);
    return 1;
}

static int test_gen4_default_placeholder_stops_before_side_effects(void)
{
    mock_services_t mock;
    gen4_action_services_t services;
    gen4_alloc_hook_context_t hook_context;
    uint8_t module_buffer[TEST_MODULE_SIZE] = { 0 };

    mock_initialize(&mock);
    services = gen4_services(&mock);
    services.patch_notify_phase = gen4_notify_phase_patch_placeholder;
    memset(&hook_context, 0, sizeof(hook_context));

    CHECK(gen4_apply(&services, EXPECTED_GEN4_BASE, module_buffer,
        sizeof(module_buffer), 12, &hook_context) == ACTION_STATUS_UNSUPPORTED);
    CHECK(mock.patch_calls == 0);
    CHECK(mock.add_calls == 0);
    CHECK(mock.set_calls == 0);
    CHECK(mock.hook_calls == 0);
    return 1;
}

static int test_gen4_success_path_with_mock_patch_gate(void)
{
    mock_services_t mock;
    gen4_action_services_t services;
    gen4_alloc_hook_context_t hook_context;
    uint8_t module_buffer[TEST_MODULE_SIZE] = { 0 };
    uint64_t attributes = 0;

    mock_initialize(&mock);
    services = gen4_services(&mock);
    memset(&hook_context, 0, sizeof(hook_context));

    CHECK(gen4_apply(&services, EXPECTED_GEN4_BASE, module_buffer,
        sizeof(module_buffer), 12, &hook_context) == ACTION_STATUS_SUCCESS);
    CHECK(mock.call_count == 4 && memcmp(mock.call_log, "PASH", 4) == 0);
    CHECK(mock.memory_type == 3U);
    CHECK(mock.memory_base == EXPECTED_GEN4_BASE);
    CHECK(mock.memory_length == EXPECTED_GEN4_WINDOW);
    CHECK(mock.add_attributes == 0);
    CHECK(mock.set_attributes == 1);
    CHECK(mock.installed_hook != NULL);
    mock.original_attributes = UINT64_C(0x80);
    CHECK(mock.installed_hook(mock.installed_hook_context, &attributes) ==
        ACTION_STATUS_SUCCESS);
    CHECK(attributes == UINT64_C(0x82));
    return 1;
}

static int test_gen4_uses_four_gib_base_when_touud_is_lower(void)
{
    mock_services_t mock;
    gen4_action_services_t services;
    gen4_alloc_hook_context_t hook_context;
    uint8_t module_buffer[TEST_MODULE_SIZE] = { 0 };

    mock_initialize(&mock);
    services = gen4_services(&mock);
    memset(&hook_context, 0, sizeof(hook_context));

    CHECK(gen4_apply(&services, UINT64_C(0x80000000), module_buffer,
        sizeof(module_buffer), 4, &hook_context) == ACTION_STATUS_SUCCESS);
    CHECK(mock.memory_base == UINT64_C(0x100000000));
    return 1;
}

static int test_gen4_patch_failure_stops(void)
{
    mock_services_t mock;
    gen4_action_services_t services;
    gen4_alloc_hook_context_t hook_context;
    uint8_t module_buffer[TEST_MODULE_SIZE] = { 0 };

    mock_initialize(&mock);
    mock.patch_status = ACTION_STATUS_UNSUPPORTED;
    services = gen4_services(&mock);
    memset(&hook_context, 0, sizeof(hook_context));

    CHECK(gen4_apply(&services, EXPECTED_GEN4_BASE, module_buffer,
        sizeof(module_buffer), 12, &hook_context) == ACTION_STATUS_UNSUPPORTED);
    CHECK(mock.add_calls == 0 && mock.set_calls == 0 && mock.hook_calls == 0);
    return 1;
}

static int test_gen4_add_failure_stops(void)
{
    mock_services_t mock;
    gen4_action_services_t services;
    gen4_alloc_hook_context_t hook_context;
    uint8_t module_buffer[TEST_MODULE_SIZE] = { 0 };

    mock_initialize(&mock);
    mock.add_status = ACTION_STATUS_OUT_OF_RESOURCES;
    services = gen4_services(&mock);
    memset(&hook_context, 0, sizeof(hook_context));

    CHECK(gen4_apply(&services, EXPECTED_GEN4_BASE, module_buffer,
        sizeof(module_buffer), 12, &hook_context) == ACTION_STATUS_OUT_OF_RESOURCES);
    CHECK(mock.set_calls == 0 && mock.hook_calls == 0 && mock.remove_calls == 0);
    return 1;
}

static int test_gen4_set_failure_rolls_back_and_stops(void)
{
    mock_services_t mock;
    gen4_action_services_t services;
    gen4_alloc_hook_context_t hook_context;
    uint8_t module_buffer[TEST_MODULE_SIZE] = { 0 };

    mock_initialize(&mock);
    mock.set_status = ACTION_STATUS_DEVICE_ERROR;
    services = gen4_services(&mock);
    memset(&hook_context, 0, sizeof(hook_context));

    CHECK(gen4_apply(&services, EXPECTED_GEN4_BASE, module_buffer,
        sizeof(module_buffer), 12, &hook_context) == ACTION_STATUS_DEVICE_ERROR);
    CHECK(mock.hook_calls == 0);
    CHECK(mock.remove_calls == 1);
    CHECK(mock.call_count == 4 && memcmp(mock.call_log, "PASR", 4) == 0);
    return 1;
}

static int test_gen4_hook_failure_rolls_back_and_stops(void)
{
    mock_services_t mock;
    gen4_action_services_t services;
    gen4_alloc_hook_context_t hook_context;
    uint8_t module_buffer[TEST_MODULE_SIZE] = { 0 };

    mock_initialize(&mock);
    mock.hook_status = ACTION_STATUS_DEVICE_ERROR;
    services = gen4_services(&mock);
    memset(&hook_context, 0, sizeof(hook_context));

    CHECK(gen4_apply(&services, EXPECTED_GEN4_BASE, module_buffer,
        sizeof(module_buffer), 12, &hook_context) == ACTION_STATUS_DEVICE_ERROR);
    CHECK(mock.remove_calls == 1);
    CHECK(mock.call_count == 5 && memcmp(mock.call_log, "PASHR", 5) == 0);
    return 1;
}

static int test_gen4_rollback_failure_is_reported(void)
{
    mock_services_t mock;
    gen4_action_services_t services;
    gen4_alloc_hook_context_t hook_context;
    uint8_t module_buffer[TEST_MODULE_SIZE] = { 0 };

    mock_initialize(&mock);
    mock.set_status = ACTION_STATUS_DEVICE_ERROR;
    mock.remove_status = ACTION_STATUS_DEVICE_ERROR;
    services = gen4_services(&mock);
    memset(&hook_context, 0, sizeof(hook_context));

    CHECK(gen4_apply(&services, EXPECTED_GEN4_BASE, module_buffer,
        sizeof(module_buffer), 12, &hook_context) == ACTION_STATUS_ROLLBACK_FAILED);
    CHECK(mock.hook_calls == 0);
    return 1;
}

static int test_gen4_attribute_hook_preserves_original_failure(void)
{
    mock_services_t mock;
    gen4_alloc_hook_context_t hook_context;
    uint64_t attributes = UINT64_C(0x40);

    mock_initialize(&mock);
    hook_context.original = mock_original_get_attributes;
    hook_context.original_context = &mock;
    mock.original_status = ACTION_STATUS_DEVICE_ERROR;

    CHECK(gen4_get_alloc_attributes_hook(&hook_context, &attributes) ==
        ACTION_STATUS_DEVICE_ERROR);
    CHECK(attributes == UINT64_C(0x40));
    return 1;
}

int main(void)
{
    RUN_TEST(test_gen23_success_and_guid);
    RUN_TEST(test_gen23_locate_failure_stops);
    RUN_TEST(test_gen23_map_failure_stops);
    RUN_TEST(test_gen23_unsafe_map_stops);
    RUN_TEST(test_gen23_invalid_policy_stops);
    RUN_TEST(test_gen23_short_payload_stops);
    RUN_TEST(test_gen5_success_and_guid);
    RUN_TEST(test_gen5_locate_failure_stops);
    RUN_TEST(test_gen5_invalid_policy_stops);
    RUN_TEST(test_gen5_short_payload_stops);
    RUN_TEST(test_gen4_placeholder_refuses_without_writing);
    RUN_TEST(test_gen4_default_placeholder_stops_before_side_effects);
    RUN_TEST(test_gen4_success_path_with_mock_patch_gate);
    RUN_TEST(test_gen4_uses_four_gib_base_when_touud_is_lower);
    RUN_TEST(test_gen4_patch_failure_stops);
    RUN_TEST(test_gen4_add_failure_stops);
    RUN_TEST(test_gen4_set_failure_rolls_back_and_stops);
    RUN_TEST(test_gen4_hook_failure_rolls_back_and_stops);
    RUN_TEST(test_gen4_rollback_failure_is_reported);
    RUN_TEST(test_gen4_attribute_hook_preserves_original_failure);

    if (tests_failed != 0) {
        fprintf(stderr, "%d of %d action tests failed.\n", tests_failed, tests_run);
        return 1;
    }

    printf("All %d action tests passed.\n", tests_run);
    return 0;
}