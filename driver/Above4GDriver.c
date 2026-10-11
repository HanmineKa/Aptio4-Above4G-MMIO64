#include <Uefi.h>

#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "Above4GPlatformServicesProtocol.h"

#define ABOVE4G_ENABLE_VARIABLE_NAME L"Above4GEnabled"
#define ABOVE4G_STATUS_VARIABLE_NAME L"Above4GStatus"

typedef enum {
    Above4GStatusUnknown = 1,
    Above4GStatusPlatformProviderError,
    Above4GStatusGen23ActionApplied,
    Above4GStatusGen5ActionApplied,
    Above4GStatusGen4ManualReviewRequired,
    Above4GStatusActionFailed
} ABOVE4G_STATUS;

typedef struct {
    EFI_EVENT NotifyEvent;
    VOID *Registration;
    BOOLEAN Completed;
    gen4_alloc_hook_context_t Gen4HookContext;
} ABOVE4G_DRIVER_CONTEXT;

STATIC ABOVE4G_DRIVER_CONTEXT *mDriverContext;

STATIC
EFI_STATUS
WriteStatus (
    IN ABOVE4G_STATUS Status
    )
{
    UINT32 Attributes;
    UINT8 StatusValue;

    Attributes = EFI_VARIABLE_NON_VOLATILE |
                 EFI_VARIABLE_BOOTSERVICE_ACCESS |
                 EFI_VARIABLE_RUNTIME_ACCESS;
    StatusValue = (UINT8) Status;

    return gRT->SetVariable (
                  ABOVE4G_STATUS_VARIABLE_NAME,
                  &gAptio4Above4GVariableGuid,
                  Attributes,
                  sizeof (StatusValue),
                  &StatusValue
                  );
}

STATIC
BOOLEAN
IsFeatureEnabled (
    VOID
    )
{
    EFI_STATUS Status;
    UINT8 Enabled;
    UINTN DataSize;

    Enabled = 0;
    DataSize = sizeof (Enabled);
    Status = gRT->GetVariable (
                    ABOVE4G_ENABLE_VARIABLE_NAME,
                    &gAptio4Above4GVariableGuid,
                    NULL,
                    &DataSize,
                    &Enabled
                    );

    return (BOOLEAN) (
        !EFI_ERROR (Status) &&
        DataSize == sizeof (Enabled) &&
        Enabled == 1
        );
}

STATIC
ABOVE4G_STATUS
RunProfileAction (
    IN APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL *Platform,
    IN firmware_module_t PciHostBridge,
    IN firmware_module_t NbDxe,
    IN UINT8 *Gen4PatchBuffer,
    IN size_t Gen4PatchBufferSize,
    IN size_t Gen4PatchOffset,
    IN UINT64 TouudBase
    )
{
    firmware_profile_t Profile;
    action_status_t ActionStatus;
    gen4_action_services_t Gen4Services;

    Profile = detect_firmware_profile (PciHostBridge, NbDxe);
    switch (Profile) {
    case PROFILE_GEN23:
        ActionStatus = gen23_apply (&Platform->Gen23Services);
        return (ActionStatus == ACTION_STATUS_SUCCESS)
                   ? Above4GStatusGen23ActionApplied
                   : Above4GStatusActionFailed;

    case PROFILE_GEN5:
        ActionStatus = gen5_apply (&Platform->Gen5Services);
        return (ActionStatus == ACTION_STATUS_SUCCESS)
                   ? Above4GStatusGen5ActionApplied
                   : Above4GStatusActionFailed;

    case PROFILE_GEN4_CANDIDATE:
        if ((Gen4PatchBuffer == NULL) ||
            (Gen4PatchBufferSize == 0) ||
            (Gen4PatchOffset >= Gen4PatchBufferSize)) {
            return Above4GStatusPlatformProviderError;
        }

        Gen4Services = Platform->Gen4Services;
        Gen4Services.patch_notify_phase = gen4_notify_phase_patch_placeholder;
        ActionStatus = gen4_apply (
                           &Gen4Services,
                           TouudBase,
                           Gen4PatchBuffer,
                           Gen4PatchBufferSize,
                           Gen4PatchOffset,
                           &mDriverContext->Gen4HookContext
                           );
        return (ActionStatus == ACTION_STATUS_UNSUPPORTED)
                   ? Above4GStatusGen4ManualReviewRequired
                   : Above4GStatusActionFailed;

    case PROFILE_UNKNOWN:
    default:
        return Above4GStatusUnknown;
    }
}

