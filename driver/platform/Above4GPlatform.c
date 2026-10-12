#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PciHostBridgeResourceAllocation.h>
#include <Protocol/PciRootBridgeIo.h>

#include "../Above4GPlatformServicesProtocol.h"
#include "platform_testable.h"

#define PLATFORM_POLICY_POINTER_OFFSET 0x28U
#define PLATFORM_POLICY_INTERFACE_SIZE 0x30U
#define GEN23_POLICY_DATA_SIZE 0x12U
#define GEN5_POLICY_DATA_SIZE 0x21U
#define GEN23_POLICY_FLAG_OFFSET 0x11U
#define GEN5_POLICY_FLAG_OFFSET 0x20U
#define GEN23_MMIO64_WINDOW_SIZE 0x400000000ULL
#define MMIO64_MIN_BASE 0x100000000ULL
#define GCD_MAP_MAX_DESCRIPTORS 4096U
#define ABOVE4G_ENABLE_VARIABLE_NAME L"Above4GEnabled"

typedef struct {
    VOID *PciHostBridgeBuffer;
    UINTN PciHostBridgeSize;
    VOID *NbDxeBuffer;
    UINTN NbDxeSize;
    UINT64 TouudBase;
} PLATFORM_CONTEXT;

typedef EFI_STATUS (EFIAPI *PCI_HB_GET_ALLOC_ATTRIBUTES)(
    IN EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *This,
    IN EFI_HANDLE RootBridgeHandle,
    OUT UINT64 *Attributes
    );

STATIC PLATFORM_CONTEXT mContext;
STATIC EFI_HANDLE mProviderImageHandle;
STATIC EFI_HANDLE mProviderProtocolHandle;
STATIC EFI_EVENT mPolicyNotifyEvent;
STATIC VOID *mPolicyRegistration;
STATIC EFI_GUID mPendingPolicyGuid;
STATIC EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *mHostBridgeAllocation;
STATIC PCI_HB_GET_ALLOC_ATTRIBUTES mOriginalGetAllocAttributes;
STATIC EFI_HANDLE mCurrentRootBridgeHandle;
STATIC gen4_get_alloc_attributes_fn mInstalledAttributesHook;
STATIC gen4_alloc_hook_context_t *mInstalledHookContext;
STATIC APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL mPlatformServices;

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
        (DataSize == sizeof (Enabled)) &&
        (Enabled == 1)
        );
}

STATIC
EFI_STATUS
MapActionStatusToEfiStatus (
    IN action_status_t ActionStatus
    )
{
    switch (ActionStatus) {
    case ACTION_STATUS_SUCCESS:
        return EFI_SUCCESS;
    case ACTION_STATUS_INVALID_PARAMETER:
        return EFI_INVALID_PARAMETER;
    case ACTION_STATUS_NOT_FOUND:
        return EFI_NOT_FOUND;
    case ACTION_STATUS_OUT_OF_RESOURCES:
        return EFI_OUT_OF_RESOURCES;
    case ACTION_STATUS_UNSUPPORTED:
        return EFI_UNSUPPORTED;
    default:
        return EFI_DEVICE_ERROR;
    }
}

STATIC
action_status_t
MapEfiStatusToActionStatus (
    IN EFI_STATUS Status
    )
{
    if (!EFI_ERROR (Status)) {
        return ACTION_STATUS_SUCCESS;
    }

    if (Status == EFI_NOT_FOUND) {
        return ACTION_STATUS_NOT_FOUND;
    }

    if (Status == EFI_INVALID_PARAMETER) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    if (Status == EFI_OUT_OF_RESOURCES) {
        return ACTION_STATUS_OUT_OF_RESOURCES;
    }

    if (Status == EFI_UNSUPPORTED) {
        return ACTION_STATUS_UNSUPPORTED;
    }

    return ACTION_STATUS_DEVICE_ERROR;
}

typedef struct {
    EFI_FIRMWARE_VOLUME2_PROTOCOL *FirmwareVolume;
} PLATFORM_FV2_ADAPTER;

