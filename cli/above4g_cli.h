#ifndef ABOVE4G_CLI_H
#define ABOVE4G_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ABOVE4G_ENABLE_VARIABLE_NAME "Above4GEnabled"
#define ABOVE4G_STATUS_VARIABLE_NAME "Above4GStatus"
#define ABOVE4G_VARIABLE_ATTRIBUTES 7U
#define ABOVE4G_ERROR_ENVVAR_NOT_FOUND 203U

enum {
    ABOVE4G_STATUS_UNKNOWN = 1,
    ABOVE4G_STATUS_PLATFORM_PROVIDER_ERROR,
    ABOVE4G_STATUS_GEN23_ACTION_APPLIED,
    ABOVE4G_STATUS_GEN5_ACTION_APPLIED,
    ABOVE4G_STATUS_GEN4_MANUAL_REVIEW_REQUIRED,
    ABOVE4G_STATUS_ACTION_FAILED
};

typedef struct {
    void *context;
    bool (*enable_firmware_privilege)(void *context, uint32_t *error_code);
    bool (*get_variable)(
        void *context,
        const char *name,
        uint8_t *buffer,
        size_t buffer_size,
        size_t *data_size,
        uint32_t *error_code
    );
    bool (*set_variable)(
        void *context,
        const char *name,
        const uint8_t *data,
        size_t data_size,
        uint32_t attributes,
        uint32_t *error_code
    );
    void (*write_line)(void *context, const char *line);
} above4g_cli_services_t;

int above4g_cli_run(
    const char *command,
    const above4g_cli_services_t *services
);

#endif