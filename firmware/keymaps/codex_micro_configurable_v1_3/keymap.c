#include QMK_KEYBOARD_H

#include "codex_micro_protocol.h"
#include "kb16_config.h"
#include "oled_icons.h"

#define HOLD_ACTION_MS 1000
#define MIDDLE_RETURN_MS 3000
#define OLED_BRIGHTNESS_STEP 16

enum layer_names {
    _CODEX,
    _NUM,
    _NAV,
    _SYS,
};

enum custom_keycodes {
    CFG_KEY_0 = SAFE_RANGE,
    CFG_KEY_1,
    CFG_KEY_2,
    CFG_KEY_3,
    CFG_KEY_4,
    CFG_KEY_5,
    CFG_KEY_6,
    CFG_KEY_7,
    CFG_KEY_8,
    CFG_KEY_9,
    CFG_KEY_10,
    CFG_KEY_11,
    CFG_KEY_12,
    CFG_KEY_13,
    CFG_KEY_14,
    CFG_KEY_15,
    CFG_ENCODER_LEFT_CCW,
    CFG_ENCODER_LEFT_CW,
    CFG_ENCODER_RIGHT_CCW,
    CFG_ENCODER_RIGHT_CW,
    CFG_ENCODER_MIDDLE_CCW,
    CFG_ENCODER_MIDDLE_CW,
    CFG_ENCODER_LEFT_PRESS,
    CFG_ENCODER_RIGHT_PRESS,
    CFG_ENCODER_MIDDLE_PRESS,
};

typedef struct {
    bool     pressed;
    bool     fired;
    uint16_t pressed_at;
    uint8_t  display_stage;
} guarded_action_t;

typedef struct {
    bool          active;
    bool          native;
    uint8_t       native_control;
    kb16_action_t action;
} held_control_t;

static guarded_action_t stop_action;
static guarded_action_t archive_action;
static guarded_action_t boot_action;
static held_control_t   held_keys[KB16_CONFIG_KEY_COUNT];
static held_control_t   held_encoder_presses[KB16_CONFIG_ENCODER_COUNT];
static uint8_t          modifier_counts[8];
static bool             middle_pressed;
static bool             middle_long_fired;
static uint16_t         middle_pressed_at;
static kb16_action_t    middle_short_action;

static void show_no_link(void) {
    oled_controller_show_popup("NO LINK", "CODEX MICRO", OLED_POPUP_ERROR_MS);
}

static void cycle_layer(void) {
    uint8_t active_layer = get_highest_layer(layer_state);
    layer_move((active_layer + 1) % KB16_CONFIG_LAYER_COUNT);
}

static void start_guarded_action(guarded_action_t *action, const char *label) {
    action->pressed = true;
    action->fired = false;
    action->pressed_at = timer_read();
    action->display_stage = 0;
    oled_controller_show_popup(label, "HOLD", HOLD_ACTION_MS + 200);
}

static void cancel_guarded_action(guarded_action_t *action, const char *label) {
    action->pressed = false;
    if (!action->fired) {
        oled_controller_show_popup(label, "CANCEL", OLED_POPUP_NORMAL_MS);
    }
}

static void scan_no_link_guard(guarded_action_t *action) {
    if (action->pressed && !action->fired && timer_elapsed(action->pressed_at) >= HOLD_ACTION_MS) {
        action->fired = true;
        show_no_link();
    }
}

static void scan_boot_guard(void) {
    if (!boot_action.pressed || boot_action.fired) {
        return;
    }
    uint16_t elapsed = timer_elapsed(boot_action.pressed_at);
    uint8_t stage = elapsed < 334 ? 3 : (elapsed < 667 ? 2 : 1);
    if (stage != boot_action.display_stage) {
        boot_action.display_stage = stage;
        char countdown[2] = {(char)('0' + stage), '\0'};
        oled_controller_show_popup("BOOTLOADER", countdown, HOLD_ACTION_MS + 200);
    }
    if (elapsed >= HOLD_ACTION_MS) {
        boot_action.fired = true;
        reset_keyboard();
    }
}

