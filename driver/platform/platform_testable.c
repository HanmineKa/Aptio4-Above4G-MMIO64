#include "platform_testable.h"

#include <stdlib.h>
#include <string.h>

#define PLATFORM_MAX_FV_KEY_SIZE 4096U

static void release_if_present(
    const aptio4_platform_fv_services_t *services,
    void *buffer
)
{
    if (buffer != NULL) {
        services->release(services->context, buffer);
    }
}

bool SectionNameMatches(
    const uint16_t *section_name,
    size_t section_size,
    const uint16_t *expected_name
)
{
    size_t index;
    size_t character_count;

    if (section_name == NULL || expected_name == NULL ||
        section_size < sizeof (*section_name) ||
        section_size % sizeof (*section_name) != 0) {
        return false;
    }

    character_count = section_size / sizeof (*section_name);
    for (index = 0; expected_name[index] != 0; ++index) {
        if (index >= character_count ||
            section_name[index] != expected_name[index]) {
            return false;
        }
    }

    return index < character_count && section_name[index] == 0;
}

aptio4_platform_io_status_t ReadNamedPe32Module(
    const aptio4_platform_fv_services_t *services,
    const uint16_t *expected_name,
    uint8_t **module_buffer,
    size_t *module_size
)
{
    aptio4_platform_io_status_t status;
    uint8_t file_name[16];
    size_t file_size;
    uint32_t authentication_status;
    void *user_interface;
    size_t user_interface_size;
    void *pe32_buffer;
    size_t pe32_size;
    void *key;
    bool found;

    if (services == NULL || expected_name == NULL || module_buffer == NULL ||
        module_size == NULL || services->get_next_file == NULL ||
        services->read_section == NULL || services->allocate_zero == NULL ||
        services->release == NULL) {
        return APTIO4_PLATFORM_IO_INVALID_PARAMETER;
    }

    *module_buffer = NULL;
    *module_size = 0;
    if (services->key_size == 0 ||
        services->key_size > PLATFORM_MAX_FV_KEY_SIZE) {
        return APTIO4_PLATFORM_IO_UNSUPPORTED;
    }

    key = services->allocate_zero (services->context, services->key_size);
    if (key == NULL) {
        return APTIO4_PLATFORM_IO_OUT_OF_RESOURCES;
    }

    found = false;
    for (;;) {
        file_size = 0;
        status = services->get_next_file (
                     services->context,
                     key,
                     file_name,
                     &file_size
                     );
        if (status != APTIO4_PLATFORM_IO_SUCCESS) {
            if (status == APTIO4_PLATFORM_IO_NOT_FOUND) {
                break;
            }

            services->release (services->context, key);
            release_if_present (services, *module_buffer);
            *module_buffer = NULL;
            *module_size = 0;
            return status;
        }

        user_interface = NULL;
        user_interface_size = 0;
        authentication_status = 0;
        status = services->read_section (
                     services->context,
                     file_name,
                     APTIO4_PLATFORM_SECTION_USER_INTERFACE,
                     &user_interface,
                     &user_interface_size,
                     &authentication_status
                     );
        if ((status != APTIO4_PLATFORM_IO_SUCCESS) ||
            !SectionNameMatches (
                 (const uint16_t *) user_interface,
                 user_interface_size,
                 expected_name
                 )) {
            release_if_present (services, user_interface);
            if ((status == APTIO4_PLATFORM_IO_SUCCESS) ||
                (status == APTIO4_PLATFORM_IO_NOT_FOUND) ||
                (status == APTIO4_PLATFORM_IO_ACCESS_DENIED)) {
                continue;
            }

            services->release (services->context, key);
            release_if_present (services, *module_buffer);
            *module_buffer = NULL;
            *module_size = 0;
            return status;
        }

        services->release (services->context, user_interface);
        if (found) {
            services->release (services->context, *module_buffer);
            *module_buffer = NULL;
            *module_size = 0;
            services->release (services->context, key);
            return APTIO4_PLATFORM_IO_UNSUPPORTED;
        }

        pe32_buffer = NULL;
        pe32_size = 0;
        status = services->read_section (
                     services->context,
                     file_name,
                     APTIO4_PLATFORM_SECTION_PE32,
                     &pe32_buffer,
                     &pe32_size,
                     &authentication_status
                     );
        if ((status != APTIO4_PLATFORM_IO_SUCCESS) ||
            (pe32_buffer == NULL) || (pe32_size == 0)) {
            release_if_present (services, pe32_buffer);
            services->release (services->context, key);
            return APTIO4_PLATFORM_IO_UNSUPPORTED;
        }

        *module_buffer = (uint8_t *) pe32_buffer;
        *module_size = pe32_size;
        found = true;
    }

    services->release (services->context, key);
    return found ? APTIO4_PLATFORM_IO_SUCCESS : APTIO4_PLATFORM_IO_NOT_FOUND;
}

bool RangesOverlap(
    uint64_t left_base,
    uint64_t left_length,
    uint64_t right_base,
    uint64_t right_length
)
{
    if (left_length == 0 || right_length == 0) {
        return false;
    }

    if (left_base <= right_base) {
        return (right_base - left_base) < left_length;
    }

    return (left_base - right_base) < right_length;
}

aptio4_platform_hook_status_t InstallHookOnce(
    void *context,
    aptio4_platform_hook_read_fn read_hook,
    aptio4_platform_hook_write_fn write_hook,
    aptio4_platform_function_t replacement,
    aptio4_platform_function_t *original
)
{
    aptio4_platform_function_t current;

    if (read_hook == NULL || write_hook == NULL || replacement == NULL ||
        original == NULL) {
        return APTIO4_PLATFORM_HOOK_INVALID_PARAMETER;
    }

    current = read_hook (context);
    if (current == NULL) {
        return APTIO4_PLATFORM_HOOK_EMPTY;
    }

    if (current == replacement) {
        return APTIO4_PLATFORM_HOOK_ALREADY_INSTALLED;
    }

    *original = current;
    write_hook (context, replacement);
    return APTIO4_PLATFORM_HOOK_INSTALLED;
}