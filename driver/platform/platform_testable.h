#ifndef APTIO4_ABOVE4G_PLATFORM_TESTABLE_H
#define APTIO4_ABOVE4G_PLATFORM_TESTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    APTIO4_PLATFORM_IO_SUCCESS = 0,
    APTIO4_PLATFORM_IO_NOT_FOUND,
    APTIO4_PLATFORM_IO_ACCESS_DENIED,
    APTIO4_PLATFORM_IO_OUT_OF_RESOURCES,
    APTIO4_PLATFORM_IO_INVALID_PARAMETER,
    APTIO4_PLATFORM_IO_UNSUPPORTED,
    APTIO4_PLATFORM_IO_ERROR
} aptio4_platform_io_status_t;

typedef enum {
    APTIO4_PLATFORM_SECTION_USER_INTERFACE = 1,
    APTIO4_PLATFORM_SECTION_PE32
} aptio4_platform_section_t;

typedef struct {
    void *context;
    size_t key_size;
    aptio4_platform_io_status_t (*get_next_file)(
        void *context,
        void *key,
        uint8_t file_name[16],
        size_t *file_size
    );
    aptio4_platform_io_status_t (*read_section)(
        void *context,
        const uint8_t file_name[16],
        aptio4_platform_section_t section,
        void **buffer,
        size_t *buffer_size,
        uint32_t *authentication_status
    );
    void *(*allocate_zero)(void *context, size_t size);
    void (*release)(void *context, void *buffer);
} aptio4_platform_fv_services_t;

typedef enum {
    APTIO4_PLATFORM_HOOK_INSTALLED = 0,
    APTIO4_PLATFORM_HOOK_INVALID_PARAMETER,
    APTIO4_PLATFORM_HOOK_EMPTY,
    APTIO4_PLATFORM_HOOK_ALREADY_INSTALLED
} aptio4_platform_hook_status_t;

typedef void (*aptio4_platform_function_t)(void);
typedef aptio4_platform_function_t (*aptio4_platform_hook_read_fn)(void *context);
typedef void (*aptio4_platform_hook_write_fn)(
    void *context,
    aptio4_platform_function_t function
);

bool SectionNameMatches(
    const uint16_t *section_name,
    size_t section_size,
    const uint16_t *expected_name
);

aptio4_platform_io_status_t ReadNamedPe32Module(
    const aptio4_platform_fv_services_t *services,
    const uint16_t *expected_name,
    uint8_t **module_buffer,
    size_t *module_size
);

bool RangesOverlap(
    uint64_t left_base,
    uint64_t left_length,
    uint64_t right_base,
    uint64_t right_length
);

aptio4_platform_hook_status_t InstallHookOnce(
    void *context,
    aptio4_platform_hook_read_fn read_hook,
    aptio4_platform_hook_write_fn write_hook,
    aptio4_platform_function_t replacement,
    aptio4_platform_function_t *original
);

#endif