#include "codex_micro_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oled_icons.h"
#include "kb16_config.h"
#include "kb16_config_transport.h"
#include "raw_hid.h"

#define CODEX_MICRO_CHANNEL_RPC 2
#define CODEX_MICRO_RX_BUFFER_SIZE 1024
#define CODEX_MICRO_OBJECT_SIZE 256
#define CODEX_MICRO_BREATH_MS 1600
#define CODEX_MICRO_COOL_WHITE_RED 215
#define CODEX_MICRO_COOL_WHITE_GREEN 211
#define CODEX_MICRO_COOL_WHITE_BLUE 255
#define CODEX_MICRO_STATUS_UNREAD_COLOR 0x00FF4CU
#define CODEX_MICRO_STATUS_WORKING_COLOR 0x304FFEU
#define CODEX_MICRO_STATUS_ATTENTION_COLOR 0xFF6D00U
#define CODEX_MICRO_STATUS_ERROR_COLOR 0xFF0033U
#define CODEX_MICRO_CUSTOM_UNREAD_COLOR 0x1FAB21U
#define CODEX_MICRO_CUSTOM_WORKING_COLOR 0x1A48ABU
#define CODEX_MICRO_CUSTOM_ATTENTION_COLOR 0xC76700U
#define CODEX_MICRO_CUSTOM_ERROR_COLOR 0xC7144DU
#define CODEX_MICRO_ALL_HOST_SLOTS_MASK ((1U << CODEX_MICRO_HOST_SLOT_COUNT) - 1U)

typedef enum {
    CODEX_MICRO_EFFECT_OFF = 0,
    CODEX_MICRO_EFFECT_STEADY,
    CODEX_MICRO_EFFECT_BREATH,
} codex_micro_effect_t;

typedef struct {
    uint32_t             color;
    uint8_t              brightness;
    codex_micro_effect_t effect;
    bool                 assigned;
} codex_micro_light_t;

static codex_micro_light_t thread_lights[CODEX_MICRO_HOST_SLOT_COUNT];
static char                rpc_buffer[CODEX_MICRO_RX_BUFFER_SIZE];
static size_t              rpc_length;
static bool                host_seen;
static uint8_t             sent_press_mask;
static uint8_t             sent_action_mask;
static bool                sent_encoder_press;
static uint8_t             joystick_held_mask;
static uint8_t             joystick_press_order[CODEX_MICRO_JOYSTICK_DIRECTION_COUNT];
static uint8_t             joystick_press_count;
static uint8_t             global_brightness;
static bool                lighting_off;

static const char   *const action_key_names[CODEX_MICRO_ACTION_COUNT] = {"ACT06", "ACT07", "ACT08", "ACT09", "ACT12", "ACT10"};
static const char   *const joystick_angles[CODEX_MICRO_JOYSTICK_DIRECTION_COUNT] = {"0.75", "0.00", "0.25", "0.50"};
static const uint8_t agent_native_controls[CODEX_MICRO_TASK_KEY_COUNT] = {
    KB16_NATIVE_AG00, KB16_NATIVE_AG01, KB16_NATIVE_AG02,
    KB16_NATIVE_AG03, KB16_NATIVE_AG04, KB16_NATIVE_AG05,
};

static void send_json(const char *json) {
    size_t json_length = strlen(json);
    size_t total       = json_length + 1;
    size_t offset      = 0;

    while (offset < total) {
        uint8_t report[CODEX_MICRO_REPORT_SIZE] = {0};
        size_t  chunk = total - offset;
        if (chunk > CODEX_MICRO_FRAGMENT_SIZE) {
            chunk = CODEX_MICRO_FRAGMENT_SIZE;
        }

        report[0] = CODEX_MICRO_REPORT_ID;
        report[1] = CODEX_MICRO_CHANNEL_RPC;
        report[2] = (uint8_t)chunk;
        for (size_t index = 0; index < chunk; ++index) {
            size_t source = offset + index;
            report[3 + index] = source < json_length ? (uint8_t)json[source] : (uint8_t)'\n';
        }

        raw_hid_send(report, sizeof(report));
        offset += chunk;
        if (offset < total) {
            wait_ms(4);
        }
    }
}

static void show_no_link(void) {
    oled_controller_show_popup("NO LINK", "CODEX MICRO", OLED_POPUP_ERROR_MS);
}

