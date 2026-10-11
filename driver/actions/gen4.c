#include "action_api.h"

#define GEN4_GCD_MEMORY_MAPPED_IO 3U
#define GEN4_MMIO64_MIN_BASE UINT64_C(0x100000000)
#define GEN4_MMIO64_WINDOW_SIZE UINT64_C(0x100000000)
#define GEN4_MEMORY_SPACE_ATTRIBUTES UINT64_C(1)
#define GEN4_MEM64_DECODE_ATTRIBUTE UINT64_C(2)

static action_status_t rollback_memory_space(
    const gen4_action_services_t *services,
    uint64_t base
)
{
    action_status_t rollback_status;

    rollback_status = services->remove_memory_space(
        services->context,
        GEN4_GCD_MEMORY_MAPPED_IO,
        base,
        GEN4_MMIO64_WINDOW_SIZE
    );
    if (rollback_status != ACTION_STATUS_SUCCESS) {
        return ACTION_STATUS_ROLLBACK_FAILED;
    }

    return ACTION_STATUS_SUCCESS;
}

action_status_t gen4_get_alloc_attributes_hook(
    void *context,
    uint64_t *attributes
)
{
    gen4_alloc_hook_context_t *hook_context =
        (gen4_alloc_hook_context_t *)context;
    action_status_t status;

    if (hook_context == NULL || hook_context->original == NULL ||
        attributes == NULL) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    status = hook_context->original(hook_context->original_context, attributes);
    if (status != ACTION_STATUS_SUCCESS) {
        return status;
    }

    *attributes |= GEN4_MEM64_DECODE_ATTRIBUTE;
    return ACTION_STATUS_SUCCESS;
}

action_status_t gen4_notify_phase_patch_placeholder(
    void *context,
    uint8_t *module_buffer,
    size_t module_size,
    size_t patch_offset
)
{
    (void)context;

    if (module_buffer == NULL || module_size == 0 || patch_offset >= module_size) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    // WAJIB REVIEW MANUAL SEBELUM DIPAKAI DI HARDWARE NYATA
    return ACTION_STATUS_UNSUPPORTED;
}

action_status_t gen4_apply(
    const gen4_action_services_t *services,
    uint64_t touud_base,
    uint8_t *module_buffer,
    size_t module_size,
    size_t patch_offset,
    gen4_alloc_hook_context_t *hook_context
)
{
    action_status_t status;
    action_status_t rollback_status;
    uint64_t base;

    if (services == NULL || services->patch_notify_phase == NULL ||
        services->add_memory_space == NULL ||
        services->set_memory_space_attributes == NULL ||
        services->remove_memory_space == NULL ||
        services->install_get_alloc_attributes_hook == NULL ||
        module_buffer == NULL || module_size == 0 || patch_offset >= module_size ||
        hook_context == NULL) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    base = touud_base < GEN4_MMIO64_MIN_BASE ? GEN4_MMIO64_MIN_BASE : touud_base;
    if (base > UINT64_MAX - GEN4_MMIO64_WINDOW_SIZE) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    hook_context->original = NULL;
    hook_context->original_context = NULL;

    status = services->patch_notify_phase(
        services->context,
        module_buffer,
        module_size,
        patch_offset
    );
    if (status != ACTION_STATUS_SUCCESS) {
        return status;
    }

    status = services->add_memory_space(
        services->context,
        GEN4_GCD_MEMORY_MAPPED_IO,
        base,
        GEN4_MMIO64_WINDOW_SIZE,
        0
    );
    if (status != ACTION_STATUS_SUCCESS) {
        return status;
    }

    status = services->set_memory_space_attributes(
        services->context,
        base,
        GEN4_MMIO64_WINDOW_SIZE,
        GEN4_MEMORY_SPACE_ATTRIBUTES
    );
    if (status != ACTION_STATUS_SUCCESS) {
        rollback_status = rollback_memory_space(services, base);
        return rollback_status == ACTION_STATUS_SUCCESS
            ? status
            : rollback_status;
    }

    status = services->install_get_alloc_attributes_hook(
        services->context,
        gen4_get_alloc_attributes_hook,
        hook_context
    );
    if (status != ACTION_STATUS_SUCCESS) {
        rollback_status = rollback_memory_space(services, base);
        return rollback_status == ACTION_STATUS_SUCCESS
            ? status
            : rollback_status;
    }

    return ACTION_STATUS_SUCCESS;
}