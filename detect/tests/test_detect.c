#include "../detect.h"

#include <stdio.h>
#include <string.h>

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

static int expect_profile(
    const char *test_name,
    firmware_profile_t expected,
    firmware_profile_t actual
)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %d, got %d)\n", test_name, expected, actual);
        return 0;
    }

    return 1;
}

static int test_gen23_guid_in_host_bridge(void)
{
    uint8_t host_data[64] = { 0 };
    uint8_t nb_data[16] = { 0 };
    firmware_module_t host = { host_data, sizeof(host_data) };
    firmware_module_t nb = { nb_data, sizeof(nb_data) };

    memcpy(host_data, aptio_host_bridge_signature, sizeof(aptio_host_bridge_signature));
    memcpy(host_data + 20, gen23_policy_guid, sizeof(gen23_policy_guid));

    return expect_profile(
        "Gen 2-3 GUID in PciHostBridge",
        PROFILE_GEN23,
        detect_firmware_profile(host, nb)
    );
}

static int test_gen5_guid_in_nb_dxe(void)
{
    uint8_t host_data[64] = { 0 };
    uint8_t nb_data[64] = { 0 };
    firmware_module_t host = { host_data, sizeof(host_data) };
    firmware_module_t nb = { nb_data, sizeof(nb_data) };

    memcpy(host_data, aptio_host_bridge_signature, sizeof(aptio_host_bridge_signature));
    memcpy(nb_data + 24, gen5_policy_guid, sizeof(gen5_policy_guid));

    return expect_profile(
        "Gen 5 GUID in NbDxe",
        PROFILE_GEN5,
        detect_firmware_profile(host, nb)
    );
}

static int test_gen4_candidate_requires_further_verification(void)
{
    uint8_t host_data[32] = { 0 };
    uint8_t nb_data[16] = { 0 };
    firmware_module_t host = { host_data, sizeof(host_data) };
    firmware_module_t nb = { nb_data, sizeof(nb_data) };

    memcpy(host_data, aptio_host_bridge_signature, sizeof(aptio_host_bridge_signature));

    return expect_profile(
        "Aptio host bridge without known policy GUIDs",
        PROFILE_GEN4_CANDIDATE,
        detect_firmware_profile(host, nb)
    );
}

static int test_conflicting_policy_guids_are_unknown(void)
{
    uint8_t host_data[64] = { 0 };
    uint8_t nb_data[64] = { 0 };
    firmware_module_t host = { host_data, sizeof(host_data) };
    firmware_module_t nb = { nb_data, sizeof(nb_data) };

    memcpy(host_data, aptio_host_bridge_signature, sizeof(aptio_host_bridge_signature));
    memcpy(host_data + 16, gen23_policy_guid, sizeof(gen23_policy_guid));
    memcpy(nb_data + 24, gen5_policy_guid, sizeof(gen5_policy_guid));

    return expect_profile(
        "Conflicting known policy GUIDs",
        PROFILE_UNKNOWN,
        detect_firmware_profile(host, nb)
    );
}

static int test_unrecognized_buffer_is_unknown(void)
{
    uint8_t host_data[] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
    };
    uint8_t nb_data[] = { 0x10, 0x20, 0x30, 0x40 };
    firmware_module_t host = { host_data, sizeof(host_data) };
    firmware_module_t nb = { nb_data, sizeof(nb_data) };

    return expect_profile(
        "Unrecognized synthetic buffers",
        PROFILE_UNKNOWN,
        detect_firmware_profile(host, nb)
    );
}

static int test_incomplete_module_input_is_unknown(void)
{
    uint8_t host_data[] = { 0x65, 0x68, 0x73, 0x74 };
    firmware_module_t host = { host_data, sizeof(host_data) };
    firmware_module_t missing_nb = { NULL, 0 };

    return expect_profile(
        "Missing NbDxe buffer",
        PROFILE_UNKNOWN,
        detect_firmware_profile(host, missing_nb)
    );
}

static int test_guid_without_aptio_marker_is_unknown(void)
{
    uint8_t host_data[32] = { 0 };
    uint8_t nb_data[32] = { 0 };
    firmware_module_t host = { host_data, sizeof(host_data) };
    firmware_module_t nb = { nb_data, sizeof(nb_data) };

    memcpy(host_data, gen23_policy_guid, sizeof(gen23_policy_guid));

    return expect_profile(
        "Known GUID without Aptio host bridge marker",
        PROFILE_UNKNOWN,
        detect_firmware_profile(host, nb)
    );
}

int main(void)
{
    int passed = 0;
    int failed = 0;

#define RUN_TEST(test_function) \
    do { \
        if (test_function()) { \
            ++passed; \
        } else { \
            ++failed; \
        } \
    } while (0)

    RUN_TEST(test_gen23_guid_in_host_bridge);
    RUN_TEST(test_gen5_guid_in_nb_dxe);
    RUN_TEST(test_gen4_candidate_requires_further_verification);
    RUN_TEST(test_conflicting_policy_guids_are_unknown);
    RUN_TEST(test_unrecognized_buffer_is_unknown);
    RUN_TEST(test_incomplete_module_input_is_unknown);
    RUN_TEST(test_guid_without_aptio_marker_is_unknown);

#undef RUN_TEST

    if (failed != 0) {
        fprintf(stderr, "%d test(s) failed; %d passed.\n", failed, passed);
        return 1;
    }

    printf("All %d detector tests passed.\n", passed);
    return 0;
}