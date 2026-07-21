#include "codex_micro_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oled_icons.h"
#include "kb16_config.h"
#include "kb16_config_transport.h"
#include "codex_micro_alerts.h"
#include "codex_micro_settings.h"
#include "raw_hid.h"

#define CODEX_MICRO_CHANNEL_RPC 2
#define CODEX_MICRO_RX_BUFFER_SIZE 1024
#define CODEX_MICRO_OBJECT_SIZE 256
#define CODEX_MICRO_DEFAULT_EFFECT_MS 2400
#define CODEX_MICRO_COOL_WHITE_RED 215
#define CODEX_MICRO_COOL_WHITE_GREEN 211
#define CODEX_MICRO_COOL_WHITE_BLUE 255
#define CODEX_MICRO_IDLE_WARM_COLOR 0xFFD09AU
#define CODEX_MICRO_STATUS_UNREAD_COLOR 0x00FF4CU
#define CODEX_MICRO_STATUS_WORKING_COLOR 0x304FFEU
#define CODEX_MICRO_STATUS_ATTENTION_COLOR 0xFF6D00U
#define CODEX_MICRO_STATUS_ERROR_COLOR 0xFF0033U
#define CODEX_MICRO_CUSTOM_UNREAD_COLOR 0x1FAB21U
#define CODEX_MICRO_CUSTOM_WORKING_COLOR 0x005CFFU
#define CODEX_MICRO_CUSTOM_ATTENTION_COLOR 0xC76700U
#define CODEX_MICRO_CUSTOM_ERROR_COLOR 0xC7144DU
#define CODEX_MICRO_ALL_HOST_SLOTS_MASK ((1U << CODEX_MICRO_HOST_SLOT_COUNT) - 1U)

typedef enum {
    CODEX_MICRO_EFFECT_OFF = 0,
    CODEX_MICRO_EFFECT_SOLID = 1,
    CODEX_MICRO_EFFECT_SNAKE = 2,
    CODEX_MICRO_EFFECT_RAINBOW = 3,
    CODEX_MICRO_EFFECT_BREATH = 4,
    CODEX_MICRO_EFFECT_GRADIENT = 5,
    CODEX_MICRO_EFFECT_SHALLOW_BREATH = 6,
} codex_micro_effect_t;

typedef struct {
    uint32_t             color;
    uint8_t              brightness;
    uint8_t              speed;
    codex_micro_effect_t effect;
    bool                 assigned;
    bool                 sync_keys;
    bool                 sync_ambient;
} codex_micro_light_t;

typedef struct {
    uint32_t             color;
    uint8_t              brightness;
    uint8_t              speed;
    uint8_t              magic;
    codex_micro_effect_t effect;
} codex_micro_lighting_side_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} codex_micro_rgb_t;

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
static codex_micro_lighting_side_t ambient_lighting;
static codex_micro_lighting_side_t keys_lighting;
static bool                        lighting_config_seen;
static bool                        thread_snapshot_seen;
static codex_micro_lighting_side_t preview_lighting;
static bool                        preview_lighting_active;
static bool                        status_demo_active;
static codex_micro_slot_state_t    slot_states[CODEX_MICRO_HOST_SLOT_COUNT];
static uint32_t                    reminder_started_at[CODEX_MICRO_HOST_SLOT_COUNT];
static bool                        reminder_acknowledged[CODEX_MICRO_HOST_SLOT_COUNT];

// A continuous path across the physical 4x4 key grid. It keeps snake and
// rainbow effects from jumping between opposite edges at each row boundary.
static const uint8_t led_animation_position[RGB_MATRIX_LED_COUNT] = {
    0, 1, 2, 3,
    7, 6, 5, 4,
    8, 9, 10, 11,
    15, 14, 13, 12,
};