STATIC
EFI_STATUS
ProcessPlatformServices (
    IN APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL *Platform
    )
{
    EFI_STATUS Status;
    firmware_module_t PciHostBridge;
    firmware_module_t NbDxe;
    UINT8 *Gen4PatchBuffer;
    size_t Gen4PatchBufferSize;
    size_t Gen4PatchOffset;
    UINT64 TouudBase;
    ABOVE4G_STATUS DriverStatus;

    if ((Platform == NULL) ||
        (Platform->Revision != APTIO4_ABOVE4G_PLATFORM_SERVICES_REVISION) ||
        (Platform->GetFirmwareModules == NULL)) {
        DriverStatus = Above4GStatusPlatformProviderError;
    } else {
        PciHostBridge.data = NULL;
        PciHostBridge.size = 0;
        NbDxe.data = NULL;
        NbDxe.size = 0;
        Gen4PatchBuffer = NULL;
        Gen4PatchBufferSize = 0;
        Gen4PatchOffset = 0;
        TouudBase = 0;

        Status = Platform->GetFirmwareModules (
                             Platform,
                             &PciHostBridge,
                             &NbDxe,
                             &Gen4PatchBuffer,
                             &Gen4PatchBufferSize,
                             &Gen4PatchOffset,
                             &TouudBase
                             );
        if (EFI_ERROR (Status) ||
            (PciHostBridge.data == NULL) ||
            (PciHostBridge.size == 0) ||
            (NbDxe.data == NULL) ||
            (NbDxe.size == 0)) {
            DriverStatus = Above4GStatusPlatformProviderError;
        } else {
            DriverStatus = RunProfileAction (
                               Platform,
                               PciHostBridge,
                               NbDxe,
                               Gen4PatchBuffer,
                               Gen4PatchBufferSize,
                               Gen4PatchOffset,
                               TouudBase
                               );
        }
    }

    Status = WriteStatus (DriverStatus);
    if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "Above4G: failed to write status: %r\n", Status));
    }

    return Status;
}

STATIC
VOID
EFIAPI
OnPlatformServicesAvailable (
    IN EFI_EVENT Event,
    IN VOID *Context
    )
{
    EFI_STATUS Status;
    APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL *Platform;
    ABOVE4G_DRIVER_CONTEXT *DriverContext;

    DriverContext = (ABOVE4G_DRIVER_CONTEXT *) Context;
    if ((DriverContext == NULL) || DriverContext->Completed) {
        return;
    }

    Status = gBS->LocateProtocol (
                    &gAptio4Above4GPlatformServicesProtocolGuid,
                    NULL,
                    (VOID **) &Platform
                    );
    if (EFI_ERROR (Status)) {
        return;
    }

    DriverContext->Completed = TRUE;
    (VOID) ProcessPlatformServices (Platform);

    if (Event != NULL) {
        gBS->CloseEvent (Event);
        DriverContext->NotifyEvent = NULL;
    }
}

EFI_STATUS
EFIAPI
UefiMain (
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
    )
{
    EFI_STATUS Status;
    APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL *Platform;

    (VOID) ImageHandle;
    (VOID) SystemTable;

    if (!IsFeatureEnabled ()) {
        return EFI_SUCCESS;
    }

    if (mDriverContext != NULL) {
        return EFI_ALREADY_STARTED;
    }

    mDriverContext = AllocateZeroPool (sizeof (*mDriverContext));
    if (mDriverContext == NULL) {
        return WriteStatus (Above4GStatusPlatformProviderError);
    }

    Status = gBS->CreateEvent (
                    EVT_NOTIFY_SIGNAL,
                    TPL_APPLICATION,
                    OnPlatformServicesAvailable,
                    mDriverContext,
                    &mDriverContext->NotifyEvent
                    );
    if (EFI_ERROR (Status)) {
        FreePool (mDriverContext);
        mDriverContext = NULL;
        return WriteStatus (Above4GStatusPlatformProviderError);
    }

    Status = gBS->RegisterProtocolNotify (
                    &gAptio4Above4GPlatformServicesProtocolGuid,
                    mDriverContext->NotifyEvent,
                    &mDriverContext->Registration
                    );
    if (EFI_ERROR (Status)) {
        gBS->CloseEvent (mDriverContext->NotifyEvent);
        FreePool (mDriverContext);
        mDriverContext = NULL;
        return WriteStatus (Above4GStatusPlatformProviderError);
    }

    Status = gBS->LocateProtocol (
                    &gAptio4Above4GPlatformServicesProtocolGuid,
                    NULL,
                    (VOID **) &Platform
                    );
    if (!EFI_ERROR (Status)) {
        mDriverContext->Completed = TRUE;
        Status = ProcessPlatformServices (Platform);
        gBS->CloseEvent (mDriverContext->NotifyEvent);
        mDriverContext->NotifyEvent = NULL;
        return Status;
    }

    if (Status != EFI_NOT_FOUND) {
        gBS->CloseEvent (mDriverContext->NotifyEvent);
        FreePool (mDriverContext);
        mDriverContext = NULL;
        return WriteStatus (Above4GStatusPlatformProviderError);
    }

    return EFI_SUCCESS;
}