static uint16_t consumer_keycode(uint16_t code) {
    switch (code) {
        case KB16_CONSUMER_VOLUME_UP: return KC_VOLU;
        case KB16_CONSUMER_VOLUME_DOWN: return KC_VOLD;
        case KB16_CONSUMER_MUTE: return KC_MUTE;
        case KB16_CONSUMER_PLAY_PAUSE: return KC_MPLY;
        case KB16_CONSUMER_NEXT: return KC_MNXT;
        case KB16_CONSUMER_PREVIOUS: return KC_MPRV;
        default: return KC_NO;
    }
}

static uint16_t mouse_keycode(uint16_t code) {
    switch (code) {
        case KB16_MOUSE_BUTTON_1: return MS_BTN1;
        case KB16_MOUSE_BUTTON_2: return MS_BTN2;
        case KB16_MOUSE_BUTTON_3: return MS_BTN3;
        case KB16_MOUSE_BUTTON_4: return MS_BTN4;
        case KB16_MOUSE_BUTTON_5: return MS_BTN5;
        case KB16_MOUSE_WHEEL_UP: return MS_WHLU;
        case KB16_MOUSE_WHEEL_DOWN: return MS_WHLD;
        case KB16_MOUSE_WHEEL_LEFT: return MS_WHLL;
        case KB16_MOUSE_WHEEL_RIGHT: return MS_WHLR;
        default: return KC_NO;
    }
}

static void update_action_modifiers(uint8_t modifiers, bool pressed) {
    for (uint8_t bit = 0; bit < 8; ++bit) {
        uint8_t mask = (uint8_t)(1U << bit);
        if ((modifiers & mask) == 0) {
            continue;
        }
        if (pressed) {
            if (modifier_counts[bit]++ == 0) {
                register_mods(mask);
            }
        } else if (modifier_counts[bit] > 0 && --modifier_counts[bit] == 0) {
            unregister_mods(mask);
        }
    }
}

static void execute_firmware_action(uint16_t code, bool pressed) {
    if (code == KB16_FIRMWARE_NATIVE_ENCODER_PRESS) {
        codex_micro_send_encoder_press(pressed);
        return;
    }
    if (code == KB16_FIRMWARE_BOOTLOADER_HOLD) {
        pressed ? start_guarded_action(&boot_action, "BOOTLOADER") : cancel_guarded_action(&boot_action, "BOOTLOADER");
        return;
    }
    if (code == KB16_FIRMWARE_ARCHIVE_HOLD) {
        pressed ? start_guarded_action(&archive_action, "ARCHIVE") : cancel_guarded_action(&archive_action, "ARCHIVE");
        return;
    }
    if (!pressed) {
        return;
    }
    switch (code) {
        case KB16_FIRMWARE_LAYER_CYCLE: cycle_layer(); break;
        case KB16_FIRMWARE_LAYER_CODEX: layer_move(_CODEX); break;
        case KB16_FIRMWARE_LAYER_NUM: layer_move(_NUM); break;
        case KB16_FIRMWARE_LAYER_NAV: layer_move(_NAV); break;
        case KB16_FIRMWARE_LAYER_SYS: layer_move(_SYS); break;
#ifdef RGB_MATRIX_ENABLE
        case KB16_FIRMWARE_RGB_TOGGLE: rgb_matrix_toggle(); break;
        case KB16_FIRMWARE_RGB_MODE_PREVIOUS: rgb_matrix_step_reverse(); break;
        case KB16_FIRMWARE_RGB_MODE_NEXT: rgb_matrix_step(); break;
        case KB16_FIRMWARE_RGB_VALUE_DOWN: rgb_matrix_decrease_val(); break;
        case KB16_FIRMWARE_RGB_VALUE_UP: rgb_matrix_increase_val(); break;
        case KB16_FIRMWARE_RGB_SOLID: {
            uint8_t brightness = rgb_matrix_get_val();
            rgb_matrix_mode(RGB_MATRIX_SOLID_COLOR);
            rgb_matrix_sethsv(190, 44, brightness);
            oled_controller_show_popup("RGB", "COOL WHITE", OLED_POPUP_NORMAL_MS);
            break;
        }
#endif
        case KB16_FIRMWARE_OLED_BRIGHTNESS_DOWN:
        case KB16_FIRMWARE_OLED_BRIGHTNESS_UP:
#ifdef OLED_ENABLE
        {
            uint8_t brightness = oled_get_brightness();
            brightness = code == KB16_FIRMWARE_OLED_BRIGHTNESS_DOWN
                             ? (brightness > OLED_BRIGHTNESS_STEP ? brightness - OLED_BRIGHTNESS_STEP : 0)
                             : (brightness < 255 - OLED_BRIGHTNESS_STEP ? brightness + OLED_BRIGHTNESS_STEP : 255);
            oled_set_brightness(brightness);
            break;
        }
#else
            break;
#endif
        case KB16_FIRMWARE_NO_LINK: show_no_link(); break;
        case KB16_FIRMWARE_NATIVE_ENCODER_CCW: codex_micro_send_encoder_turn(false); break;
        case KB16_FIRMWARE_NATIVE_ENCODER_CW: codex_micro_send_encoder_turn(true); break;
        default: break;
    }
}

