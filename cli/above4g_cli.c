#include "above4g_cli.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    VARIABLE_READ_OK,
    VARIABLE_READ_MISSING,
    VARIABLE_READ_ERROR,
    VARIABLE_READ_INVALID
} variable_read_result_t;

static void write_line(
    const above4g_cli_services_t *services,
    const char *line
)
{
    services->write_line (services->context, line);
}

static variable_read_result_t read_byte(
    const above4g_cli_services_t *services,
    const char *name,
    uint8_t *value,
    uint32_t *error_code
)
{
    size_t data_size;

    data_size = 0;
    if (!services->get_variable (
            services->context,
            name,
            value,
            sizeof (*value),
            &data_size,
            error_code
        )) {
        return (*error_code == ABOVE4G_ERROR_ENVVAR_NOT_FOUND)
                   ? VARIABLE_READ_MISSING
                   : VARIABLE_READ_ERROR;
    }

    return (data_size == sizeof (*value))
               ? VARIABLE_READ_OK
               : VARIABLE_READ_INVALID;
}

static const char *status_description(uint8_t status)
{
    switch (status) {
    case ABOVE4G_STATUS_UNKNOWN:
        return "Unknown firmware; no profile action was taken.";
    case ABOVE4G_STATUS_PLATFORM_PROVIDER_ERROR:
        return "Platform provider error.";
    case ABOVE4G_STATUS_GEN23_ACTION_APPLIED:
        return "Gen 2-3 action applied; hardware behavior is not verified.";
    case ABOVE4G_STATUS_GEN5_ACTION_APPLIED:
        return "Gen 5 action applied; hardware behavior is not verified.";
    case ABOVE4G_STATUS_GEN4_MANUAL_REVIEW_REQUIRED:
        return "Gen 4 requires manual review; no profile action was applied.";
    case ABOVE4G_STATUS_ACTION_FAILED:
        return "Profile action failed.";
    default:
        return "Unrecognized driver status value.";
    }
}

static int show_status(const above4g_cli_services_t *services)
{
    char message[96];
    uint8_t enabled;
    uint8_t status;
    uint32_t error_code;
    variable_read_result_t result;

    error_code = 0;
    result = read_byte (
                 services,
                 ABOVE4G_ENABLE_VARIABLE_NAME,
                 &enabled,
                 &error_code
             );
    if (result == VARIABLE_READ_MISSING) {
        enabled = 0;
    } else if (result == VARIABLE_READ_ERROR) {
        (void) snprintf (
            message,
            sizeof (message),
            "Could not read Above4GEnabled (Windows error %lu).",
            (unsigned long) error_code
        );
        write_line (services, message);
        return 1;
    } else if ((result == VARIABLE_READ_INVALID) || (enabled > 1)) {
        write_line (services, "Above4GEnabled has an invalid size or value.");
        return 1;
    }

    write_line (
        services,
        enabled ? "Above 4G driver setting: Enabled"
                : "Above 4G driver setting: Disabled"
    );

    error_code = 0;
    result = read_byte (
                 services,
                 ABOVE4G_STATUS_VARIABLE_NAME,
                 &status,
                 &error_code
             );
    if (result == VARIABLE_READ_MISSING) {
        write_line (services, "Last driver result: No status recorded.");
        return 0;
    }
    if (result == VARIABLE_READ_ERROR) {
        (void) snprintf (
            message,
            sizeof (message),
            "Could not read Above4GStatus (Windows error %lu).",
            (unsigned long) error_code
        );
        write_line (services, message);
        return 1;
    }
    if (result == VARIABLE_READ_INVALID) {
        write_line (services, "Above4GStatus has an invalid size.");
        return 1;
    }

    (void) snprintf (
        message,
        sizeof (message),
        "Last driver result: %s",
        status_description (status)
    );
    write_line (services, message);
    return 0;
}

int above4g_cli_run(
    const char *command,
    const above4g_cli_services_t *services
)
{
    uint8_t enabled;
    uint32_t error_code;
    char message[112];
    bool is_enable;

    if ((command == NULL) || (services == NULL) ||
        (services->enable_firmware_privilege == NULL) ||
        (services->get_variable == NULL) ||
        (services->set_variable == NULL) ||
        (services->write_line == NULL)) {
        return 1;
    }

    if (strcmp (command, "status") == 0) {
        is_enable = false;
    } else if (strcmp (command, "enable") == 0) {
        is_enable = true;
    } else if (strcmp (command, "disable") == 0) {
        is_enable = false;
    } else {
        write_line (services, "Usage: above4g <status|enable|disable>");
        return 2;
    }

    error_code = 0;
    if (!services->enable_firmware_privilege (
            services->context,
            &error_code
        )) {
        (void) snprintf (
            message,
            sizeof (message),
            "Cannot access firmware variables (Windows error %lu). Run this CLI as Administrator with SE_SYSTEM_ENVIRONMENT_NAME enabled.",
            (unsigned long) error_code
        );
        write_line (services, message);
        return 1;
    }

    if (strcmp (command, "status") == 0) {
        return show_status (services);
    }

    enabled = is_enable ? 1 : 0;
    error_code = 0;
    if (!services->set_variable (
            services->context,
            ABOVE4G_ENABLE_VARIABLE_NAME,
            &enabled,
            sizeof (enabled),
            ABOVE4G_VARIABLE_ATTRIBUTES,
            &error_code
        )) {
        (void) snprintf (
            message,
            sizeof (message),
            "Could not write Above4GEnabled (Windows error %lu).",
            (unsigned long) error_code
        );
        write_line (services, message);
        return 1;
    }

    write_line (
        services,
        is_enable ? "Above4GEnabled set to Enabled."
                  : "Above4GEnabled set to Disabled."
    );
    write_line (services, "The change takes effect after reboot.");
    write_line (
        services,
        "On Gen 4 firmware, one additional automatic reboot may occur through MemCeil."
    );
    return 0;
}