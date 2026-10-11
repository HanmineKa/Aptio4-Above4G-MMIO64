#ifndef APTIO4_ABOVE4G_ACTION_API_H
#define APTIO4_ABOVE4G_ACTION_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ACTION_STATUS_SUCCESS = 0,
    ACTION_STATUS_INVALID_PARAMETER,
    ACTION_STATUS_NOT_FOUND,
    ACTION_STATUS_UNSAFE,
    ACTION_STATUS_UNSUPPORTED,
    ACTION_STATUS_OUT_OF_RESOURCES,
    ACTION_STATUS_DEVICE_ERROR,
    ACTION_STATUS_ROLLBACK_FAILED
} action_status_t;

typedef struct {
    uint8_t bytes[16];
} action_guid_t;

typedef struct {
    size_t declared_size;
    uint8_t *policy_data;
    size_t policy_data_size;
} action_policy_view_t;

typedef action_status_t (*action_locate_protocol_fn)(
    void *context,
    const action_guid_t *guid,
    action_policy_view_t *policy_view
);

typedef struct {
    void *context;
    action_locate_protocol_fn locate_protocol;
    action_status_t (*get_memory_space_map)(void *context, bool *safe_to_enable);
} gen23_action_services_t;

typedef struct {
    void *context;
    action_locate_protocol_fn locate_protocol;
} gen5_action_services_t;

typedef action_status_t (*gen4_get_alloc_attributes_fn)(
    void *context,
    uint64_t *attributes
);

typedef struct {
    gen4_get_alloc_attributes_fn original;
    void *original_context;
} gen4_alloc_hook_context_t;

typedef struct {
    void *context;
    action_status_t (*patch_notify_phase)(
        void *context,
        uint8_t *module_buffer,
        size_t module_size,
        size_t patch_offset
    );
    action_status_t (*add_memory_space)(
        void *context,
        uint32_t memory_type,
        uint64_t base,
        uint64_t length,
        uint64_t attributes
    );
    action_status_t (*set_memory_space_attributes)(
        void *context,
        uint64_t base,
        uint64_t length,
        uint64_t attributes
    );
    action_status_t (*remove_memory_space)(
        void *context,
        uint32_t memory_type,
        uint64_t base,
        uint64_t length
    );
    action_status_t (*install_get_alloc_attributes_hook)(
        void *context,
        gen4_get_alloc_attributes_fn hook,
        gen4_alloc_hook_context_t *hook_context
    );
} gen4_action_services_t;

extern const action_guid_t ACTION_GUID_GEN23_POLICY;
extern const action_guid_t ACTION_GUID_GEN5_POLICY;

action_status_t gen23_apply(const gen23_action_services_t *services);
action_status_t gen5_apply(const gen5_action_services_t *services);

action_status_t gen4_get_alloc_attributes_hook(
    void *context,
    uint64_t *attributes
);
action_status_t gen4_notify_phase_patch_placeholder(
    void *context,
    uint8_t *module_buffer,
    size_t module_size,
    size_t patch_offset
);
action_status_t gen4_apply(
    const gen4_action_services_t *services,
    uint64_t touud_base,
    uint8_t *module_buffer,
    size_t module_size,
    size_t patch_offset,
    gen4_alloc_hook_context_t *hook_context
);

#endif