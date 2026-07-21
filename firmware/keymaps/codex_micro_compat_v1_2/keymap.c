#include QMK_KEYBOARD_H

#include "codex_micro_protocol.h"
#include "oled_icons.h"

#define HOLD_ACTION_MS 1000
#define OLED_BRIGHTNESS_STEP 16

enum layer_names {
    _CODEX,
    _NUM,
    _NAV,
    _SYS,
};

enum custom_keycodes {
    CDX_TASK_1 = SAFE_RANGE,
    CDX_TASK_2,
    CDX_TASK_3,
    CDX_TASK_4,
    CDX_TASK_5,
    CDX_TASK_6,
    CDX_JOYSTICK_UP,
    CDX_JOYSTICK_RIGHT,
    CDX_JOYSTICK_DOWN,
    CDX_JOYSTICK_LEFT,
    CDX_ACTION_06,
    CDX_ACTION_07,
    CDX_ACTION_08,
    CDX_ACTION_09,
    CDX_ACTION_12,
    CDX_ACTION_MIC,
    CDX_ENCODER_CCW,
    CDX_ENCODER_CW,
    CDX_ENCODER_PRESS,
    CDX_NEW_TASK,
    CDX_CONTINUE,
    CDX_STOP,
    CDX_FORK,
    CDX_FOCUS_TASK,
    CDX_FOCUS_INPUT,
    CDX_APPROVE,
    CDX_REJECT,
    CDX_PLAN_MODE,
    CDX_ADD_FILE,
    CDX_COPY_REPLY,
    CDX_VOICE,
    CDX_OPEN_APP,
    CDX_OPEN_TERM,
    CDX_OPEN_PROJECT,
    CDX_BRIDGE_STATUS,
    CDX_RECONNECT,
    CDX_VIEW_LOG,
    CDX_ARCHIVE,
    CDX_SIDEBAR_CLOSE,
    CDX_SIDEBAR_OPEN,
    CDX_REASON_DOWN,
    CDX_REASON_UP,
    CDX_REASON_CONFIRM,
    OLED_BRIGHTNESS_DOWN,
    OLED_BRIGHTNESS_UP,
    RGB_SOLID_MODE,
    LAYER_CYCLE,
    BOOT_HOLD,
};

typedef struct {
    bool     pressed;
    bool     fired;
    uint16_t pressed_at;
    uint8_t  display_stage;
} guarded_action_t;

static guarded_action_t stop_action;
static guarded_action_t archive_action;
static guarded_action_t boot_action;

static void show_no_link(void) {
    oled_controller_show_popup("NO LINK", "CODEX MICRO", OLED_POPUP_ERROR_MS);
}

static void cycle_layer(void) {
    uint8_t active_layer = get_highest_layer(layer_state);
    layer_move((active_layer + 1) % 4);
}

static void start_guarded_action(guarded_action_t *action, const char *label) {
    action->pressed       = true;
    action->fired         = false;
    action->pressed_at    = timer_read();
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
    if (!action->pressed || action->fired || timer_elapsed(action->pressed_at) < HOLD_ACTION_MS) {
        return;
    }

    action->fired = true;
    show_no_link();
}

static void scan_boot_guard(void) {
    if (!boot_action.pressed || boot_action.fired) {
        return;
    }

    uint16_t elapsed = timer_elapsed(boot_action.pressed_at);
    uint8_t  stage   = elapsed < 334 ? 3 : (elapsed < 667 ? 2 : 1);
    if (stage != boot_action.display_stage) {
        boot_action.display_stage = stage;
        if (stage == 3) {
            oled_controller_show_popup("BOOTLOADER", "3", HOLD_ACTION_MS + 200);
        } else if (stage == 2) {
            oled_controller_show_popup("BOOTLOADER", "2", HOLD_ACTION_MS + 200);
        } else {
            oled_controller_show_popup("BOOTLOADER", "1", HOLD_ACTION_MS + 200);
        }
    }

    if (elapsed >= HOLD_ACTION_MS) {
        boot_action.fired = true;
        reset_keyboard();
    }
}