static void execute_action(const kb16_action_t *action, bool pressed) {
    if (action == NULL) {
        return;
    }
    switch (action->kind) {
        case KB16_ACTION_KEYBOARD:
            if (pressed) {
                update_action_modifiers(action->modifiers, true);
                register_code((uint8_t)action->code);
            } else {
                unregister_code((uint8_t)action->code);
                update_action_modifiers(action->modifiers, false);
            }
            break;
        case KB16_ACTION_CONSUMER: {
            uint16_t keycode = consumer_keycode(action->code);
            pressed ? register_code16(keycode) : unregister_code16(keycode);
            break;
        }
        case KB16_ACTION_MOUSE: {
            uint16_t keycode = mouse_keycode(action->code);
            pressed ? register_code16(keycode) : unregister_code16(keycode);
            break;
        }
        case KB16_ACTION_FIRMWARE:
            execute_firmware_action(action->code, pressed);
            break;
        default:
            break;
    }
}

static void tap_action(const kb16_action_t *action) {
    execute_action(action, true);
    execute_action(action, false);
}

static void execute_native(uint8_t control, bool pressed) {
    switch (control) {
        case KB16_NATIVE_AG00: codex_micro_send_agent_key(0, pressed); break;
        case KB16_NATIVE_AG01: codex_micro_send_agent_key(1, pressed); break;
        case KB16_NATIVE_AG02: codex_micro_send_agent_key(2, pressed); break;
        case KB16_NATIVE_AG03: codex_micro_send_agent_key(3, pressed); break;
        case KB16_NATIVE_AG04: codex_micro_send_agent_key(4, pressed); break;
        case KB16_NATIVE_AG05: codex_micro_send_agent_key(5, pressed); break;
        case KB16_NATIVE_JOY_UP: codex_micro_send_joystick_direction(CODEX_MICRO_JOYSTICK_UP, pressed); break;
        case KB16_NATIVE_JOY_RIGHT: codex_micro_send_joystick_direction(CODEX_MICRO_JOYSTICK_RIGHT, pressed); break;
        case KB16_NATIVE_JOY_DOWN: codex_micro_send_joystick_direction(CODEX_MICRO_JOYSTICK_DOWN, pressed); break;
        case KB16_NATIVE_JOY_LEFT: codex_micro_send_joystick_direction(CODEX_MICRO_JOYSTICK_LEFT, pressed); break;
        case KB16_NATIVE_ACT06: codex_micro_send_action_key(CODEX_MICRO_ACTION_06, pressed); break;
        case KB16_NATIVE_ACT07: codex_micro_send_action_key(CODEX_MICRO_ACTION_07, pressed); break;
        case KB16_NATIVE_ACT08: codex_micro_send_action_key(CODEX_MICRO_ACTION_08, pressed); break;
        case KB16_NATIVE_ACT09: codex_micro_send_action_key(CODEX_MICRO_ACTION_09, pressed); break;
        case KB16_NATIVE_ACT12: codex_micro_send_action_key(CODEX_MICRO_ACTION_12, pressed); break;
        case KB16_NATIVE_MIC: codex_micro_send_action_key(CODEX_MICRO_ACTION_MIC, pressed); break;
        default: break;
    }
}