static void send_hid_key_event(const char *key, uint8_t action) {
    char message[128];
    int  length = snprintf(message, sizeof(message), "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\",\"act\":%u}}", key, action);
    if (length > 0 && (size_t)length < sizeof(message)) {
        send_json(message);
    }
}

static void send_joystick_position(codex_micro_joystick_direction_t direction, bool active) {
    const char *angle = direction < CODEX_MICRO_JOYSTICK_DIRECTION_COUNT ? joystick_angles[direction] : "0.00";
    char        message[128];
    int         length = snprintf(message, sizeof(message), "{\"method\":\"v.oai.rad\",\"params\":{\"a\":%s,\"d\":%u}}", angle, active ? 1U : 0U);
    if (length > 0 && (size_t)length < sizeof(message)) {
        send_json(message);
    }
}

static const char *find_top_level_value(const char *json, const char *key) {
    size_t key_length = strlen(key);
    int    depth      = 0;

    for (const char *cursor = json; *cursor != '\0'; ++cursor) {
        if (*cursor == '{') {
            ++depth;
            continue;
        }
        if (*cursor == '}') {
            --depth;
            continue;
        }
        if (*cursor != '"') {
            continue;
        }

        const char *start   = cursor + 1;
        const char *closing = start;
        bool        escaped = false;
        while (*closing != '\0') {
            if (!escaped && *closing == '"') {
                break;
            }
            if (!escaped && *closing == '\\') {
                escaped = true;
            } else {
                escaped = false;
            }
            ++closing;
        }
        if (*closing == '\0') {
            return NULL;
        }

        if (depth == 1 && (size_t)(closing - start) == key_length && memcmp(start, key, key_length) == 0) {
            const char *value = closing + 1;
            while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
                ++value;
            }
            if (*value != ':') {
                return NULL;
            }
            ++value;
            while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
                ++value;
            }
            return value;
        }
        cursor = closing;
    }
    return NULL;
}

static bool copy_json_string(const char *value, char *target, size_t target_size) {
    if (value == NULL || *value != '"' || target_size == 0) {
        return false;
    }

    size_t output  = 0;
    bool   escaped = false;
    for (const char *cursor = value + 1; *cursor != '\0'; ++cursor) {
        if (!escaped && *cursor == '"') {
            target[output] = '\0';
            return true;
        }
        if (output + 1 >= target_size) {
            return false;
        }
        if (!escaped && *cursor == '\\') {
            escaped = true;
            continue;
        }
        target[output++] = *cursor;
        escaped          = false;
    }
    return false;
}

static bool copy_json_token(const char *value, char *target, size_t target_size) {
    if (value == NULL || target_size == 0) {
        return false;
    }

    const char *end = value;
    if (*value == '"') {
        bool escaped = false;
        ++end;
        while (*end != '\0') {
            if (!escaped && *end == '"') {
                ++end;
                break;
            }
            if (!escaped && *end == '\\') {
                escaped = true;
            } else {
                escaped = false;
            }
            ++end;
        }
    } else {
        while (*end != '\0' && *end != ',' && *end != '}' && *end != '\r' && *end != '\n') {
            ++end;
        }
        while (end > value && (end[-1] == ' ' || end[-1] == '\t')) {
            --end;
        }
    }

    size_t length = (size_t)(end - value);
    if (length == 0 || length >= target_size) {
        return false;
    }
    memcpy(target, value, length);
    target[length] = '\0';
    return true;
}

static bool parse_u32(const char *value, uint32_t *result) {
    if (value == NULL || result == NULL || *value < '0' || *value > '9') {
        return false;
    }

    uint32_t parsed = 0;
    while (*value >= '0' && *value <= '9') {
        parsed = parsed * 10U + (uint32_t)(*value - '0');
        ++value;
    }
    *result = parsed;
    return true;
}