STATIC
aptio4_platform_io_status_t
MapEfiStatusToPlatformIoStatus (
    IN EFI_STATUS Status
    )
{
    if (Status == EFI_SUCCESS) {
        return APTIO4_PLATFORM_IO_SUCCESS;
    }
    if (Status == EFI_NOT_FOUND) {
        return APTIO4_PLATFORM_IO_NOT_FOUND;
    }
    if (Status == EFI_ACCESS_DENIED) {
        return APTIO4_PLATFORM_IO_ACCESS_DENIED;
    }
    if (Status == EFI_OUT_OF_RESOURCES) {
        return APTIO4_PLATFORM_IO_OUT_OF_RESOURCES;
    }
    if (Status == EFI_INVALID_PARAMETER) {
        return APTIO4_PLATFORM_IO_INVALID_PARAMETER;
    }
    if (Status == EFI_UNSUPPORTED) {
        return APTIO4_PLATFORM_IO_UNSUPPORTED;
    }
    return APTIO4_PLATFORM_IO_ERROR;
}

STATIC
EFI_STATUS
MapPlatformIoStatusToEfiStatus (
    IN aptio4_platform_io_status_t Status
    )
{
    switch (Status) {
    case APTIO4_PLATFORM_IO_SUCCESS:
        return EFI_SUCCESS;
    case APTIO4_PLATFORM_IO_NOT_FOUND:
        return EFI_NOT_FOUND;
    case APTIO4_PLATFORM_IO_ACCESS_DENIED:
        return EFI_ACCESS_DENIED;
    case APTIO4_PLATFORM_IO_OUT_OF_RESOURCES:
        return EFI_OUT_OF_RESOURCES;
    case APTIO4_PLATFORM_IO_INVALID_PARAMETER:
        return EFI_INVALID_PARAMETER;
    case APTIO4_PLATFORM_IO_UNSUPPORTED:
        return EFI_UNSUPPORTED;
    default:
        return EFI_DEVICE_ERROR;
    }
}

STATIC
aptio4_platform_io_status_t
Fv2GetNextFile (
    IN VOID *Context,
    IN VOID *Key,
    OUT UINT8 FileName[16],
    OUT size_t *FileSize
    )
{
    EFI_STATUS Status;
    EFI_GUID FileGuid;
    EFI_FV_FILETYPE FileType;
    EFI_FV_FILE_ATTRIBUTES Attributes;
    UINTN EfiFileSize;
    PLATFORM_FV2_ADAPTER *Adapter;

    Adapter = (PLATFORM_FV2_ADAPTER *) Context;
    EfiFileSize = 0;
    FileType = EFI_FV_FILETYPE_ALL;
    Status = Adapter->FirmwareVolume->GetNextFile (
                                     Adapter->FirmwareVolume,
                                     Key,
                                     &FileType,
                                     &FileGuid,
                                     &Attributes,
                                     &EfiFileSize
                                     );
    if (Status == EFI_SUCCESS) {
        CopyMem (FileName, &FileGuid, sizeof (FileGuid));
        *FileSize = EfiFileSize;
    }

    return MapEfiStatusToPlatformIoStatus (Status);
}

STATIC
aptio4_platform_io_status_t
Fv2ReadSection (
    IN VOID *Context,
    IN CONST UINT8 FileName[16],
    IN aptio4_platform_section_t Section,
    OUT VOID **Buffer,
    OUT size_t *BufferSize,
    OUT UINT32 *AuthenticationStatus
    )
{
    EFI_STATUS Status;
    EFI_GUID FileGuid;
    EFI_SECTION_TYPE SectionType;
    UINTN EfiBufferSize;
    VOID *SectionBuffer;
    PLATFORM_FV2_ADAPTER *Adapter;

    if (Section == APTIO4_PLATFORM_SECTION_USER_INTERFACE) {
        SectionType = EFI_SECTION_USER_INTERFACE;
    } else if (Section == APTIO4_PLATFORM_SECTION_PE32) {
        SectionType = EFI_SECTION_PE32;
    } else {
        return APTIO4_PLATFORM_IO_INVALID_PARAMETER;
    }

    Adapter = (PLATFORM_FV2_ADAPTER *) Context;
    CopyMem (&FileGuid, FileName, sizeof (FileGuid));
    EfiBufferSize = 0;
    SectionBuffer = NULL;
    Status = Adapter->FirmwareVolume->ReadSection (
                                     Adapter->FirmwareVolume,
                                     &FileGuid,
                                     SectionType,
                                     0,
                                     &SectionBuffer,
                                     &EfiBufferSize,
                                     AuthenticationStatus
                                     );
    *Buffer = SectionBuffer;
    *BufferSize = EfiBufferSize;
    return MapEfiStatusToPlatformIoStatus (Status);
}