/* Matrix tail positions are physical knob presses in left, right, middle order. */
#define CONFIGURABLE_LAYOUT LAYOUT( \
    CFG_KEY_0, CFG_KEY_1, CFG_KEY_2, CFG_KEY_3, CFG_ENCODER_LEFT_PRESS, \
    CFG_KEY_4, CFG_KEY_5, CFG_KEY_6, CFG_KEY_7, CFG_ENCODER_RIGHT_PRESS, \
    CFG_KEY_8, CFG_KEY_9, CFG_KEY_10, CFG_KEY_11, CFG_ENCODER_MIDDLE_PRESS, \
    CFG_KEY_12, CFG_KEY_13, CFG_KEY_14, CFG_KEY_15)

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_CODEX] = CONFIGURABLE_LAYOUT,
    [_NUM] = CONFIGURABLE_LAYOUT,
    [_NAV] = CONFIGURABLE_LAYOUT,
    [_SYS] = CONFIGURABLE_LAYOUT,
};

#ifdef ENCODER_MAP_ENABLE
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [_CODEX] = {ENCODER_CCW_CW(CFG_ENCODER_LEFT_CCW, CFG_ENCODER_LEFT_CW), ENCODER_CCW_CW(CFG_ENCODER_RIGHT_CCW, CFG_ENCODER_RIGHT_CW), ENCODER_CCW_CW(CFG_ENCODER_MIDDLE_CCW, CFG_ENCODER_MIDDLE_CW)},
    [_NUM] = {ENCODER_CCW_CW(CFG_ENCODER_LEFT_CCW, CFG_ENCODER_LEFT_CW), ENCODER_CCW_CW(CFG_ENCODER_RIGHT_CCW, CFG_ENCODER_RIGHT_CW), ENCODER_CCW_CW(CFG_ENCODER_MIDDLE_CCW, CFG_ENCODER_MIDDLE_CW)},
    [_NAV] = {ENCODER_CCW_CW(CFG_ENCODER_LEFT_CCW, CFG_ENCODER_LEFT_CW), ENCODER_CCW_CW(CFG_ENCODER_RIGHT_CCW, CFG_ENCODER_RIGHT_CW), ENCODER_CCW_CW(CFG_ENCODER_MIDDLE_CCW, CFG_ENCODER_MIDDLE_CW)},
    [_SYS] = {ENCODER_CCW_CW(CFG_ENCODER_LEFT_CCW, CFG_ENCODER_LEFT_CW), ENCODER_CCW_CW(CFG_ENCODER_RIGHT_CCW, CFG_ENCODER_RIGHT_CW), ENCODER_CCW_CW(CFG_ENCODER_MIDDLE_CCW, CFG_ENCODER_MIDDLE_CW)},
};
#endif
static uint8_t current_layer(void) {
    uint8_t layer = get_highest_layer(layer_state);
    return layer < KB16_CONFIG_LAYER_COUNT ? layer : _CODEX;
}

static void handle_matrix_key(uint8_t position, bool pressed) {
    held_control_t *held = &held_keys[position];
    if (pressed) {
        kb16_config_input_pressed();
        memset(held, 0, sizeof(*held));
        held->active = true;
        uint8_t layer = current_layer();
        if (layer == _CODEX) {
            held->native = true;
            held->native_control = kb16_config_native_at(position);
            execute_native(held->native_control, true);
        } else {
            const kb16_action_t *action = kb16_config_key_action(layer, position);
            if (action != NULL) {
                held->action = *action;
                execute_action(&held->action, true);
            }
        }
    } else {
        if (held->active) {
            held->native ? execute_native(held->native_control, false) : execute_action(&held->action, false);
            held->active = false;
        }
        kb16_config_input_released();
    }
}

