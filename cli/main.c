#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "above4g_cli.h"

#define ABOVE4G_VARIABLE_GUID L"{B1C7A19D-9E2B-4DF1-9038-C30D81E7A240}"
#define ABOVE4G_SYSTEM_ENVIRONMENT_PRIVILEGE L"SeSystemEnvironmentPrivilege"

typedef DWORD (WINAPI *GET_FIRMWARE_ENVIRONMENT_VARIABLE_EX_W)(
    LPCWSTR Name,
    LPCWSTR Guid,
    PVOID Buffer,
    DWORD BufferSize,
    PDWORD Attributes
);

typedef BOOL (WINAPI *SET_FIRMWARE_ENVIRONMENT_VARIABLE_EX_W)(
    LPCWSTR Name,
    LPCWSTR Guid,
    PVOID Value,
    DWORD ValueSize,
    DWORD Attributes
);

static const wchar_t *native_variable_name(const char *name)
{
    if (strcmp (name, ABOVE4G_ENABLE_VARIABLE_NAME) == 0) {
        return L"Above4GEnabled";
    }
    if (strcmp (name, ABOVE4G_STATUS_VARIABLE_NAME) == 0) {
        return L"Above4GStatus";
    }
    return NULL;
}

static bool enable_firmware_privilege(void *context, uint32_t *error_code)
{
    HANDLE token;
    TOKEN_PRIVILEGES privileges;
    BOOL adjusted;
    DWORD last_error;
    LUID privilege_id;

    (void) context;
    if (!OpenProcessToken (
            GetCurrentProcess (),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
            &token
        )) {
        *error_code = GetLastError ();
        return false;
    }

    if (!LookupPrivilegeValueW (
            NULL,
            ABOVE4G_SYSTEM_ENVIRONMENT_PRIVILEGE,
            &privilege_id
        )) {
        *error_code = GetLastError ();
        CloseHandle (token);
        return false;
    }

    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = privilege_id;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError (ERROR_SUCCESS);
    adjusted = AdjustTokenPrivileges (
                  token,
                  FALSE,
                  &privileges,
                  sizeof (privileges),
                  NULL,
                  NULL
              );
    last_error = GetLastError ();
    CloseHandle (token);
    if (!adjusted || (last_error == ERROR_NOT_ALL_ASSIGNED)) {
        *error_code = last_error;
        return false;
    }

    *error_code = ERROR_SUCCESS;
    return true;
}

static bool get_firmware_variable(
    void *context,
    const char *name,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *data_size,
    uint32_t *error_code
)
{
    const wchar_t *wide_name;
    DWORD bytes_read;
    DWORD attributes;
    FARPROC procedure;
    GET_FIRMWARE_ENVIRONMENT_VARIABLE_EX_W get_variable;

    (void) context;
    wide_name = native_variable_name (name);
    if ((wide_name == NULL) || (buffer == NULL) || (data_size == NULL) ||
        (buffer_size > MAXDWORD)) {
        *error_code = ERROR_INVALID_PARAMETER;
        return false;
    }

    procedure = GetProcAddress (
                    GetModuleHandleW (L"kernel32.dll"),
                    "GetFirmwareEnvironmentVariableExW"
                );
    if (procedure == NULL) {
        *error_code = ERROR_CALL_NOT_IMPLEMENTED;
        return false;
    }
    memcpy (&get_variable, &procedure, sizeof (get_variable));
    attributes = 0;
    bytes_read = get_variable (
                     wide_name,
                     ABOVE4G_VARIABLE_GUID,
                     (PVOID) buffer,
                     (DWORD) buffer_size,
                     &attributes
                 );
    if (bytes_read == 0) {
        *error_code = GetLastError ();
        *data_size = 0;
        return false;
    }

    *data_size = bytes_read;
    *error_code = ERROR_SUCCESS;
    return true;
}

static bool set_firmware_variable(
    void *context,
    const char *name,
    const uint8_t *data,
    size_t data_size,
    uint32_t attributes,
    uint32_t *error_code
)
{
    const wchar_t *wide_name;
    FARPROC procedure;
    SET_FIRMWARE_ENVIRONMENT_VARIABLE_EX_W set_variable;

    (void) context;
    wide_name = native_variable_name (name);
    if ((wide_name == NULL) || (data == NULL) || (data_size == 0) ||
        (data_size > MAXDWORD)) {
        *error_code = ERROR_INVALID_PARAMETER;
        return false;
    }

    procedure = GetProcAddress (
                    GetModuleHandleW (L"kernel32.dll"),
                    "SetFirmwareEnvironmentVariableExW"
                );
    if (procedure == NULL) {
        *error_code = ERROR_CALL_NOT_IMPLEMENTED;
        return false;
    }
    memcpy (&set_variable, &procedure, sizeof (set_variable));
    if (!set_variable (
            wide_name,
            ABOVE4G_VARIABLE_GUID,
            (PVOID) data,
            (DWORD) data_size,
            (DWORD) attributes
        )) {
        *error_code = GetLastError ();
        return false;
    }

    *error_code = ERROR_SUCCESS;
    return true;
}

static void write_console_line(void *context, const char *line)
{
    (void) context;
    puts (line);
}

int main(int argc, char **argv)
{
    above4g_cli_services_t services;

    if (argc != 2) {
        puts ("Usage: above4g <status|enable|disable>");
        return 2;
    }

    services.context = NULL;
    services.enable_firmware_privilege = enable_firmware_privilege;
    services.get_variable = get_firmware_variable;
    services.set_variable = set_firmware_variable;
    services.write_line = write_console_line;
    return above4g_cli_run (argv[1], &services);
}