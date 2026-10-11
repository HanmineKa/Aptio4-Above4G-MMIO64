#include "action_api.h"

const action_guid_t ACTION_GUID_GEN23_POLICY = {
    { 0xD7, 0x33, 0x23, 0xCD, 0x0A, 0x6A, 0x76, 0x4C,
      0x83, 0x50, 0x24, 0x0A, 0xDA, 0x36, 0xA2, 0xC7 }
};

action_status_t gen23_apply(const gen23_action_services_t *services)
{
    action_policy_view_t policy_view = { 0, NULL, 0 };
    action_status_t status;
    bool safe_to_enable = false;

    if (services == NULL || services->locate_protocol == NULL ||
        services->get_memory_space_map == NULL) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    status = services->locate_protocol(
        services->context,
        &ACTION_GUID_GEN23_POLICY,
        &policy_view
    );
    if (status != ACTION_STATUS_SUCCESS) {
        return status;
    }

    if (policy_view.declared_size < 10 || policy_view.policy_data == NULL ||
        policy_view.policy_data_size <= 0x11) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    status = services->get_memory_space_map(
        services->context,
        &safe_to_enable
    );
    if (status != ACTION_STATUS_SUCCESS) {
        return status;
    }
    if (!safe_to_enable) {
        return ACTION_STATUS_UNSAFE;
    }

    policy_view.policy_data[0x11] = 1;
    return ACTION_STATUS_SUCCESS;
}