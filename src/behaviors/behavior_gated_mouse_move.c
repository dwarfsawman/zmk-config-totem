/*
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_gated_mouse_move

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct gated_mouse_move_state {
    bool active;
    uint32_t param1;
    uint32_t param2;
    struct zmk_behavior_binding_event press_event;
};

struct behavior_gated_mouse_move_data {
    struct gated_mouse_move_state positions[ZMK_KEYMAP_LEN];
};

struct behavior_gated_mouse_move_config {
    struct zmk_behavior_binding movement;
    uint8_t stop_layer;
};

static struct zmk_behavior_binding
movement_binding(const struct behavior_gated_mouse_move_config *config,
                 const struct gated_mouse_move_state *state) {
    struct zmk_behavior_binding binding = config->movement;
    binding.param1 = state->param1;
    binding.param2 = state->param2;
    return binding;
}

static int on_gated_mouse_move_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_gated_mouse_move_config *config = dev->config;
    struct behavior_gated_mouse_move_data *data = dev->data;

    if (event.position >= ARRAY_SIZE(data->positions)) {
        LOG_ERR("Gated mouse move position %u is out of range", event.position);
        return -EINVAL;
    }

    struct gated_mouse_move_state *state = &data->positions[event.position];
    if (state->active) {
        LOG_WRN("Gated mouse move position %u is already active", event.position);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    state->param1 = binding->param1;
    state->param2 = binding->param2;
    state->press_event = event;

    struct zmk_behavior_binding movement = movement_binding(config, state);
    int ret = zmk_behavior_invoke_binding(&movement, event, true);
    if (ret < 0) {
        LOG_ERR("Failed to start gated mouse move at position %u: %d", event.position, ret);
        return ret;
    }

    state->active = true;
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_gated_mouse_move_released(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_gated_mouse_move_config *config = dev->config;
    struct behavior_gated_mouse_move_data *data = dev->data;

    if (event.position >= ARRAY_SIZE(data->positions)) {
        LOG_ERR("Gated mouse move position %u is out of range", event.position);
        return -EINVAL;
    }

    struct gated_mouse_move_state *state = &data->positions[event.position];
    if (!state->active) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    state->active = false;
    struct zmk_behavior_binding movement = movement_binding(config, state);
    int ret = zmk_behavior_invoke_binding(&movement, event, false);
    if (ret < 0) {
        LOG_ERR("Failed to stop gated mouse move at position %u: %d", event.position, ret);
        return ret;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static void stop_active_movements(const struct device *dev,
                                  const struct zmk_layer_state_changed *event) {
    const struct behavior_gated_mouse_move_config *config = dev->config;
    struct behavior_gated_mouse_move_data *data = dev->data;

    if (event->state || event->layer != config->stop_layer) {
        return;
    }

    for (size_t position = 0; position < ARRAY_SIZE(data->positions); position++) {
        struct gated_mouse_move_state *state = &data->positions[position];
        if (!state->active) {
            continue;
        }

        state->active = false;
        struct zmk_behavior_binding movement = movement_binding(config, state);
        struct zmk_behavior_binding_event release_event = state->press_event;
        release_event.timestamp = event->timestamp;

        int ret = zmk_behavior_invoke_binding(&movement, release_event, false);
        if (ret < 0) {
            LOG_ERR("Failed to stop gated mouse move at position %u: %d", position, ret);
        }
    }
}

static int gated_mouse_move_layer_state_changed(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *event = as_zmk_layer_state_changed(eh);
    if (event == NULL || event->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

#define STOP_GATED_MOUSE_MOVE_INSTANCE(n) stop_active_movements(DEVICE_DT_INST_GET(n), event);
    DT_INST_FOREACH_STATUS_OKAY(STOP_GATED_MOUSE_MOVE_INSTANCE)
#undef STOP_GATED_MOUSE_MOVE_INSTANCE

    return ZMK_EV_EVENT_BUBBLE;
}

static const struct behavior_driver_api behavior_gated_mouse_move_driver_api = {
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
    .binding_pressed = on_gated_mouse_move_pressed,
    .binding_released = on_gated_mouse_move_released,
};

#define GATED_MOUSE_MOVE_INST(n)                                                                   \
    static struct behavior_gated_mouse_move_data behavior_gated_mouse_move_data_##n = {};          \
    static const struct behavior_gated_mouse_move_config behavior_gated_mouse_move_config_##n = {  \
        .movement = ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n)),                                 \
        .stop_layer = DT_INST_PROP(n, stop_layer),                                                 \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(                                                                       \
        n, NULL, NULL, &behavior_gated_mouse_move_data_##n, &behavior_gated_mouse_move_config_##n, \
        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_gated_mouse_move_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GATED_MOUSE_MOVE_INST)

ZMK_LISTENER(behavior_gated_mouse_move, gated_mouse_move_layer_state_changed);
ZMK_SUBSCRIPTION(behavior_gated_mouse_move, zmk_layer_state_changed);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
