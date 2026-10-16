#include "../above4g_cli.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf (stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

typedef struct {
    bool privilege_succeeds;
    bool enabled_exists;
    bool status_exists;
    bool get_succeeds;
    bool set_succeeds;
    uint8_t enabled_value;
    uint8_t status_value;
    size_t enabled_size;
    size_t status_size;
    uint8_t written_value;
    uint32_t written_attributes;
    uint32_t error_code;
    unsigned int get_calls;
    unsigned int set_calls;
    char events[16];
    size_t event_count;
    char output[1024];
    size_t output_size;
} mock_t;

static void record_event(mock_t *mock, char event)
{
    if (mock->event_count < sizeof (mock->events) - 1) {
        mock->events[mock->event_count++] = event;
        mock->events[mock->event_count] = '\0';
    }
}

static bool mock_enable_privilege(void *context, uint32_t *error_code)
{
    mock_t *mock = (mock_t *) context;

    record_event (mock, 'P');
    *error_code = mock->privilege_succeeds ? 0 : 1300;
    return mock->privilege_succeeds;
}

static bool mock_get_variable(
    void *context,
    const char *name,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *data_size,
    uint32_t *error_code
)
{
    mock_t *mock = (mock_t *) context;

    ++mock->get_calls;
    record_event (mock, 'G');
    if ((strcmp (name, ABOVE4G_ENABLE_VARIABLE_NAME) == 0) &&
        mock->enabled_exists) {
        *buffer = mock->enabled_value;
        *data_size = mock->enabled_size;
    } else if ((strcmp (name, ABOVE4G_STATUS_VARIABLE_NAME) == 0) &&
               mock->status_exists) {
        *buffer = mock->status_value;
        *data_size = mock->status_size;
    } else {
        *error_code = ABOVE4G_ERROR_ENVVAR_NOT_FOUND;
        *data_size = 0;
        return false;
    }

    if (!mock->get_succeeds) {
        *error_code = mock->error_code;
        return false;
    }
    if (buffer_size < *data_size) {
        *error_code = 122;
        return false;
    }

    *error_code = 0;
    return true;
}

static bool mock_set_variable(
    void *context,
    const char *name,
    const uint8_t *data,
    size_t data_size,
    uint32_t attributes,
    uint32_t *error_code
)
{
    mock_t *mock = (mock_t *) context;

    ++mock->set_calls;
    record_event (mock, 'S');
    if ((strcmp (name, ABOVE4G_ENABLE_VARIABLE_NAME) != 0) ||
        (data_size != 1)) {
        *error_code = 87;
        return false;
    }

    mock->written_value = *data;
    mock->written_attributes = attributes;
    *error_code = mock->error_code;
    return mock->set_succeeds;
}

static void mock_write_line(void *context, const char *line)
{
    mock_t *mock = (mock_t *) context;
    size_t line_size;
    size_t available;

    line_size = strlen (line);
    available = sizeof (mock->output) - mock->output_size - 1;
    if (line_size > available) {
        line_size = available;
    }
    memcpy (mock->output + mock->output_size, line, line_size);
    mock->output_size += line_size;
    mock->output[mock->output_size++] = '\n';
    mock->output[mock->output_size] = '\0';
}

static void initialize_mock(mock_t *mock)
{
    memset (mock, 0, sizeof (*mock));
    mock->privilege_succeeds = true;
    mock->get_succeeds = true;
    mock->set_succeeds = true;
    mock->enabled_exists = true;
    mock->enabled_size = 1;
    mock->status_size = 1;
}

static above4g_cli_services_t mock_services(mock_t *mock)
{
    above4g_cli_services_t services;

    services.context = mock;
    services.enable_firmware_privilege = mock_enable_privilege;
    services.get_variable = mock_get_variable;
    services.set_variable = mock_set_variable;
    services.write_line = mock_write_line;
    return services;
}

static int test_status_reads_and_decodes_variables(void)
{
    mock_t mock;
    above4g_cli_services_t services;

    initialize_mock (&mock);
    mock.enabled_value = 1;
    mock.status_exists = true;
    mock.status_value = ABOVE4G_STATUS_GEN5_ACTION_APPLIED;
    services = mock_services (&mock);
    CHECK (above4g_cli_run ("status", &services) == 0);
    CHECK (strcmp (mock.events, "PGG") == 0);
    CHECK (strstr (mock.output, "setting: Enabled") != NULL);
    CHECK (strstr (mock.output, "Gen 5 action applied") != NULL);
    CHECK (mock.set_calls == 0);
    return 0;
}

