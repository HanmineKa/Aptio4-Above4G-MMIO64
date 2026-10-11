#ifndef APTIO4_ABOVE4G_DETECT_H
#define APTIO4_ABOVE4G_DETECT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PROFILE_UNKNOWN = 0,
    PROFILE_GEN23,
    PROFILE_GEN5,
    PROFILE_GEN4_CANDIDATE
} firmware_profile_t;

typedef struct {
    const uint8_t *data;
    size_t size;
} firmware_module_t;

firmware_profile_t detect_firmware_profile(
    firmware_module_t pci_host_bridge,
    firmware_module_t nb_dxe
);

#endif