static void handle_encoder_turn(uint8_t encoder, uint8_t action_index) {
    uint8_t layer = current_layer();
    if (layer == _CODEX && encoder == 0) {
        codex_micro_send_encoder_turn(action_index == KB16_ENCODER_CW);
        return;
    }
    tap_action(kb16_config_encoder_action(layer, encoder, action_index));
}

static void handle_encoder_press(uint8_t encoder, bool pressed) {
    if (encoder == 2) {
        if (pressed) {
            kb16_config_input_pressed();
            middle_pressed = true;
            middle_long_fired = false;
            middle_pressed_at = timer_read();
            const kb16_action_t *action = kb16_config_encoder_action(current_layer(), encoder, KB16_ENCODER_PRESS);
            middle_short_action = action == NULL ? (kb16_action_t){0} : *action;
        } else {
            if (middle_pressed && !middle_long_fired) {
                tap_action(&middle_short_action);
            }
            middle_pressed = false;
            kb16_config_input_released();
        }
        return;
    }
    held_control_t *held = &held_encoder_presses[encoder];
    if (pressed) {
        kb16_config_input_pressed();
        memset(held, 0, sizeof(*held));
        held->active = true;
        if (current_layer() == _CODEX && encoder == 0) {
            held->native = true;
            codex_micro_send_encoder_press(true);
        } else {
            const kb16_action_t *action = kb16_config_encoder_action(current_layer(), encoder, KB16_ENCODER_PRESS);
            if (action != NULL) {
                held->action = *action;
                execute_action(&held->action, true);
            }
        }
    } else {
        if (held->active) {
            held->native ? codex_micro_send_encoder_press(false) : execute_action(&held->action, false);
            held->active = false;
        }
        kb16_config_input_released();
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode >= CFG_KEY_0 && keycode <= CFG_KEY_15) {
        handle_matrix_key((uint8_t)(keycode - CFG_KEY_0), record->event.pressed);
        return false;
    }
    if (record->event.pressed) {
        switch (keycode) {
            case CFG_ENCODER_LEFT_CCW: handle_encoder_turn(0, KB16_ENCODER_CCW); return false;
            case CFG_ENCODER_LEFT_CW: handle_encoder_turn(0, KB16_ENCODER_CW); return false;
            case CFG_ENCODER_RIGHT_CCW: handle_encoder_turn(1, KB16_ENCODER_CCW); return false;
            case CFG_ENCODER_RIGHT_CW: handle_encoder_turn(1, KB16_ENCODER_CW); return false;
            case CFG_ENCODER_MIDDLE_CCW: handle_encoder_turn(2, KB16_ENCODER_CCW); return false;
            case CFG_ENCODER_MIDDLE_CW: handle_encoder_turn(2, KB16_ENCODER_CW); return false;
        }
    }
    switch (keycode) {
        case CFG_ENCODER_LEFT_PRESS: handle_encoder_press(0, record->event.pressed); return false;
        case CFG_ENCODER_RIGHT_PRESS: handle_encoder_press(1, record->event.pressed); return false;
        case CFG_ENCODER_MIDDLE_PRESS: handle_encoder_press(2, record->event.pressed); return false;
    }
    return true;
}

void matrix_scan_user(void) {
    scan_no_link_guard(&stop_action);
    scan_no_link_guard(&archive_action);
    scan_boot_guard();
    if (middle_pressed && !middle_long_fired && timer_elapsed(middle_pressed_at) >= MIDDLE_RETURN_MS) {
        middle_long_fired = true;
        layer_move(_CODEX);
        oled_controller_show_popup("CODEX", "RETURN", OLED_POPUP_NORMAL_MS);
    }
    codex_micro_task();
}

void keyboard_post_init_user(void) {
    kb16_config_init();
    codex_micro_init();
}

#ifdef RGB_MATRIX_ENABLE
bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    return codex_micro_rgb_indicators(led_min, led_max);
}
#endif

#ifdef OLED_ENABLE
bool oled_task_user(void) {
    oled_controller_render();
    return true;
}
#endif
