#ifndef APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL_H
#define APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL_H

#include <Uefi.h>

#include "../detect/detect.h"
#include "actions/action_api.h"

#define APTIO4_ABOVE4G_PLATFORM_SERVICES_REVISION 1U

typedef struct _APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL
    APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL;

typedef EFI_STATUS (EFIAPI *APTIO4_ABOVE4G_GET_FIRMWARE_MODULES)(
    IN APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL *This,
    OUT firmware_module_t *PciHostBridge,
    OUT firmware_module_t *NbDxe,
    OUT UINT8 **Gen4PatchBuffer,
    OUT size_t *Gen4PatchBufferSize,
    OUT size_t *Gen4PatchOffset,
    OUT UINT64 *TouudBase
);

struct _APTIO4_ABOVE4G_PLATFORM_SERVICES_PROTOCOL {
    UINT32 Revision;
    APTIO4_ABOVE4G_GET_FIRMWARE_MODULES GetFirmwareModules;
    gen23_action_services_t Gen23Services;
    gen5_action_services_t Gen5Services;
    gen4_action_services_t Gen4Services;
};

#endif