static bool parse_brightness(const char *value, uint8_t *result) {
    if (value == NULL || result == NULL || *value < '0' || *value > '9') {
        return false;
    }

    uint32_t whole = 0;
    while (*value >= '0' && *value <= '9') {
        whole = whole * 10U + (uint32_t)(*value - '0');
        ++value;
    }

    if (whole >= 1U) {
        *result = 255;
        return true;
    }

    uint32_t fraction = 0;
    uint32_t scale    = 1;
    if (*value == '.') {
        ++value;
        for (uint8_t digits = 0; digits < 4 && *value >= '0' && *value <= '9'; ++digits, ++value) {
            fraction = fraction * 10U + (uint32_t)(*value - '0');
            scale *= 10U;
        }
    }
    *result = (uint8_t)((fraction * 255U + scale / 2U) / scale);
    return true;
}

static void mark_host_seen(void) {
    if (host_seen) {
        return;
    }
    host_seen = true;
    oled_controller_show_popup("CODEX MICRO", "CONNECTED", OLED_POPUP_NORMAL_MS);
}

static void send_result(const char *id, const char *result) {
    if (id == NULL) {
        return;
    }
    char response[CODEX_MICRO_OBJECT_SIZE];
    int  length = snprintf(response, sizeof(response), "{\"id\":%s,\"result\":%s}", id, result);
    if (length > 0 && (size_t)length < sizeof(response)) {
        send_json(response);
    }
}

static void send_method_error(const char *id) {
    if (id == NULL) {
        return;
    }
    char response[CODEX_MICRO_OBJECT_SIZE];
    int  length = snprintf(response, sizeof(response), "{\"id\":%s,\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}", id);
    if (length > 0 && (size_t)length < sizeof(response)) {
        send_json(response);
    }
}

static int8_t update_thread_object(const char *object) {
    uint32_t id;
    if (!parse_u32(find_top_level_value(object, "id"), &id) || id >= CODEX_MICRO_HOST_SLOT_COUNT) {
        return -1;
    }

    codex_micro_light_t *light = &thread_lights[id];
    uint32_t             color;
    uint8_t              brightness;
    char                 effect[16];

    if (parse_u32(find_top_level_value(object, "c"), &color)) {
        light->color = color & 0xFFFFFFU;
    }
    if (parse_brightness(find_top_level_value(object, "b"), &brightness)) {
        light->brightness = brightness;
    }
    if (copy_json_string(find_top_level_value(object, "e"), effect, sizeof(effect))) {
        if (strcmp(effect, "off") == 0) {
            light->effect   = CODEX_MICRO_EFFECT_OFF;
            light->assigned = false;
        } else if (strcmp(effect, "breath") == 0) {
            light->effect   = CODEX_MICRO_EFFECT_BREATH;
            light->assigned = true;
        } else {
            light->effect   = CODEX_MICRO_EFFECT_STEADY;
            light->assigned = true;
        }
    } else if (light->color != 0 || light->brightness != 0) {
        light->assigned = true;
        if (light->effect == CODEX_MICRO_EFFECT_OFF) {
            light->effect = CODEX_MICRO_EFFECT_STEADY;
        }
    }
    return (int8_t)id;
}

static void apply_global_lighting(uint8_t brightness, bool off) {
    global_brightness = brightness;
    lighting_off      = off;
}

static void update_global_lighting(uint8_t seen_mask) {
    uint8_t maximum = 0;
    for (uint8_t slot = 0; slot < CODEX_MICRO_HOST_SLOT_COUNT; ++slot) {
        const codex_micro_light_t *light = &thread_lights[slot];
        if (light->assigned && light->effect != CODEX_MICRO_EFFECT_OFF && light->brightness > maximum) {
            maximum = light->brightness;
        }
    }

    if (maximum > 0) {
        apply_global_lighting(maximum, false);
    } else if (seen_mask == CODEX_MICRO_ALL_HOST_SLOTS_MASK) {
        apply_global_lighting(0, true);
    }
}

