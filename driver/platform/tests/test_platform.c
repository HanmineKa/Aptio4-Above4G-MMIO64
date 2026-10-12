#include "../platform_testable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_MAX_FILES 4U
#define TEST_MAX_SECTION_SIZE 64U

typedef struct {
    uint8_t file_name[16];
    const uint16_t *user_interface;
    size_t user_interface_size;
    const uint8_t *pe32;
    size_t pe32_size;
} mock_ffs_file_t;

typedef struct {
    mock_ffs_file_t files[TEST_MAX_FILES];
    size_t file_count;
    size_t allocations;
    size_t releases;
} mock_fv_t;

typedef struct {
    size_t next_file;
} mock_fv_key_t;

typedef struct {
    aptio4_platform_function_t current;
    size_t writes;
} mock_hook_slot_t;

static size_t tests_run;
static size_t tests_failed;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf (stderr, "%s:%d: check failed: %s\n", \
                     __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (0)

#define RUN_TEST(test_function) \
    do { \
        ++tests_run; \
        if (!(test_function ())) { \
            ++tests_failed; \
        } \
    } while (0)

static void *mock_allocate_zero(void *context, size_t size)
{
    mock_fv_t *mock = (mock_fv_t *) context;
    void *buffer = calloc (1, size);

    if (buffer != NULL) {
        ++mock->allocations;
    }
    return buffer;
}

static void mock_release(void *context, void *buffer)
{
    mock_fv_t *mock = (mock_fv_t *) context;

    if (buffer != NULL) {
        ++mock->releases;
        free (buffer);
    }
}

static aptio4_platform_io_status_t mock_get_next_file(
    void *context,
    void *key_buffer,
    uint8_t file_name[16],
    size_t *file_size
)
{
    mock_fv_t *mock = (mock_fv_t *) context;
    mock_fv_key_t *key = (mock_fv_key_t *) key_buffer;

    if (key->next_file >= mock->file_count) {
        return APTIO4_PLATFORM_IO_NOT_FOUND;
    }

    memcpy (file_name, mock->files[key->next_file].file_name, 16);
    *file_size = mock->files[key->next_file].pe32_size;
    ++key->next_file;
    return APTIO4_PLATFORM_IO_SUCCESS;
}

static aptio4_platform_io_status_t mock_read_section(
    void *context,
    const uint8_t file_name[16],
    aptio4_platform_section_t section,
    void **buffer,
    size_t *buffer_size,
    uint32_t *authentication_status
)
{
    mock_fv_t *mock = (mock_fv_t *) context;
    size_t file_index;
    const void *source;
    size_t source_size;
    void *copy;

    *buffer = NULL;
    *buffer_size = 0;
    *authentication_status = 0;

    for (file_index = 0; file_index < mock->file_count; ++file_index) {
        if (memcmp (file_name, mock->files[file_index].file_name, 16) == 0) {
            break;
        }
    }
    if (file_index == mock->file_count) {
        return APTIO4_PLATFORM_IO_NOT_FOUND;
    }

    if (section == APTIO4_PLATFORM_SECTION_USER_INTERFACE) {
        source = mock->files[file_index].user_interface;
        source_size = mock->files[file_index].user_interface_size;
    } else if (section == APTIO4_PLATFORM_SECTION_PE32) {
        source = mock->files[file_index].pe32;
        source_size = mock->files[file_index].pe32_size;
    } else {
        return APTIO4_PLATFORM_IO_INVALID_PARAMETER;
    }

    if (source == NULL || source_size == 0) {
        return APTIO4_PLATFORM_IO_NOT_FOUND;
    }

    copy = malloc (source_size);
    if (copy == NULL) {
        return APTIO4_PLATFORM_IO_OUT_OF_RESOURCES;
    }
    ++mock->allocations;
    memcpy (copy, source, source_size);
    *buffer = copy;
    *buffer_size = source_size;
    return APTIO4_PLATFORM_IO_SUCCESS;
}

static aptio4_platform_fv_services_t mock_fv_services(mock_fv_t *mock)
{
    aptio4_platform_fv_services_t services;

    services.context = mock;
    services.key_size = sizeof (mock_fv_key_t);
    services.get_next_file = mock_get_next_file;
    services.read_section = mock_read_section;
    services.allocate_zero = mock_allocate_zero;
    services.release = mock_release;
    return services;
}

static void set_file_name(mock_ffs_file_t *file, uint8_t id)
{
    memset (file->file_name, 0, sizeof (file->file_name));
    file->file_name[0] = id;
}

static bool test_section_name_boundaries(void)
{
    static const uint16_t expected[] = {
        'N', 'b', 'D', 'x', 'e', 0
    };
    static const uint16_t exact[] = {
        'N', 'b', 'D', 'x', 'e', 0
    };
    static const uint16_t longer[] = {
        'N', 'b', 'D', 'x', 'e', 'X', 0
    };
    static const uint16_t shorter[] = {
        'N', 'b', 'D', 'x', 0
    };
    static const uint16_t not_terminated[] = {
        'N', 'b', 'D', 'x', 'e'
    };

    CHECK (SectionNameMatches (exact, sizeof (exact), expected));
    CHECK (!SectionNameMatches (longer, sizeof (longer), expected));
    CHECK (!SectionNameMatches (shorter, sizeof (shorter), expected));
    CHECK (!SectionNameMatches (
               not_terminated,
               sizeof (not_terminated),
               expected
               ));
    CHECK (!SectionNameMatches (exact, sizeof (exact) - 1, expected));
    CHECK (!SectionNameMatches (NULL, sizeof (exact), expected));
    CHECK (!SectionNameMatches (exact, sizeof (exact), NULL));
    return true;
}

static bool test_read_named_module_success(void)
{
    static const uint16_t module_name[] = {
        'P', 'c', 'i', 'H', 'o', 's', 't', 'B', 'r', 'i', 'd', 'g', 'e', 0
    };
    static const uint8_t pe32[] = { 0x4D, 0x5A, 0x01, 0x02 };
    mock_fv_t mock = { 0 };
    aptio4_platform_fv_services_t services;
    uint8_t *module_buffer = NULL;
    size_t module_size = 0;

    mock.file_count = 1;
    set_file_name (&mock.files[0], 1);
    mock.files[0].user_interface = module_name;
    mock.files[0].user_interface_size = sizeof (module_name);
    mock.files[0].pe32 = pe32;
    mock.files[0].pe32_size = sizeof (pe32);
    services = mock_fv_services (&mock);

    CHECK (ReadNamedPe32Module (
               &services,
               module_name,
               &module_buffer,
               &module_size
               ) == APTIO4_PLATFORM_IO_SUCCESS);
    CHECK (module_size == sizeof (pe32));
    CHECK (memcmp (module_buffer, pe32, sizeof (pe32)) == 0);
    mock_release (&mock, module_buffer);
    CHECK (mock.allocations == mock.releases);
    return true;
}

static bool test_read_named_module_duplicate_ui_name(void)
{
    static const uint16_t module_name[] = {
        'N', 'b', 'D', 'x', 'e', 0
    };
    static const uint8_t pe32_a[] = { 0x4D, 0x5A, 0xA1 };
    static const uint8_t pe32_b[] = { 0x4D, 0x5A, 0xB2 };
    mock_fv_t mock = { 0 };
    aptio4_platform_fv_services_t services;
    uint8_t *module_buffer = NULL;
    size_t module_size = 99;

    mock.file_count = 2;
    set_file_name (&mock.files[0], 1);
    set_file_name (&mock.files[1], 2);
    mock.files[0].user_interface = module_name;
    mock.files[1].user_interface = module_name;
    mock.files[0].user_interface_size = sizeof (module_name);
    mock.files[1].user_interface_size = sizeof (module_name);
    mock.files[0].pe32 = pe32_a;
    mock.files[1].pe32 = pe32_b;
    mock.files[0].pe32_size = sizeof (pe32_a);
    mock.files[1].pe32_size = sizeof (pe32_b);
    services = mock_fv_services (&mock);

    CHECK (ReadNamedPe32Module (
               &services,
               module_name,
               &module_buffer,
               &module_size
               ) == APTIO4_PLATFORM_IO_UNSUPPORTED);
    CHECK (module_buffer == NULL);
    CHECK (module_size == 0);
    CHECK (mock.allocations == mock.releases);
    return true;
}

static bool test_read_named_module_missing(void)
{
    static const uint16_t expected_name[] = { 'N', 'b', 'D', 'x', 'e', 0 };
    static const uint16_t other_name[] = { 'O', 't', 'h', 'e', 'r', 0 };
    static const uint8_t pe32[] = { 0x4D, 0x5A };
    mock_fv_t mock = { 0 };
    aptio4_platform_fv_services_t services;
    uint8_t *module_buffer = NULL;
    size_t module_size = 0;

    mock.file_count = 1;
    set_file_name (&mock.files[0], 1);
    mock.files[0].user_interface = other_name;
    mock.files[0].user_interface_size = sizeof (other_name);
    mock.files[0].pe32 = pe32;
    mock.files[0].pe32_size = sizeof (pe32);
    services = mock_fv_services (&mock);

    CHECK (ReadNamedPe32Module (
               &services,
               expected_name,
               &module_buffer,
               &module_size
               ) == APTIO4_PLATFORM_IO_NOT_FOUND);
    CHECK (module_buffer == NULL);
    CHECK (module_size == 0);
    CHECK (mock.allocations == mock.releases);
    return true;
}

static bool test_ranges_overlap_boundaries(void)
{
    CHECK (!RangesOverlap (0x1000, 0x100, 0x1100, 0x80));
    CHECK (!RangesOverlap (0x1100, 0x80, 0x1000, 0x100));
    CHECK (RangesOverlap (0x1000, 0x100, 0x1040, 0x10));
    CHECK (RangesOverlap (0x1030, 0x10, 0x1000, 0x100));
    CHECK (!RangesOverlap (0x1000, 0x10, 0x2000, 0x10));
    CHECK (!RangesOverlap (0x1000, 0, 0x1000, 0x10));
    CHECK (RangesOverlap (UINT64_MAX - 7, 8, UINT64_MAX, 1));
    CHECK (!RangesOverlap (UINT64_MAX - 7, 8, 0, 1));
    return true;
}

static aptio4_platform_function_t mock_read_hook(void *context)
{
    return ((mock_hook_slot_t *) context)->current;
}

static void mock_write_hook(
    void *context,
    aptio4_platform_function_t function
)
{
    mock_hook_slot_t *slot = (mock_hook_slot_t *) context;

    slot->current = function;
    ++slot->writes;
}

static void mock_original_hook(void)
{
}

static void mock_replacement_hook(void)
{
}

static bool test_install_hook_once_preserves_original(void)
{
    mock_hook_slot_t slot = { mock_original_hook, 0 };
    aptio4_platform_function_t original = NULL;

    CHECK (InstallHookOnce (
               &slot,
               mock_read_hook,
               mock_write_hook,
               mock_replacement_hook,
               &original
               ) == APTIO4_PLATFORM_HOOK_INSTALLED);
    CHECK (slot.current == mock_replacement_hook);
    CHECK (slot.writes == 1);
    CHECK (original == mock_original_hook);

    original = mock_original_hook;
    CHECK (InstallHookOnce (
               &slot,
               mock_read_hook,
               mock_write_hook,
               mock_replacement_hook,
               &original
               ) == APTIO4_PLATFORM_HOOK_ALREADY_INSTALLED);
    CHECK (slot.current == mock_replacement_hook);
    CHECK (slot.writes == 1);
    CHECK (original == mock_original_hook);
    return true;
}

static bool test_install_hook_once_rejects_empty_slot(void)
{
    mock_hook_slot_t slot = { NULL, 0 };
    aptio4_platform_function_t original = mock_original_hook;

    CHECK (InstallHookOnce (
               &slot,
               mock_read_hook,
               mock_write_hook,
               mock_replacement_hook,
               &original
               ) == APTIO4_PLATFORM_HOOK_EMPTY);
    CHECK (slot.current == NULL);
    CHECK (slot.writes == 0);
    CHECK (original == mock_original_hook);
    return true;
}

int main(void)
{
    RUN_TEST (test_section_name_boundaries);
    RUN_TEST (test_read_named_module_success);
    RUN_TEST (test_read_named_module_duplicate_ui_name);
    RUN_TEST (test_read_named_module_missing);
    RUN_TEST (test_ranges_overlap_boundaries);
    RUN_TEST (test_install_hook_once_preserves_original);
    RUN_TEST (test_install_hook_once_rejects_empty_slot);

    if (tests_failed != 0) {
        fprintf (stderr, "%zu/%zu platform helper tests failed\n",
                 tests_failed,
                 tests_run);
        return EXIT_FAILURE;
    }

    printf ("All %zu platform helper tests passed.\n", tests_run);
    return EXIT_SUCCESS;
}