static const char   *const action_key_names[CODEX_MICRO_ACTION_COUNT] = {"ACT06", "ACT07", "ACT08", "ACT09", "ACT12", "ACT10"};
static const char   *const joystick_angles[CODEX_MICRO_JOYSTICK_DIRECTION_COUNT] = {"0.75", "0.00", "0.25", "0.50"};
static const uint8_t agent_native_controls[CODEX_MICRO_TASK_KEY_COUNT] = {
    KB16_NATIVE_AG00, KB16_NATIVE_AG01, KB16_NATIVE_AG02,
    KB16_NATIVE_AG03, KB16_NATIVE_AG04, KB16_NATIVE_AG05,
};

static uint32_t display_thread_color(uint32_t color);

static codex_micro_slot_state_t normalize_slot(const codex_micro_light_t *light) {
    if (!light->assigned || light->effect == CODEX_MICRO_EFFECT_OFF || light->brightness == 0) {
        return CODEX_MICRO_SLOT_OFF;
    }
    switch (light->color) {
        case 0xFFFFFFU: return CODEX_MICRO_SLOT_IDLE;
        case CODEX_MICRO_STATUS_WORKING_COLOR: return CODEX_MICRO_SLOT_WORKING;
        case CODEX_MICRO_STATUS_UNREAD_COLOR: return CODEX_MICRO_SLOT_COMPLETE;
        case CODEX_MICRO_STATUS_ATTENTION_COLOR: return CODEX_MICRO_SLOT_ATTENTION;
        case CODEX_MICRO_STATUS_ERROR_COLOR: return CODEX_MICRO_SLOT_ERROR;
        default: return CODEX_MICRO_SLOT_OTHER;
    }
}

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

static bool parse_bool(const char *value, bool *result) {
    if (value == NULL || result == NULL) {
        return false;
    }
    if (*value == '1' || strncmp(value, "true", 4) == 0) {
        *result = true;
        return true;
    }
    if (*value == '0' || strncmp(value, "false", 5) == 0) {
        *result = false;
        return true;
    }
    return false;
}

static bool parse_effect(const char *value, codex_micro_effect_t *result) {
    uint32_t numeric;
    if (parse_u32(value, &numeric) && numeric <= CODEX_MICRO_EFFECT_SHALLOW_BREATH) {
        *result = (codex_micro_effect_t)numeric;
        return true;
    }

    char name[20];
    if (!copy_json_string(value, name, sizeof(name))) {
        return false;
    }
    if (strcmp(name, "off") == 0) {
        *result = CODEX_MICRO_EFFECT_OFF;
    } else if (strcmp(name, "solid") == 0) {
        *result = CODEX_MICRO_EFFECT_SOLID;
    } else if (strcmp(name, "snake") == 0) {
        *result = CODEX_MICRO_EFFECT_SNAKE;
    } else if (strcmp(name, "rainbow") == 0) {
        *result = CODEX_MICRO_EFFECT_RAINBOW;
    } else if (strcmp(name, "breath") == 0) {
        *result = CODEX_MICRO_EFFECT_BREATH;
    } else if (strcmp(name, "gradient") == 0) {
        *result = CODEX_MICRO_EFFECT_GRADIENT;
    } else if (strcmp(name, "shallowBreath") == 0 || strcmp(name, "shallow_breath") == 0) {
        *result = CODEX_MICRO_EFFECT_SHALLOW_BREATH;
    } else {
        return false;
    }
    return true;
}

static void mark_host_seen(void) {
    if (host_seen) {
        return;
    }
    host_seen = true;
    oled_controller_show_popup("CODEX MICRO", "CONNECTED", OLED_POPUP_NORMAL_MS);
    codex_micro_alerts_queue(CODEX_MICRO_ALERT_RECONNECT, 0xFF, timer_read32(), false);
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
    uint8_t              speed;
    codex_micro_effect_t effect;
    bool                 sync;

    if (parse_u32(find_top_level_value(object, "c"), &color)) {
        light->color = color & 0xFFFFFFU;
    }
    if (parse_brightness(find_top_level_value(object, "b"), &brightness)) {
        light->brightness = brightness;
    }
    if (parse_brightness(find_top_level_value(object, "s"), &speed)) {
        light->speed = speed;
    }
    if (parse_bool(find_top_level_value(object, "sk"), &sync)) {
        light->sync_keys = sync;
    }
    if (parse_bool(find_top_level_value(object, "sa"), &sync)) {
        light->sync_ambient = sync;
    }
    if (parse_effect(find_top_level_value(object, "e"), &effect)) {
        light->effect   = effect;
        light->assigned = effect != CODEX_MICRO_EFFECT_OFF;
    } else if (light->color != 0 || light->brightness != 0) {
        light->assigned = true;
        if (light->effect == CODEX_MICRO_EFFECT_OFF) {
            light->effect = CODEX_MICRO_EFFECT_SOLID;
        }
    }
    return (int8_t)id;
}

