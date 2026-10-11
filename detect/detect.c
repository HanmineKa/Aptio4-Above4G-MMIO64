#include "detect.h"

static const uint8_t aptio_host_bridge_signature[] = {
    0x65, 0x68, 0x73, 0x74
};

static const uint8_t gen23_policy_guid[] = {
    0xD7, 0x33, 0x23, 0xCD, 0x0A, 0x6A, 0x76, 0x4C,
    0x83, 0x50, 0x24, 0x0A, 0xDA, 0x36, 0xA2, 0xC7
};

static const uint8_t gen5_policy_guid[] = {
    0x13, 0x29, 0x2A, 0x91, 0xA8, 0x42, 0xB0, 0x45,
    0x82, 0x2F, 0xA9, 0x4D, 0x1E, 0xAE, 0x99, 0x65
};

static int bytes_equal(const uint8_t *left, const uint8_t *right, size_t size);

static int contains_bytes(
    const uint8_t *buffer,
    size_t buffer_size,
    const uint8_t *signature,
    size_t signature_size
)
{
    size_t offset;

    if (buffer_size < signature_size) {
        return 0;
    }

    for (offset = 0; offset <= buffer_size - signature_size; ++offset) {
        if (bytes_equal(buffer + offset, signature, signature_size)) {
            return 1;
        }
    }

    return 0;
}

static int bytes_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }

    return 1;
}

static int module_is_valid(firmware_module_t module)
{
    return module.data != NULL && module.size > 0;
}

static int module_contains(
    firmware_module_t module,
    const uint8_t *signature,
    size_t signature_size
)
{
    return contains_bytes(module.data, module.size, signature, signature_size);
}

firmware_profile_t detect_firmware_profile(
    firmware_module_t pci_host_bridge,
    firmware_module_t nb_dxe
)
{
    int has_gen23_guid;
    int has_gen5_guid;
    int is_aptio_host_bridge;

    if (!module_is_valid(pci_host_bridge) || !module_is_valid(nb_dxe)) {
        return PROFILE_UNKNOWN;
    }

    is_aptio_host_bridge = module_contains(
        pci_host_bridge,
        aptio_host_bridge_signature,
        sizeof(aptio_host_bridge_signature)
    );
    has_gen23_guid =
        module_contains(pci_host_bridge, gen23_policy_guid, sizeof(gen23_policy_guid)) ||
        module_contains(nb_dxe, gen23_policy_guid, sizeof(gen23_policy_guid));
    has_gen5_guid =
        module_contains(pci_host_bridge, gen5_policy_guid, sizeof(gen5_policy_guid)) ||
        module_contains(nb_dxe, gen5_policy_guid, sizeof(gen5_policy_guid));

    if (!is_aptio_host_bridge || (has_gen23_guid && has_gen5_guid)) {
        return PROFILE_UNKNOWN;
    }

    if (has_gen23_guid) {
        return PROFILE_GEN23;
    }

    if (has_gen5_guid) {
        return PROFILE_GEN5;
    }

    return PROFILE_GEN4_CANDIDATE;
}