static void update_thread_lighting(const char *params) {
    if (params == NULL || *params != '[') {
        return;
    }

    const char *cursor = params;
    uint8_t     seen_mask = 0;
    while (*cursor != '\0' && *cursor != ']') {
        if (*cursor != '{') {
            ++cursor;
            continue;
        }

        const char *start     = cursor;
        int         depth     = 0;
        bool        in_string = false;
        bool        escaped   = false;
        do {
            if (in_string) {
                if (!escaped && *cursor == '"') {
                    in_string = false;
                }
                if (!escaped && *cursor == '\\') {
                    escaped = true;
                } else {
                    escaped = false;
                }
            } else if (*cursor == '"') {
                in_string = true;
            } else if (*cursor == '{') {
                ++depth;
            } else if (*cursor == '}') {
                --depth;
            }
            ++cursor;
        } while (*cursor != '\0' && depth > 0);

        size_t length = (size_t)(cursor - start);
        if (depth == 0 && length < CODEX_MICRO_OBJECT_SIZE) {
            char object[CODEX_MICRO_OBJECT_SIZE];
            memcpy(object, start, length);
            object[length] = '\0';
            int8_t id = update_thread_object(object);
            if (id >= 0) {
                seen_mask |= (uint8_t)(1U << id);
            }
        }
    }
    update_global_lighting(seen_mask);
}

static void handle_request(const char *json) {
    char method[32];
    char id[48];
    bool has_method = copy_json_string(find_top_level_value(json, "method"), method, sizeof(method));
    bool has_id     = copy_json_token(find_top_level_value(json, "id"), id, sizeof(id));
    if (!has_method) {
        return;
    }

    mark_host_seen();
    if (strcmp(method, "sys.version") == 0) {
        send_result(has_id ? id : NULL, "{\"version\":\"0.1.0-qmk\"}");
    } else if (strcmp(method, "device.status") == 0) {
        send_result(has_id ? id : NULL, "{\"version\":\"0.1.0-qmk\",\"profile_index\":0,\"layer_index\":1,\"battery\":100,\"is_charging\":false}");
    } else if (strcmp(method, "v.oai.thstatus") == 0) {
        update_thread_lighting(find_top_level_value(json, "params"));
        send_result(has_id ? id : NULL, "{\"ok\":true}");
    } else if (strcmp(method, "v.oai.rgbcfg") == 0 || strcmp(method, "lights.preview") == 0 || strcmp(method, "host.focused_app") == 0) {
        send_result(has_id ? id : NULL, "{\"ok\":true}");
    } else {
        send_method_error(has_id ? id : NULL);
    }
}

static void process_rpc_buffer(void) {
    while (rpc_length > 0) {
        size_t start = 0;
        while (start < rpc_length && rpc_buffer[start] != '{') {
            ++start;
        }
        if (start == rpc_length) {
            rpc_length = 0;
            return;
        }
        if (start > 0) {
            memmove(rpc_buffer, rpc_buffer + start, rpc_length - start);
            rpc_length -= start;
        }

        int  depth     = 0;
        bool in_string = false;
        bool escaped   = false;
        for (size_t index = 0; index < rpc_length; ++index) {
            char value = rpc_buffer[index];
            if (in_string) {
                if (!escaped && value == '"') {
                    in_string = false;
                }
                if (!escaped && value == '\\') {
                    escaped = true;
                } else {
                    escaped = false;
                }
                continue;
            }
            if (value == '"') {
                in_string = true;
            } else if (value == '{') {
                ++depth;
            } else if (value == '}') {
                --depth;
                if (depth == 0) {
                    size_t object_length = index + 1;
                    char   saved         = rpc_buffer[object_length];
                    rpc_buffer[object_length] = '\0';
                    handle_request(rpc_buffer);
                    rpc_buffer[object_length] = saved;
                    memmove(rpc_buffer, rpc_buffer + object_length, rpc_length - object_length);
                    rpc_length -= object_length;
                    goto next_object;
                }
            }
        }
        return;

    next_object:
        continue;
    }
}

void codex_micro_init(void) {
    memset(thread_lights, 0, sizeof(thread_lights));
    for (uint8_t slot = 0; slot < CODEX_MICRO_HOST_SLOT_COUNT; ++slot) {
        thread_lights[slot].brightness = 255;
    }
    rpc_length      = 0;
    host_seen       = false;
    sent_press_mask = 0;
    sent_action_mask = 0;
    sent_encoder_press = false;
    joystick_held_mask = 0;
    joystick_press_count = 0;
    apply_global_lighting(0, true);
}

void codex_micro_task(void) {
    process_rpc_buffer();
}