static void update_lighting_side(const char *object, codex_micro_lighting_side_t *side) {
    if (object == NULL || *object != '{') {
        return;
    }

    uint32_t             value;
    uint8_t              normalized;
    codex_micro_effect_t effect;
    if (parse_u32(find_top_level_value(object, "c"), &value)) {
        side->color = value & 0xFFFFFFU;
    }
    if (parse_brightness(find_top_level_value(object, "b"), &normalized)) {
        side->brightness = normalized;
    }
    if (parse_brightness(find_top_level_value(object, "s"), &normalized)) {
        side->speed = normalized;
    }
    if (parse_u32(find_top_level_value(object, "m"), &value)) {
        side->magic = (uint8_t)value;
    }
    if (parse_effect(find_top_level_value(object, "e"), &effect)) {
        side->effect = effect;
    }
}

static void update_lighting_config(const char *params) {
    if (params == NULL || *params != '{') {
        return;
    }
    update_lighting_side(find_top_level_value(params, "ambient"), &ambient_lighting);
    update_lighting_side(find_top_level_value(params, "keys"), &keys_lighting);
    lighting_config_seen = true;
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
    uint32_t    now = timer_read32();
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
                const codex_micro_light_t *light = &thread_lights[id];
                codex_micro_slot_state_t previous = slot_states[id];
                codex_micro_slot_state_t current = normalize_slot(light);
                slot_states[id] = current;
                if (current == CODEX_MICRO_SLOT_COMPLETE && previous != current) {
                    reminder_started_at[id] = now;
                    reminder_acknowledged[id] = false;
                    if (thread_snapshot_seen) codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, (uint8_t)id, now, false);
                } else if (current != CODEX_MICRO_SLOT_COMPLETE) {
                    reminder_started_at[id] = 0;
                    reminder_acknowledged[id] = false;
                    codex_micro_alerts_acknowledge_slot((uint8_t)id, now);
                }
                if (thread_snapshot_seen && previous != current && current == CODEX_MICRO_SLOT_ATTENTION) {
                    codex_micro_alerts_queue(CODEX_MICRO_ALERT_APPROVAL, (uint8_t)id, now, false);
                }
                if (thread_snapshot_seen && previous != current && current == CODEX_MICRO_SLOT_ERROR) {
                    codex_micro_alerts_queue(CODEX_MICRO_ALERT_ERROR, (uint8_t)id, now, false);
                }
            }
        }
    }
    update_global_lighting(seen_mask);
    thread_snapshot_seen = true;
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
    } else if (strcmp(method, "v.oai.rgbcfg") == 0) {
        update_lighting_config(find_top_level_value(json, "params"));
        send_result(has_id ? id : NULL, "{\"ok\":true}");
    } else if (strcmp(method, "lights.preview") == 0 || strcmp(method, "host.focused_app") == 0) {
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
        thread_lights[slot].speed      = 102;
    }
    rpc_length      = 0;
    host_seen       = false;
    sent_press_mask = 0;
    sent_action_mask = 0;
    sent_encoder_press = false;
    joystick_held_mask = 0;
    joystick_press_count = 0;
    memset(&ambient_lighting, 0, sizeof(ambient_lighting));
    memset(&keys_lighting, 0, sizeof(keys_lighting));
    memset(&preview_lighting, 0, sizeof(preview_lighting));
    preview_lighting_active = false;
    status_demo_active = false;
    lighting_config_seen       = false;
    thread_snapshot_seen       = false;
    memset(slot_states, 0, sizeof(slot_states));
    memset(reminder_started_at, 0, sizeof(reminder_started_at));
    memset(reminder_acknowledged, 0, sizeof(reminder_acknowledged));
    codex_micro_alerts_init();
    apply_global_lighting(0, true);
}