STATIC
VOID *
Fv2AllocateZero (
    IN VOID *Context,
    IN size_t Size
    )
{
    (VOID) Context;
    return AllocateZeroPool (Size);
}

STATIC
VOID
Fv2Release (
    IN VOID *Context,
    IN VOID *Buffer
    )
{
    (VOID) Context;
    FreePool (Buffer);
}

STATIC
EFI_STATUS
ReadNamedPe32ModuleFromVolume (
    IN EFI_FIRMWARE_VOLUME2_PROTOCOL *FirmwareVolume,
    IN CONST CHAR16 *ExpectedName,
    OUT VOID **ModuleBuffer,
    OUT UINTN *ModuleSize
    )
{
    PLATFORM_FV2_ADAPTER Adapter;
    aptio4_platform_fv_services_t Services;
    aptio4_platform_io_status_t Status;
    UINT8 *Buffer;
    size_t Size;

    if (FirmwareVolume == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    Adapter.FirmwareVolume = FirmwareVolume;
    Services.context = &Adapter;
    Services.key_size = FirmwareVolume->KeySize;
    Services.get_next_file = Fv2GetNextFile;
    Services.read_section = Fv2ReadSection;
    Services.allocate_zero = Fv2AllocateZero;
    Services.release = Fv2Release;

    Buffer = NULL;
    Size = 0;
    Status = ReadNamedPe32Module (
                 &Services,
                 (const uint16_t *) ExpectedName,
                 &Buffer,
                 &Size
                 );
    *ModuleBuffer = Buffer;
    *ModuleSize = Size;
    return MapPlatformIoStatusToEfiStatus (Status);
}

STATIC
EFI_STATUS
ReadTouudBase (
    OUT UINT64 *TouudBase
    )
{
    EFI_STATUS Status;
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *RootBridgeIo;
    UINT32 Low;
    UINT32 High;

    if (TouudBase == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    *TouudBase = 0;
    Status = gBS->LocateProtocol (
                    &gEfiPciRootBridgeIoProtocolGuid,
                    NULL,
                    (VOID **) &RootBridgeIo
                    );
    if (EFI_ERROR (Status)) {
        return Status;
    }

    Low = 0;
    High = 0;
    Status = RootBridgeIo->Pci.Read (
                             RootBridgeIo,
                             EfiPciWidthUint32,
                             0xA8,
                             1,
                             &Low
                             );
    if (EFI_ERROR (Status)) {
        return Status;
    }

    Status = RootBridgeIo->Pci.Read (
                             RootBridgeIo,
                             EfiPciWidthUint32,
                             0xAC,
                             1,
                             &High
                             );
    if (EFI_ERROR (Status)) {
        return Status;
    }

    *TouudBase = LShiftU64 (High, 32) | Low;
    return (*TouudBase == MAX_UINT64) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GetFirmwareModules (
    IN APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL *This,
    OUT firmware_module_t *PciHostBridge,
    OUT firmware_module_t *NbDxe,
    OUT UINT8 **Gen4PatchBuffer,
    OUT size_t *Gen4PatchBufferSize,
    OUT size_t *Gen4PatchOffset,
    OUT UINT64 *TouudBase
    )
{
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    EFI_FIRMWARE_VOLUME2_PROTOCOL *FirmwareVolume;

    (VOID) This;
    if ((PciHostBridge == NULL) || (NbDxe == NULL) ||
        (Gen4PatchBuffer == NULL) || (Gen4PatchBufferSize == NULL) ||
        (Gen4PatchOffset == NULL) || (TouudBase == NULL)) {
        return EFI_INVALID_PARAMETER;
    }

    PciHostBridge->data = NULL;
    PciHostBridge->size = 0;
    NbDxe->data = NULL;
    NbDxe->size = 0;
    *Gen4PatchBuffer = NULL;
    *Gen4PatchBufferSize = 0;
    *Gen4PatchOffset = 0;
    *TouudBase = 0;

    if ((mContext.PciHostBridgeBuffer == NULL) ||
        (mContext.NbDxeBuffer == NULL)) {
        Status = gBS->HandleProtocol (
                        mProviderImageHandle,
                        &gEfiLoadedImageProtocolGuid,
                        (VOID **) &LoadedImage
                        );
        if (EFI_ERROR (Status) || (LoadedImage->DeviceHandle == NULL)) {
            return EFI_NOT_FOUND;
        }

        Status = gBS->HandleProtocol (
                        LoadedImage->DeviceHandle,
                        &gEfiFirmwareVolume2ProtocolGuid,
                        (VOID **) &FirmwareVolume
                        );
        if (EFI_ERROR (Status)) {
            return Status;
        }

        Status = ReadNamedPe32ModuleFromVolume (
                     FirmwareVolume,
                     L"PciHostBridge",
                     &mContext.PciHostBridgeBuffer,
                     &mContext.PciHostBridgeSize
                     );
        if (EFI_ERROR (Status)) {
            return Status;
        }

        Status = ReadNamedPe32ModuleFromVolume (
                     FirmwareVolume,
                     L"NbDxe",
                     &mContext.NbDxeBuffer,
                     &mContext.NbDxeSize
                     );
        if (EFI_ERROR (Status)) {
            FreePool (mContext.PciHostBridgeBuffer);
            mContext.PciHostBridgeBuffer = NULL;
            mContext.PciHostBridgeSize = 0;
            return Status;
        }

        (VOID) ReadTouudBase (&mContext.TouudBase);
    }

    PciHostBridge->data = (const uint8_t *) mContext.PciHostBridgeBuffer;
    PciHostBridge->size = mContext.PciHostBridgeSize;
    NbDxe->data = (const uint8_t *) mContext.NbDxeBuffer;
    NbDxe->size = mContext.NbDxeSize;
    *Gen4PatchBuffer = (UINT8 *) mContext.PciHostBridgeBuffer;
    *Gen4PatchBufferSize = mContext.PciHostBridgeSize;
    *TouudBase = mContext.TouudBase;
    return EFI_SUCCESS;
}

STATIC
action_status_t
LocateVendorPolicy (
    IN VOID *Context,
    IN CONST action_guid_t *ActionGuid,
    OUT action_policy_view_t *PolicyView
    )
{
    EFI_STATUS Status;
    EFI_GUID VendorGuid;
    VOID *VendorInterface;
    VOID *PolicyData;
    UINTN RequiredDataSize;
    UINTN MinimumDeclaredSize;
    UINT8 DeclaredSize;

    (VOID) Context;
    if ((ActionGuid == NULL) || (PolicyView == NULL)) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    ZeroMem (PolicyView, sizeof (*PolicyView));
    CopyMem (&VendorGuid, ActionGuid->bytes, sizeof (VendorGuid));
    Status = gBS->LocateProtocol (
                    &VendorGuid,
                    NULL,
                    &VendorInterface
                    );
    if (EFI_ERROR (Status)) {
        return MapEfiStatusToActionStatus (Status);
    }

    if ((UINTN) VendorInterface > MAX_UINTN - PLATFORM_POLICY_INTERFACE_SIZE) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    PolicyData = NULL;
    CopyMem (
        &PolicyData,
        (CONST UINT8 *) VendorInterface + PLATFORM_POLICY_POINTER_OFFSET,
        sizeof (PolicyData)
        );
    if (PolicyData == NULL) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    if (CompareMem (
            ActionGuid->bytes,
            ACTION_GUID_GEN23_POLICY.bytes,
            sizeof (ActionGuid->bytes)
            ) == 0) {
        RequiredDataSize = GEN23_POLICY_DATA_SIZE;
            MinimumDeclaredSize = 10;
    } else if (CompareMem (
                   ActionGuid->bytes,
                   ACTION_GUID_GEN5_POLICY.bytes,
                   sizeof (ActionGuid->bytes)
                   ) == 0) {
        RequiredDataSize = GEN5_POLICY_DATA_SIZE;
                MinimumDeclaredSize = 9;
    } else {
        return ACTION_STATUS_UNSUPPORTED;
    }

    DeclaredSize = *(CONST UINT8 *) PolicyData;
    if ((UINTN) DeclaredSize < MinimumDeclaredSize) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    PolicyView->declared_size = DeclaredSize;
    PolicyView->policy_data = (uint8_t *) PolicyData;
    PolicyView->policy_data_size = RequiredDataSize;
    return ACTION_STATUS_SUCCESS;
}

STATIC
action_status_t
GetMemorySpaceMapPreflight (
    IN VOID *Context,
    OUT bool *SafeToEnable
    )
{
    EFI_STATUS Status;
    EFI_GCD_MEMORY_SPACE_DESCRIPTOR *Map;
    UINTN DescriptorCount;
    UINTN Index;
    UINT64 WindowBase;

    if (SafeToEnable == NULL) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    *SafeToEnable = FALSE;
    Map = NULL;
    DescriptorCount = 0;
    Status = gDS->GetMemorySpaceMap (&DescriptorCount, &Map);
    if (EFI_ERROR (Status) || (Map == NULL) ||
        (DescriptorCount == 0) ||
        (DescriptorCount > GCD_MAP_MAX_DESCRIPTORS)) {
        if (Map != NULL) {
            FreePool (Map);
        }
        return ACTION_STATUS_UNSAFE;
    }

    WindowBase = mContext.TouudBase < MMIO64_MIN_BASE
                     ? MMIO64_MIN_BASE
                     : mContext.TouudBase;
    if (WindowBase > MAX_UINT64 - GEN23_MMIO64_WINDOW_SIZE) {
        FreePool (Map);
        return ACTION_STATUS_UNSAFE;
    }

    for (Index = 0; Index < DescriptorCount; ++Index) {
        if ((Map[Index].Length == 0) ||
            (Map[Index].BaseAddress > MAX_UINT64 - Map[Index].Length) ||
            RangesOverlap (
                Map[Index].BaseAddress,
                Map[Index].Length,
                WindowBase,
                GEN23_MMIO64_WINDOW_SIZE
                )) {
            FreePool (Map);
            return ACTION_STATUS_UNSAFE;
        }
    }

    FreePool (Map);
    (VOID) Context;

    // The map cannot prove that later PCI allocations will avoid the vendor hang path.
    return ACTION_STATUS_UNSAFE;
}

STATIC
action_status_t
AddMemorySpace (
    IN VOID *Context,
    IN uint32_t MemoryType,
    IN uint64_t Base,
    IN uint64_t Length,
    IN uint64_t Attributes
    )
{
    (VOID) Context;
    if (MemoryType != EfiGcdMemoryTypeMemoryMappedIo) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    return MapEfiStatusToActionStatus (
               gDS->AddMemorySpace (
                      EfiGcdMemoryTypeMemoryMappedIo,
                      Base,
                      Length,
                      Attributes
                      )
               );
}

STATIC
action_status_t
SetMemorySpaceAttributes (
    IN VOID *Context,
    IN uint64_t Base,
    IN uint64_t Length,
    IN uint64_t Attributes
    )
{
    (VOID) Context;
    return MapEfiStatusToActionStatus (
               gDS->SetMemorySpaceAttributes (Base, Length, Attributes)
               );
}

STATIC
action_status_t
RemoveMemorySpace (
    IN VOID *Context,
    IN uint32_t MemoryType,
    IN uint64_t Base,
    IN uint64_t Length
    )
{
    (VOID) Context;
    if (MemoryType != EfiGcdMemoryTypeMemoryMappedIo) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    return MapEfiStatusToActionStatus (
             gDS->RemoveMemorySpace (Base, Length)
               );
}

STATIC
action_status_t
OriginalGetAllocAttributesAdapter (
    IN VOID *Context,
    OUT uint64_t *Attributes
    )
{
    EFI_STATUS Status;
    UINT64 UefiAttributes;

    (VOID) Context;
    if ((mOriginalGetAllocAttributes == NULL) ||
        (mHostBridgeAllocation == NULL) ||
        (mCurrentRootBridgeHandle == NULL) ||
        (Attributes == NULL)) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    Status = mOriginalGetAllocAttributes (
                 mHostBridgeAllocation,
                 mCurrentRootBridgeHandle,
                 &UefiAttributes
                 );
    if (EFI_ERROR (Status)) {
        return MapEfiStatusToActionStatus (Status);
    }

    *Attributes = (uint64_t) UefiAttributes;
    return ACTION_STATUS_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GetAllocAttributesThunk (
    IN EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *This,
    IN EFI_HANDLE RootBridgeHandle,
    OUT UINT64 *Attributes
    )
{
    EFI_HANDLE PreviousRootBridgeHandle;
    action_status_t ActionStatus;
    uint64_t ActionAttributes;

    if ((This == NULL) || (Attributes == NULL) ||
        (mInstalledAttributesHook == NULL) ||
        (mInstalledHookContext == NULL)) {
        return EFI_INVALID_PARAMETER;
    }

    PreviousRootBridgeHandle = mCurrentRootBridgeHandle;
    mCurrentRootBridgeHandle = RootBridgeHandle;
    ActionAttributes = (uint64_t) *Attributes;
    ActionStatus = mInstalledAttributesHook (
                       mInstalledHookContext,
                       &ActionAttributes
                       );
    mCurrentRootBridgeHandle = PreviousRootBridgeHandle;
    if (ActionStatus == ACTION_STATUS_SUCCESS) {
        *Attributes = (UINT64) ActionAttributes;
    }

    return MapActionStatusToEfiStatus (ActionStatus);
}

STATIC
aptio4_platform_function_t
ReadGetAllocAttributesMethod (
    IN VOID *Context
    )
{
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *Protocol;

    Protocol = (EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *) Context;
    return (aptio4_platform_function_t) Protocol->GetAllocAttributes;
}

STATIC
VOID
WriteGetAllocAttributesMethod (
    IN VOID *Context,
    IN aptio4_platform_function_t Function
    )
{
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *Protocol;

    Protocol = (EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *) Context;
    Protocol->GetAllocAttributes = (PCI_HB_GET_ALLOC_ATTRIBUTES) Function;
}

STATIC
action_status_t
InstallGetAllocAttributesHook (
    IN VOID *Context,
    IN gen4_get_alloc_attributes_fn Hook,
    IN gen4_alloc_hook_context_t *HookContext
    )
{
    EFI_STATUS Status;
    aptio4_platform_function_t OriginalMethod;
    aptio4_platform_hook_status_t HookStatus;

    (VOID) Context;
    if ((Hook == NULL) || (HookContext == NULL)) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    Status = gBS->LocateProtocol (
                    &gEfiPciHostBridgeResourceAllocationProtocolGuid,
                    NULL,
                    (VOID **) &mHostBridgeAllocation
                    );
    if (EFI_ERROR (Status)) {
        mHostBridgeAllocation = NULL;
        return MapEfiStatusToActionStatus (Status);
    }

    OriginalMethod = NULL;
    HookStatus = InstallHookOnce (
                     mHostBridgeAllocation,
                     ReadGetAllocAttributesMethod,
                     WriteGetAllocAttributesMethod,
                     (aptio4_platform_function_t) GetAllocAttributesThunk,
                     &OriginalMethod
                     );
    if (HookStatus == APTIO4_PLATFORM_HOOK_INVALID_PARAMETER) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }
    if (HookStatus != APTIO4_PLATFORM_HOOK_INSTALLED) {
        return ACTION_STATUS_UNSUPPORTED;
    }

    mOriginalGetAllocAttributes =
        (PCI_HB_GET_ALLOC_ATTRIBUTES) OriginalMethod;
    HookContext->original = OriginalGetAllocAttributesAdapter;
    HookContext->original_context = NULL;
    mInstalledAttributesHook = Hook;
    mInstalledHookContext = HookContext;
    return ACTION_STATUS_SUCCESS;
}

STATIC
VOID
InitializePlatformServices (
    VOID
    )
{
    mPlatformServices.Revision = APTIO4_ABOVE4G_PLATFORM_SERVICES_REVISION;
    mPlatformServices.GetFirmwareModules = GetFirmwareModules;

    mPlatformServices.Gen23Services.context = &mContext;
    mPlatformServices.Gen23Services.locate_protocol = LocateVendorPolicy;
    mPlatformServices.Gen23Services.get_memory_space_map =
        GetMemorySpaceMapPreflight;

    mPlatformServices.Gen5Services.context = &mContext;
    mPlatformServices.Gen5Services.locate_protocol = LocateVendorPolicy;

    mPlatformServices.Gen4Services.context = &mContext;
    mPlatformServices.Gen4Services.patch_notify_phase =
        gen4_notify_phase_patch_placeholder;
    mPlatformServices.Gen4Services.add_memory_space = AddMemorySpace;
    mPlatformServices.Gen4Services.set_memory_space_attributes =
        SetMemorySpaceAttributes;
    mPlatformServices.Gen4Services.remove_memory_space = RemoveMemorySpace;
    mPlatformServices.Gen4Services.install_get_alloc_attributes_hook =
        InstallGetAllocAttributesHook;
}

STATIC
EFI_STATUS
InstallPlatformServices (
    VOID
    )
{
    if (mProviderProtocolHandle != NULL) {
        return EFI_ALREADY_STARTED;
    }

    return gBS->InstallMultipleProtocolInterfaces (
                    &mProviderProtocolHandle,
                    &gAptio4Above4GPlatformServicesProtocolGuid,
                    &mPlatformServices,
                    NULL
                    );
}

STATIC
VOID
EFIAPI
OnPolicyProtocolInstalled (
    IN EFI_EVENT Event,
    IN VOID *Context
    )
{
    EFI_STATUS Status;
    VOID *Interface;

    (VOID) Context;
    Status = gBS->LocateProtocol (&mPendingPolicyGuid, NULL, &Interface);
    if (EFI_ERROR (Status)) {
        return;
    }

    if (mPolicyNotifyEvent != NULL) {
        gBS->CloseEvent (mPolicyNotifyEvent);
        mPolicyNotifyEvent = NULL;
    }

    (VOID) InstallPlatformServices ();

    (VOID) Event;
}

STATIC
EFI_STATUS
WaitForPolicyProtocol (
    IN CONST EFI_GUID *PolicyGuid
    )
{
    EFI_STATUS Status;
    VOID *Interface;

    CopyMem (&mPendingPolicyGuid, PolicyGuid, sizeof (mPendingPolicyGuid));
    Status = gBS->CreateEvent (
                    EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    OnPolicyProtocolInstalled,
                    NULL,
                    &mPolicyNotifyEvent
                    );
    if (EFI_ERROR (Status)) {
        return Status;
    }

    Status = gBS->RegisterProtocolNotify (
                    &mPendingPolicyGuid,
                    mPolicyNotifyEvent,
                    &mPolicyRegistration
                    );
    if (EFI_ERROR (Status)) {
        gBS->CloseEvent (mPolicyNotifyEvent);
        mPolicyNotifyEvent = NULL;
        return Status;
    }

    Status = gBS->LocateProtocol (&mPendingPolicyGuid, NULL, &Interface);
    if (!EFI_ERROR (Status)) {
        gBS->SignalEvent (mPolicyNotifyEvent);
    }

    return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Above4GPlatformEntryPoint (
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
    )
{
    EFI_STATUS Status;
    firmware_module_t PciHostBridge;
    firmware_module_t NbDxe;
    UINT8 *PatchBuffer;
    size_t PatchBufferSize;
    size_t PatchOffset;
    UINT64 TouudBase;
    firmware_profile_t Profile;

    (VOID) SystemTable;
    if (!IsFeatureEnabled ()) {
        return EFI_SUCCESS;
    }

    mProviderImageHandle = ImageHandle;
    InitializePlatformServices ();

    PciHostBridge.data = NULL;
    PciHostBridge.size = 0;
    NbDxe.data = NULL;
    NbDxe.size = 0;
    PatchBuffer = NULL;
    PatchBufferSize = 0;
    PatchOffset = 0;
    TouudBase = 0;
    Status = GetFirmwareModules (
                 &mPlatformServices,
                 &PciHostBridge,
                 &NbDxe,
                 &PatchBuffer,
                 &PatchBufferSize,
                 &PatchOffset,
                 &TouudBase
                 );
    if (EFI_ERROR (Status)) {
        return InstallPlatformServices ();
    }

    Profile = detect_firmware_profile (PciHostBridge, NbDxe);
    if (Profile == PROFILE_GEN23) {
        return WaitForPolicyProtocol (
                   (CONST EFI_GUID *) &gAptio4Above4GGen23PolicyProtocolGuid
                   );
    }

    if (Profile == PROFILE_GEN5) {
        return WaitForPolicyProtocol (
                   (CONST EFI_GUID *) &gAptio4Above4GGen5PolicyProtocolGuid
                   );
    }

    return InstallPlatformServices ();
}