void codex_micro_send_agent_key(uint8_t slot, bool pressed) {
    if (slot >= CODEX_MICRO_TASK_KEY_COUNT) {
        return;
    }

    uint8_t mask = (uint8_t)(1U << slot);
    if (pressed) {
        if (!host_seen) {
            show_no_link();
            return;
        }
        if (!thread_lights[slot].assigned) {
            oled_controller_show_popup("EMPTY SLOT", "NO TASK", OLED_POPUP_NORMAL_MS);
            return;
        }
        sent_press_mask |= mask;
    } else {
        if ((sent_press_mask & mask) == 0) {
            return;
        }
        sent_press_mask &= (uint8_t)~mask;
    }

    char message[128];
    int  length = snprintf(message, sizeof(message), "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"AG%02u\",\"act\":%u,\"ag\":%u}}", slot, pressed ? 1U : 0U, slot);
    if (length > 0 && (size_t)length < sizeof(message)) {
        send_json(message);
    }

    if (pressed) {
        char label[] = "TASK 1";
        label[5]     = (char)('1' + slot);
        oled_controller_show_popup(label, "REQUESTED", OLED_POPUP_NORMAL_MS);
    }
}

void codex_micro_send_action_key(codex_micro_action_t action, bool pressed) {
    if (action >= CODEX_MICRO_ACTION_COUNT) {
        return;
    }

    uint8_t mask = (uint8_t)(1U << action);
    if (pressed) {
        if (!host_seen) {
            show_no_link();
            return;
        }
        sent_action_mask |= mask;
    } else {
        if ((sent_action_mask & mask) == 0) {
            return;
        }
        sent_action_mask &= (uint8_t)~mask;
    }

    send_hid_key_event(action_key_names[action], pressed ? 1U : 0U);
}

void codex_micro_send_joystick_direction(codex_micro_joystick_direction_t direction, bool pressed) {
    if (direction >= CODEX_MICRO_JOYSTICK_DIRECTION_COUNT) {
        return;
    }

    uint8_t mask = (uint8_t)(1U << direction);
    if (pressed) {
        if ((joystick_held_mask & mask) != 0) {
            return;
        }
        if (!host_seen) {
            show_no_link();
            return;
        }
        joystick_held_mask |= mask;
        joystick_press_order[joystick_press_count++] = (uint8_t)direction;
        send_joystick_position(direction, true);
        return;
    }

    if ((joystick_held_mask & mask) == 0) {
        return;
    }

    bool was_active = joystick_press_count > 0 && joystick_press_order[joystick_press_count - 1] == (uint8_t)direction;
    for (uint8_t index = 0; index < joystick_press_count; ++index) {
        if (joystick_press_order[index] == (uint8_t)direction) {
            memmove(&joystick_press_order[index], &joystick_press_order[index + 1], joystick_press_count - index - 1);
            --joystick_press_count;
            break;
        }
    }
    joystick_held_mask &= (uint8_t)~mask;

    if (was_active) {
        if (joystick_press_count > 0) {
            send_joystick_position((codex_micro_joystick_direction_t)joystick_press_order[joystick_press_count - 1], true);
        } else {
            send_joystick_position(direction, false);
        }
    }
}

void codex_micro_send_encoder_turn(bool clockwise) {
    if (!host_seen) {
        return;
    }
    send_hid_key_event(clockwise ? "ENC_CW" : "ENC_CC", 2);
}

void codex_micro_send_encoder_press(bool pressed) {
    if (pressed) {
        if (!host_seen) {
            show_no_link();
            return;
        }
        sent_encoder_press = true;
    } else {
        if (!sent_encoder_press) {
            return;
        }
        sent_encoder_press = false;
    }
    send_hid_key_event("ENC_SW", pressed ? 1U : 0U);
}

static uint8_t breath_multiplier(void) {
    uint16_t half = CODEX_MICRO_BREATH_MS / 2;
    uint16_t tick = timer_read32() % CODEX_MICRO_BREATH_MS;
    uint16_t ramp = tick < half ? tick : CODEX_MICRO_BREATH_MS - tick;
    return (uint8_t)(90U + (uint32_t)165U * ramp / half);
}

static uint32_t display_thread_color(uint32_t color) {
    switch (color) {
        case CODEX_MICRO_STATUS_UNREAD_COLOR:
            return CODEX_MICRO_CUSTOM_UNREAD_COLOR;
        case CODEX_MICRO_STATUS_WORKING_COLOR:
            return CODEX_MICRO_CUSTOM_WORKING_COLOR;
        case CODEX_MICRO_STATUS_ATTENTION_COLOR:
            return CODEX_MICRO_CUSTOM_ATTENTION_COLOR;
        case CODEX_MICRO_STATUS_ERROR_COLOR:
            return CODEX_MICRO_CUSTOM_ERROR_COLOR;
        default:
            return color;
    }
}