void codex_micro_task(void) {
    process_rpc_buffer();
    uint32_t now = timer_read32();
    uint32_t interval = codex_micro_settings_reminder_ms(codex_micro_settings_get());
    if (interval > 0) {
        for (uint8_t slot = 0; slot < CODEX_MICRO_HOST_SLOT_COUNT; ++slot) {
            if (slot_states[slot] == CODEX_MICRO_SLOT_COMPLETE && !reminder_acknowledged[slot] &&
                now - reminder_started_at[slot] >= interval) {
                codex_micro_alerts_queue(CODEX_MICRO_ALERT_REMINDER, slot, now, false);
                reminder_started_at[slot] = now;
            }
        }
    }
    codex_micro_alerts_tick(now);
}

void codex_micro_send_agent_key(uint8_t slot, bool pressed) {
    if (slot >= CODEX_MICRO_TASK_KEY_COUNT) {
        return;
    }

    uint8_t mask = (uint8_t)(1U << slot);
    if (pressed) {
        reminder_acknowledged[slot] = true;
        codex_micro_alerts_acknowledge_slot(slot, timer_read32());
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

static uint8_t smoothstep8(uint8_t value) {
    uint32_t squared = (uint32_t)value * value;
    return (uint8_t)((squared * (765U - 2U * value) + 32512U) / 65025U);
}

static uint8_t pulse8(uint8_t phase) {
    uint8_t triangle = phase < 128 ? (uint8_t)(phase * 2U) : (uint8_t)((255U - phase) * 2U);
    return smoothstep8(triangle);
}

static uint16_t effect_period_ms(uint8_t speed) {
    if (speed == 0) {
        return CODEX_MICRO_DEFAULT_EFFECT_MS;
    }
    return (uint16_t)(4200U - (uint32_t)speed * 3200U / 255U);
}

static uint8_t effect_phase(uint32_t now, uint8_t speed) {
    if (speed == 0) {
        return 0;
    }
    uint16_t period = effect_period_ms(speed);
    return (uint8_t)(((now % period) * 255U) / period);
}

static uint8_t adjusted_speed(uint8_t speed) {
    if (speed == 0) return 0;
    uint16_t scaled = (uint16_t)speed * codex_micro_settings_get()->speed_percent / 100U;
    return scaled > 255U ? 255U : (uint8_t)scaled;
}

static uint8_t brightness_cap(void) {
    const codex_micro_settings_t *settings = codex_micro_settings_get();
    uint8_t cap = (uint8_t)((uint16_t)255U * settings->brightness_percent / 100U);
    if ((settings->flags & CODEX_MICRO_SETTING_NIGHT_MODE) != 0 && cap > 89U) cap = 89U;
    return cap;
}

static codex_micro_rgb_t scale_color(uint32_t color, uint8_t brightness, uint8_t multiplier) {
    uint16_t level = (uint16_t)brightness * multiplier / 255U;
    codex_micro_rgb_t result = {
        .red   = (uint8_t)(((color >> 16) & 0xFFU) * level / 255U),
        .green = (uint8_t)(((color >> 8) & 0xFFU) * level / 255U),
        .blue  = (uint8_t)((color & 0xFFU) * level / 255U),
    };
    return result;
}

static codex_micro_rgb_t rainbow_color(uint8_t position, uint8_t brightness) {
    uint32_t color;
    if (position < 85U) {
        uint8_t offset = position * 3U;
        color = ((uint32_t)offset << 16) | ((uint32_t)(255U - offset) << 8);
    } else if (position < 170U) {
        uint8_t offset = (uint8_t)((position - 85U) * 3U);
        color = ((uint32_t)(255U - offset) << 16) | offset;
    } else {
        uint8_t offset = (uint8_t)((position - 170U) * 3U);
        color = ((uint32_t)offset << 8) | (255U - offset);
    }
    return scale_color(color, brightness, 255);
}

static codex_micro_rgb_t blend_color(codex_micro_rgb_t base, uint32_t overlay, uint8_t amount) {
    uint8_t inverse = (uint8_t)(255U - amount);
    codex_micro_rgb_t result = {
        .red   = (uint8_t)(((uint16_t)base.red * inverse + ((overlay >> 16) & 0xFFU) * amount) / 255U),
        .green = (uint8_t)(((uint16_t)base.green * inverse + ((overlay >> 8) & 0xFFU) * amount) / 255U),
        .blue  = (uint8_t)(((uint16_t)base.blue * inverse + (overlay & 0xFFU) * amount) / 255U),
    };
    return result;
}

static codex_micro_rgb_t scale_rgb(codex_micro_rgb_t color, uint8_t amount) {
    codex_micro_rgb_t result = {
        .red   = (uint8_t)((uint16_t)color.red * amount / 255U),
        .green = (uint8_t)((uint16_t)color.green * amount / 255U),
        .blue  = (uint8_t)((uint16_t)color.blue * amount / 255U),
    };
    return result;
}

static codex_micro_rgb_t max_rgb(codex_micro_rgb_t first, codex_micro_rgb_t second) {
    codex_micro_rgb_t result = {
        .red   = first.red > second.red ? first.red : second.red,
        .green = first.green > second.green ? first.green : second.green,
        .blue  = first.blue > second.blue ? first.blue : second.blue,
    };
    return result;
}

static codex_micro_rgb_t render_effect(codex_micro_lighting_side_t side, uint8_t led, uint32_t now) {
    codex_micro_rgb_t off = {0, 0, 0};
    if (side.effect == CODEX_MICRO_EFFECT_OFF || side.brightness == 0) {
        return off;
    }

    uint8_t position = led_animation_position[led];
    uint8_t phase    = effect_phase(now, adjusted_speed(side.speed));
    uint8_t level    = 255;
    switch (side.effect) {
        case CODEX_MICRO_EFFECT_SOLID:
            break;
        case CODEX_MICRO_EFFECT_SNAKE: {
            uint16_t cycle    = RGB_MATRIX_LED_COUNT * 256U;
            uint16_t head     = (uint16_t)((uint32_t)phase * cycle / 255U);
            uint16_t led_at   = position * 256U;
            uint16_t distance = (uint16_t)((head + cycle - led_at) % cycle);
            uint16_t tail     = 6U * 256U;
            if (distance >= tail) {
                level = 0;
            } else {
                level = smoothstep8((uint8_t)(255U - (uint32_t)distance * 255U / tail));
            }
            break;
        }
        case CODEX_MICRO_EFFECT_RAINBOW:
            return rainbow_color((uint8_t)(phase + (uint16_t)position * 256U / RGB_MATRIX_LED_COUNT), side.brightness);
        case CODEX_MICRO_EFFECT_BREATH:
            level = pulse8(phase);
            break;
        case CODEX_MICRO_EFFECT_GRADIENT: {
            uint8_t gradient_position = side.magic & 1U ? (uint8_t)(RGB_MATRIX_LED_COUNT - 1U - position) : position;
            uint8_t gradient = smoothstep8((uint8_t)((uint16_t)gradient_position * 255U / (RGB_MATRIX_LED_COUNT - 1U)));
            level = (uint8_t)(72U + (uint16_t)gradient * 183U / 255U);
            break;
        }
        case CODEX_MICRO_EFFECT_SHALLOW_BREATH:
            level = (uint8_t)(128U + pulse8(phase) / 2U);
            break;
        default:
            return off;
    }
    return scale_color(side.color, side.brightness, level);
}

static bool synced_background_lighting(codex_micro_lighting_side_t *result) {
    for (uint8_t slot = 0; slot < CODEX_MICRO_TASK_KEY_COUNT; ++slot) {
        const codex_micro_light_t *light = &thread_lights[slot];
        if (!light->assigned || (!light->sync_keys && !light->sync_ambient)) {
            continue;
        }
        result->color      = display_thread_color(light->color);
        result->brightness = light->brightness;
        result->speed      = light->speed;
        result->magic      = 0;
        result->effect     = light->effect;
        return true;
    }
    return false;
}

static codex_micro_rgb_t render_background(uint8_t led, uint32_t now) {
    codex_micro_lighting_side_t synced;
    if (synced_background_lighting(&synced)) {
        return render_effect(synced, led, now);
    }

    codex_micro_rgb_t keys = render_effect(keys_lighting, led, now);
    bool perimeter = (codex_micro_settings_get()->flags & CODEX_MICRO_SETTING_PERIMETER) != 0;
    bool inner = led == 5 || led == 6 || led == 9 || led == 10;
    codex_micro_rgb_t ambient = perimeter && inner ? (codex_micro_rgb_t){0, 0, 0} : render_effect(ambient_lighting, led, now);
    if (keys_lighting.effect == CODEX_MICRO_EFFECT_OFF) {
        return ambient;
    }
    if (ambient_lighting.effect == CODEX_MICRO_EFFECT_OFF) {
        return keys;
    }
    if (ambient_lighting.effect != CODEX_MICRO_EFFECT_SOLID) {
        keys = scale_rgb(keys, 72);
    }
    return max_rgb(keys, ambient);
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

static uint32_t status_animation_time(uint32_t now) {
    return (uint32_t)((uint64_t)now * codex_micro_settings_get()->speed_percent / 100U);
}

static uint8_t status_pulse_window(uint32_t phase, uint16_t start, uint16_t length) {
    if (phase < start || phase >= (uint32_t)start + length) return 0;
    return pulse8((uint8_t)((phase - start) * 255U / length));
}

static codex_micro_rgb_t render_agent_status(const codex_micro_light_t *light, codex_micro_slot_state_t state, uint8_t led, uint32_t now) {
    codex_micro_lighting_side_t side = {
        .color = display_thread_color(light->color),
        .brightness = light->brightness,
        .speed = light->speed,
        .magic = 0,
        .effect = light->effect,
    };
    if (light->color == 0xFFFFFFU) {
        side.color = ((uint32_t)CODEX_MICRO_COOL_WHITE_RED << 16) | ((uint32_t)CODEX_MICRO_COOL_WHITE_GREEN << 8) | CODEX_MICRO_COOL_WHITE_BLUE;
    }

    if (state == CODEX_MICRO_SLOT_IDLE) side.color = CODEX_MICRO_IDLE_WARM_COLOR;

    if (state == CODEX_MICRO_SLOT_WORKING) {
        side.effect = CODEX_MICRO_EFFECT_BREATH;
        side.speed = 150;
    } else if (state == CODEX_MICRO_SLOT_COMPLETE) {
        side.effect = CODEX_MICRO_EFFECT_SHALLOW_BREATH;
        side.speed = 72;
    } else if (state == CODEX_MICRO_SLOT_ATTENTION || state == CODEX_MICRO_SLOT_ERROR) {
        uint32_t phase = status_animation_time(now) % 1800U;
        uint8_t level = 48;
        if (state == CODEX_MICRO_SLOT_ATTENTION) {
            uint8_t first = status_pulse_window(phase, 0, 300);
            uint8_t second = status_pulse_window(phase, 450, 300);
            level = first > second ? first : second;
        } else {
            uint32_t beat = phase % 600U;
            level = beat < 400U ? pulse8((uint8_t)(beat * 255U / 400U)) : 0;
        }
        if (level < 48) level = 48;
        return scale_color(side.color, side.brightness, level);
    }
    return render_effect(side, led, now);
}

bool codex_micro_rgb_indicators(uint8_t led_min, uint8_t led_max) {
    uint32_t now = timer_read32();
    codex_micro_alerts_tick(now);
    bool has_alert = codex_micro_alerts_active() != CODEX_MICRO_ALERT_NONE;
    uint8_t alert_origin = 0xFF;
    uint8_t alert_slot = codex_micro_alerts_active_slot();
    if (alert_slot < CODEX_MICRO_TASK_KEY_COUNT) {
        uint8_t origin_led = kb16_config_find_native(agent_native_controls[alert_slot]);
        if (origin_led < RGB_MATRIX_LED_COUNT) alert_origin = led_animation_position[origin_led];
    }
    if (get_highest_layer(layer_state) != 0) {
        if (has_alert) {
            for (uint8_t led = led_min; led < led_max && led < RGB_MATRIX_LED_COUNT; ++led) {
                codex_micro_alert_sample_t sample;
                if (codex_micro_alerts_sample(led_animation_position[led], alert_origin, RGB_MATRIX_LED_COUNT, now, &sample)) {
                    codex_micro_rgb_t flash = scale_color(sample.color, 255, sample.amount);
                    flash = scale_rgb(flash, brightness_cap());
                    RGB_MATRIX_INDICATOR_SET_COLOR(led, flash.red, flash.green, flash.blue);
                }
            }
        }
        return true;
    }

    if (!lighting_config_seen && lighting_off && !has_alert && codex_micro_settings_get()->background_percent == 0) {
        for (uint8_t led = led_min; led < led_max && led < RGB_MATRIX_LED_COUNT; ++led) {
            RGB_MATRIX_INDICATOR_SET_COLOR(led, 0, 0, 0);
        }
        return true;
    }

    for (uint8_t led = 0; led < RGB_MATRIX_LED_COUNT; ++led) {
        if (led >= led_min && led < led_max) {
            codex_micro_rgb_t color;
            if (lighting_config_seen) {
                color = render_background(led, now);
            } else {
                color.red   = (uint8_t)((uint16_t)CODEX_MICRO_COOL_WHITE_RED * global_brightness / 255U);
                color.green = (uint8_t)((uint16_t)CODEX_MICRO_COOL_WHITE_GREEN * global_brightness / 255U);
                color.blue  = global_brightness;
            }
            if (status_demo_active) color = (codex_micro_rgb_t){0, 0, 0};
            else if (preview_lighting_active) color = render_effect(preview_lighting, led, now);
            if (!status_demo_active && color.red == 0 && color.green == 0 && color.blue == 0 && codex_micro_settings_get()->background_percent > 0) {
                color = scale_color(((uint32_t)CODEX_MICRO_COOL_WHITE_RED << 16) | ((uint32_t)CODEX_MICRO_COOL_WHITE_GREEN << 8) |
                                        CODEX_MICRO_COOL_WHITE_BLUE,
                                    255, (uint8_t)((uint16_t)255U * codex_micro_settings_get()->background_percent / 100U));
            }
            codex_micro_alert_sample_t sample;
            if (codex_micro_alerts_sample(led_animation_position[led], alert_origin, RGB_MATRIX_LED_COUNT, now, &sample)) {
                color = blend_color(color, sample.color, sample.amount);
            }
            color = scale_rgb(color, brightness_cap());
            RGB_MATRIX_INDICATOR_SET_COLOR(led, color.red, color.green, color.blue);
        }
    }

    for (uint8_t slot = 0; slot < CODEX_MICRO_TASK_KEY_COUNT; ++slot) {
        uint8_t led = kb16_config_find_native(agent_native_controls[slot]);
        if (led < led_min || led >= led_max) {
            continue;
        }

        codex_micro_rgb_t color = {0, 0, 0};
        const codex_micro_light_t *light = &thread_lights[slot];
        if (status_demo_active) {
            static const codex_micro_slot_state_t demo_states[] = {
                CODEX_MICRO_SLOT_IDLE, CODEX_MICRO_SLOT_WORKING, CODEX_MICRO_SLOT_COMPLETE,
                CODEX_MICRO_SLOT_ATTENTION, CODEX_MICRO_SLOT_ERROR, CODEX_MICRO_SLOT_OFF,
            };
            static const uint32_t demo_colors[] = {
                0xFFFFFFU, CODEX_MICRO_STATUS_WORKING_COLOR, CODEX_MICRO_STATUS_UNREAD_COLOR,
                CODEX_MICRO_STATUS_ATTENTION_COLOR, CODEX_MICRO_STATUS_ERROR_COLOR, 0,
            };
            codex_micro_light_t demo = {.color = demo_colors[slot], .brightness = 255, .speed = 100, .effect = CODEX_MICRO_EFFECT_SOLID, .assigned = slot != 5};
            if (demo.assigned) color = render_agent_status(&demo, demo_states[slot], led, now);
        } else if (host_seen && light->assigned && light->effect != CODEX_MICRO_EFFECT_OFF) {
            color = render_agent_status(light, slot_states[slot], led, now);
        }
        if (preview_lighting_active) color = render_effect(preview_lighting, led, now);
        codex_micro_alert_sample_t sample;
        if (codex_micro_alerts_sample(led_animation_position[led], alert_origin, RGB_MATRIX_LED_COUNT, now, &sample)) {
            color = blend_color(color, sample.color, sample.amount);
        }
        color = scale_rgb(color, brightness_cap());
        RGB_MATRIX_INDICATOR_SET_COLOR(led, color.red, color.green, color.blue);
    }
    return true;
}

bool codex_micro_host_connected(void) { return host_seen; }
codex_micro_slot_state_t codex_micro_slot_state(uint8_t slot) { return slot < CODEX_MICRO_HOST_SLOT_COUNT ? slot_states[slot] : CODEX_MICRO_SLOT_OFF; }
char codex_micro_slot_mark(uint8_t slot) {
    static const char marks[] = {'-', 'I', 'W', 'C', '!', 'E', '?'};
    return marks[codex_micro_slot_state(slot)];
}
int8_t codex_micro_selected_slot(void) {
    for (uint8_t slot = 0; slot < CODEX_MICRO_HOST_SLOT_COUNT; ++slot) {
        if (thread_lights[slot].assigned && thread_lights[slot].effect == CODEX_MICRO_EFFECT_BREATH) return (int8_t)slot;
    }
    return -1;
}
const char *codex_micro_alert_label(void) {
    static const char *const labels[] = {"READY", "REMINDER", "RECONNECTED", "COMPLETE", "NEEDS INPUT", "ERROR"};
    return labels[codex_micro_alerts_active()];
}
uint8_t codex_micro_alert_slot(void) { return codex_micro_alerts_active_slot(); }
void codex_micro_preview_alert(uint8_t alert) {
    preview_lighting_active = false;
    status_demo_active = false;
    int8_t selected = codex_micro_selected_slot();
    uint8_t slot = selected >= 0 ? (uint8_t)selected : 0;
    if (alert > CODEX_MICRO_ALERT_NONE && alert < CODEX_MICRO_ALERT_COUNT) codex_micro_alerts_queue((codex_micro_alert_t)alert, slot, timer_read32(), true);
}
void codex_micro_preview_effect(uint8_t effect) {
    if (effect == 0 || effect > CODEX_MICRO_EFFECT_SHALLOW_BREATH) {
        preview_lighting_active = false;
        status_demo_active = false;
        return;
    }
    codex_micro_alerts_cancel_preview(timer_read32());
    preview_lighting_active = true;
    status_demo_active = false;
    preview_lighting = (codex_micro_lighting_side_t){
        .color = effect == CODEX_MICRO_EFFECT_SNAKE || effect == CODEX_MICRO_EFFECT_GRADIENT ? CODEX_MICRO_STATUS_WORKING_COLOR : 0xD7D3FFU,
        .brightness = 255,
        .speed = 112,
        .magic = 0,
        .effect = (codex_micro_effect_t)effect,
    };
}
void codex_micro_preview_status_demo(void) {
    codex_micro_alerts_cancel_preview(timer_read32());
    preview_lighting_active = false;
    status_demo_active = true;
}
void codex_micro_cancel_previews(void) {
    preview_lighting_active = false;
    status_demo_active = false;
    codex_micro_alerts_cancel_preview(timer_read32());
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