/*
 * The three matrix positions after each 4-key row are physical knob presses.
 * Real-device testing established this order: left, right, middle.
 */
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_CODEX] = LAYOUT(
        CDX_TASK_1,       CDX_TASK_2,        CDX_JOYSTICK_UP,  CDX_JOYSTICK_RIGHT, CDX_ENCODER_PRESS,
        CDX_TASK_3,       CDX_TASK_4,        CDX_JOYSTICK_LEFT,CDX_JOYSTICK_DOWN, CDX_REASON_CONFIRM,
        CDX_TASK_5,       CDX_TASK_6,        CDX_ACTION_06,    CDX_ACTION_07,     LAYER_CYCLE,
        CDX_ACTION_08,    CDX_ACTION_09,     CDX_ACTION_12,    CDX_ACTION_MIC
    ),
    [_NUM] = LAYOUT(
        KC_KP_SLASH,      KC_KP_ASTERISK,    KC_KP_MINUS,      KC_KP_PLUS,       CDX_ENCODER_PRESS,
        KC_KP_7,          KC_KP_8,           KC_KP_9,          KC_KP_ENTER,       KC_EQUAL,
        KC_KP_4,          KC_KP_5,           KC_KP_6,          KC_KP_DOT,         LAYER_CYCLE,
        KC_KP_1,          KC_KP_2,           KC_KP_3,          KC_KP_0
    ),
    [_NAV] = LAYOUT(
        C(KC_Z),          C(KC_Y),           C(KC_C),          C(KC_V),          CDX_ENCODER_PRESS,
        KC_HOME,          KC_UP,              KC_END,           KC_PAGE_UP,       KC_ENTER,
        KC_LEFT,          KC_DOWN,            KC_RIGHT,         KC_PAGE_DOWN,     LAYER_CYCLE,
        KC_ESCAPE,        KC_TAB,             KC_ENTER,         KC_BACKSPACE
    ),
    [_SYS] = LAYOUT(
        CDX_OPEN_APP,     CDX_OPEN_TERM,      CDX_OPEN_PROJECT, G(KC_D),          CDX_ENCODER_PRESS,
        CDX_BRIDGE_STATUS,CDX_RECONNECT,      CDX_VIEW_LOG,     CDX_ARCHIVE,      RM_TOGG,
        OLED_BRIGHTNESS_DOWN, OLED_BRIGHTNESS_UP, RM_VALD,     RM_VALU,          LAYER_CYCLE,
        KC_NUM_LOCK,      RM_TOGG,            RGB_SOLID_MODE,   BOOT_HOLD
    ),
};