bool codex_micro_rgb_indicators(uint8_t led_min, uint8_t led_max) {
    if (get_highest_layer(layer_state) != 0) {
        return true;
    }

    if (lighting_off) {
        for (uint8_t led = led_min; led < led_max && led < RGB_MATRIX_LED_COUNT; ++led) {
            RGB_MATRIX_INDICATOR_SET_COLOR(led, 0, 0, 0);
        }
        return true;
    }

    uint8_t background_red   = (uint8_t)((uint16_t)CODEX_MICRO_COOL_WHITE_RED * global_brightness / 255U);
    uint8_t background_green = (uint8_t)((uint16_t)CODEX_MICRO_COOL_WHITE_GREEN * global_brightness / 255U);
    uint8_t background_blue  = global_brightness;
    for (uint8_t led = 0; led < RGB_MATRIX_LED_COUNT; ++led) {
        if (led >= led_min && led < led_max) {
            RGB_MATRIX_INDICATOR_SET_COLOR(led, background_red, background_green, background_blue);
        }
    }

    for (uint8_t slot = 0; slot < CODEX_MICRO_TASK_KEY_COUNT; ++slot) {
        uint8_t led = kb16_config_find_native(agent_native_controls[slot]);
        if (led < led_min || led >= led_max) {
            continue;
        }

        const codex_micro_light_t *light = &thread_lights[slot];
        uint8_t red = 0, green = 0, blue = 0;
        if (host_seen && light->assigned && light->effect != CODEX_MICRO_EFFECT_OFF) {
            uint16_t brightness = light->brightness;
            if (light->effect == CODEX_MICRO_EFFECT_BREATH) {
                brightness = brightness * breath_multiplier() / 255U;
            }
            uint32_t display_color = display_thread_color(light->color);
            uint8_t  source_red    = (uint8_t)((display_color >> 16) & 0xFFU);
            uint8_t  source_green  = (uint8_t)((display_color >> 8) & 0xFFU);
            uint8_t  source_blue   = (uint8_t)(display_color & 0xFFU);
            if (light->color == 0xFFFFFFU) {
                source_red   = CODEX_MICRO_COOL_WHITE_RED;
                source_green = CODEX_MICRO_COOL_WHITE_GREEN;
                source_blue  = CODEX_MICRO_COOL_WHITE_BLUE;
            }
            red   = (uint8_t)((uint16_t)source_red * brightness / 255U);
            green = (uint8_t)((uint16_t)source_green * brightness / 255U);
            blue  = (uint8_t)((uint16_t)source_blue * brightness / 255U);
        }
        RGB_MATRIX_INDICATOR_SET_COLOR(led, red, green, blue);
    }
    return true;
}

void raw_hid_receive(uint8_t *data, uint8_t length) {
    if (kb16_config_transport_receive(data, length)) {
        return;
    }
    if (length != CODEX_MICRO_REPORT_SIZE || data[0] != CODEX_MICRO_REPORT_ID || data[1] != CODEX_MICRO_CHANNEL_RPC) {
        return;
    }

    uint8_t payload_length = data[2];
    if (payload_length > CODEX_MICRO_FRAGMENT_SIZE) {
        return;
    }

    const char top_level_prefix[] = "{\"method\"";
    if (rpc_length > 0 && payload_length >= sizeof(top_level_prefix) - 1 && memcmp(&data[3], top_level_prefix, sizeof(top_level_prefix) - 1) == 0) {
        rpc_length = 0;
    }
    if (rpc_length + payload_length >= sizeof(rpc_buffer)) {
        rpc_length = 0;
        oled_controller_show_popup("CODEX MICRO", "RPC TOO LARGE", OLED_POPUP_ERROR_MS);
        return;
    }

    memcpy(rpc_buffer + rpc_length, &data[3], payload_length);
    rpc_length += payload_length;
    rpc_buffer[rpc_length] = '\0';
    process_rpc_buffer();
}
