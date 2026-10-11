#include "action_api.h"

const action_guid_t ACTION_GUID_GEN5_POLICY = {
    { 0x13, 0x29, 0x2A, 0x91, 0xA8, 0x42, 0xB0, 0x45,
      0x82, 0x2F, 0xA9, 0x4D, 0x1E, 0xAE, 0x99, 0x65 }
};

action_status_t gen5_apply(const gen5_action_services_t *services)
{
    action_policy_view_t policy_view = { 0, NULL, 0 };
    action_status_t status;

    if (services == NULL || services->locate_protocol == NULL) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    status = services->locate_protocol(
        services->context,
        &ACTION_GUID_GEN5_POLICY,
        &policy_view
    );
    if (status != ACTION_STATUS_SUCCESS) {
        return status;
    }

    if (policy_view.declared_size < 9 || policy_view.policy_data == NULL ||
        policy_view.policy_data_size <= 0x20) {
        return ACTION_STATUS_INVALID_PARAMETER;
    }

    policy_view.policy_data[0x20] = 1;
    return ACTION_STATUS_SUCCESS;
}