/* Encoder hardware order from real-device testing: left, right, middle. */
#ifdef ENCODER_MAP_ENABLE
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [_CODEX] = {
        ENCODER_CCW_CW(CDX_ENCODER_CCW, CDX_ENCODER_CW),
        ENCODER_CCW_CW(CDX_REASON_DOWN, CDX_REASON_UP),
        ENCODER_CCW_CW(MS_WHLD, MS_WHLU),
    },
    [_NUM] = {
        ENCODER_CCW_CW(CDX_ENCODER_CCW, CDX_ENCODER_CW),
        ENCODER_CCW_CW(KC_LEFT, KC_RIGHT),
        ENCODER_CCW_CW(MS_WHLD, MS_WHLU),
    },
    [_NAV] = {
        ENCODER_CCW_CW(CDX_ENCODER_CCW, CDX_ENCODER_CW),
        ENCODER_CCW_CW(KC_LEFT, KC_RIGHT),
        ENCODER_CCW_CW(KC_UP, KC_DOWN),
    },
    [_SYS] = {
        ENCODER_CCW_CW(CDX_ENCODER_CCW, CDX_ENCODER_CW),
        ENCODER_CCW_CW(RM_PREV, RM_NEXT),
        ENCODER_CCW_CW(RM_VALD, RM_VALU),
    },
};
#endif

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case LAYER_CYCLE:
            if (record->event.pressed) {
                cycle_layer();
            }
            return false;

        case CDX_VOICE:
            if (record->event.pressed) {
                register_code(KC_LEFT_ALT);
                register_code(KC_SPACE);
            } else {
                unregister_code(KC_SPACE);
                unregister_code(KC_LEFT_ALT);
            }
            return false;

        case CDX_STOP:
            if (record->event.pressed) {
                start_guarded_action(&stop_action, "STOP");
            } else {
                cancel_guarded_action(&stop_action, "STOP");
            }
            return false;

        case CDX_ARCHIVE:
            if (record->event.pressed) {
                start_guarded_action(&archive_action, "ARCHIVE");
            } else {
                cancel_guarded_action(&archive_action, "ARCHIVE");
            }
            return false;

        case BOOT_HOLD:
            if (record->event.pressed) {
                start_guarded_action(&boot_action, "BOOTLOADER");
            } else {
                cancel_guarded_action(&boot_action, "BOOTLOADER");
            }
            return false;

        case OLED_BRIGHTNESS_DOWN:
        case OLED_BRIGHTNESS_UP:
            if (record->event.pressed) {
#ifdef OLED_ENABLE
                uint8_t brightness = oled_get_brightness();
                if (keycode == OLED_BRIGHTNESS_DOWN) {
                    brightness = brightness > OLED_BRIGHTNESS_STEP ? brightness - OLED_BRIGHTNESS_STEP : 0;
                } else {
                    brightness = brightness < (255 - OLED_BRIGHTNESS_STEP) ? brightness + OLED_BRIGHTNESS_STEP : 255;
                }
                oled_set_brightness(brightness);
#endif
            }
            return false;

        case RGB_SOLID_MODE:
            if (record->event.pressed) {
                uint8_t brightness = rgb_matrix_get_val();
                rgb_matrix_mode(RGB_MATRIX_SOLID_COLOR);
                rgb_matrix_sethsv(190, 44, brightness);
                oled_controller_show_popup("RGB", "COOL WHITE", OLED_POPUP_NORMAL_MS);
            }
            return false;

        case CDX_TASK_1:
        case CDX_TASK_2:
        case CDX_TASK_3:
        case CDX_TASK_4:
        case CDX_TASK_5:
        case CDX_TASK_6:
            codex_micro_send_agent_key(keycode - CDX_TASK_1, record->event.pressed);
            return false;

        case CDX_JOYSTICK_UP:
        case CDX_JOYSTICK_RIGHT:
        case CDX_JOYSTICK_DOWN:
        case CDX_JOYSTICK_LEFT:
            codex_micro_send_joystick_direction((codex_micro_joystick_direction_t)(keycode - CDX_JOYSTICK_UP), record->event.pressed);
            return false;

        case CDX_ACTION_06:
        case CDX_ACTION_07:
        case CDX_ACTION_08:
        case CDX_ACTION_09:
        case CDX_ACTION_12:
        case CDX_ACTION_MIC:
            codex_micro_send_action_key((codex_micro_action_t)(keycode - CDX_ACTION_06), record->event.pressed);
            return false;

        case CDX_ENCODER_CCW:
        case CDX_ENCODER_CW:
            if (record->event.pressed) {
                codex_micro_send_encoder_turn(keycode == CDX_ENCODER_CW);
            }
            return false;

        case CDX_ENCODER_PRESS:
            codex_micro_send_encoder_press(record->event.pressed);
            return false;

        case CDX_NEW_TASK:
        case CDX_CONTINUE:
        case CDX_FORK:
        case CDX_FOCUS_TASK:
        case CDX_FOCUS_INPUT:
        case CDX_APPROVE:
        case CDX_REJECT:
        case CDX_PLAN_MODE:
        case CDX_ADD_FILE:
        case CDX_COPY_REPLY:
        case CDX_OPEN_APP:
        case CDX_OPEN_TERM:
        case CDX_OPEN_PROJECT:
        case CDX_BRIDGE_STATUS:
        case CDX_RECONNECT:
        case CDX_VIEW_LOG:
        case CDX_SIDEBAR_CLOSE:
        case CDX_SIDEBAR_OPEN:
        case CDX_REASON_DOWN:
        case CDX_REASON_UP:
        case CDX_REASON_CONFIRM:
            if (record->event.pressed) {
                show_no_link();
            }
            return false;
    }

    return true;
}

void matrix_scan_user(void) {
    scan_no_link_guard(&stop_action);
    scan_no_link_guard(&archive_action);
    scan_boot_guard();
    codex_micro_task();
}

void keyboard_post_init_user(void) {
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