static int test_enable_writes_private_variable_and_reboot_notice(void)
{
    mock_t mock;
    above4g_cli_services_t services;

    initialize_mock (&mock);
    services = mock_services (&mock);
    CHECK (above4g_cli_run ("enable", &services) == 0);
    CHECK (strcmp (mock.events, "PS") == 0);
    CHECK (mock.set_calls == 1);
    CHECK (mock.written_value == 1);
    CHECK (mock.written_attributes == ABOVE4G_VARIABLE_ATTRIBUTES);
    CHECK (strstr (mock.output, "takes effect after reboot") != NULL);
    CHECK (strstr (mock.output, "additional automatic reboot") != NULL);
    return 0;
}

static int test_disable_writes_zero(void)
{
    mock_t mock;
    above4g_cli_services_t services;

    initialize_mock (&mock);
    services = mock_services (&mock);
    CHECK (above4g_cli_run ("disable", &services) == 0);
    CHECK (mock.written_value == 0);
    CHECK (mock.written_attributes == ABOVE4G_VARIABLE_ATTRIBUTES);
    CHECK (strstr (mock.output, "set to Disabled") != NULL);
    return 0;
}

static int test_missing_variables_fail_closed_as_disabled(void)
{
    mock_t mock;
    above4g_cli_services_t services;

    initialize_mock (&mock);
    mock.enabled_exists = false;
    mock.status_exists = false;
    services = mock_services (&mock);
    CHECK (above4g_cli_run ("status", &services) == 0);
    CHECK (strstr (mock.output, "setting: Disabled") != NULL);
    CHECK (strstr (mock.output, "No status recorded") != NULL);
    return 0;
}

static int test_privilege_failure_stops_before_firmware_calls(void)
{
    mock_t mock;
    above4g_cli_services_t services;

    initialize_mock (&mock);
    mock.privilege_succeeds = false;
    services = mock_services (&mock);
    CHECK (above4g_cli_run ("enable", &services) == 1);
    CHECK (strcmp (mock.events, "P") == 0);
    CHECK (mock.set_calls == 0);
    CHECK (strstr (mock.output, "Run this CLI as Administrator") != NULL);
    return 0;
}

static int test_write_failure_does_not_claim_success(void)
{
    mock_t mock;
    above4g_cli_services_t services;

    initialize_mock (&mock);
    mock.set_succeeds = false;
    mock.error_code = 5;
    services = mock_services (&mock);
    CHECK (above4g_cli_run ("enable", &services) == 1);
    CHECK (strstr (mock.output, "Could not write Above4GEnabled") != NULL);
    CHECK (strstr (mock.output, "takes effect after reboot") == NULL);
    return 0;
}

static int test_invalid_values_and_commands_are_rejected(void)
{
    mock_t mock;
    above4g_cli_services_t services;

    initialize_mock (&mock);
    mock.enabled_value = 2;
    services = mock_services (&mock);
    CHECK (above4g_cli_run ("status", &services) == 1);
    CHECK (strstr (mock.output, "invalid size or value") != NULL);

    initialize_mock (&mock);
    services = mock_services (&mock);
    CHECK (above4g_cli_run ("toggle", &services) == 2);
    CHECK (mock.event_count == 0);
    CHECK (strstr (mock.output, "Usage:") != NULL);
    return 0;
}

int main(void)
{
    CHECK (test_status_reads_and_decodes_variables () == 0);
    CHECK (test_enable_writes_private_variable_and_reboot_notice () == 0);
    CHECK (test_disable_writes_zero () == 0);
    CHECK (test_missing_variables_fail_closed_as_disabled () == 0);
    CHECK (test_privilege_failure_stops_before_firmware_calls () == 0);
    CHECK (test_write_failure_does_not_claim_success () == 0);
    CHECK (test_invalid_values_and_commands_are_rejected () == 0);
    puts ("7 CLI mock tests passed");
    return 0;
}