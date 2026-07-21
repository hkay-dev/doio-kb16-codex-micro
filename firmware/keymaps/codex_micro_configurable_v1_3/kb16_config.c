#include "kb16_config.h"

#include <string.h>

#ifdef CODEX_MICRO_HOST_TEST
static uint8_t host_eeprom[KB16_USER_STORAGE_SIZE];
static bool host_writes_fail;
void kb16_user_storage_read(void *target, uint32_t offset, uint32_t length) {
    memcpy(target, host_eeprom + offset, length);
}
void kb16_user_storage_write(const void *source, uint32_t offset, uint32_t length) {
    if (host_writes_fail) return;
    memcpy(host_eeprom + offset, source, length);
}
void kb16_config_host_clear_storage(void) {
    memset(host_eeprom, 0, sizeof(host_eeprom));
    host_writes_fail = false;
}
void kb16_config_host_fail_writes(bool fail) { host_writes_fail = fail; }
void kb16_config_host_corrupt_slot(uint8_t slot, uint16_t offset) {
    if (slot < 2 && offset < KB16_CONFIG_SLOT_SIZE) {
        host_eeprom[(uint16_t)slot * KB16_CONFIG_SLOT_SIZE + offset] ^= 0x5AU;
    }
}
void kb16_config_host_copy_storage(void *target, uint32_t offset, uint32_t length) {
    memcpy(target, host_eeprom + offset, length);
}
#else
#    include QMK_KEYBOARD_H
void kb16_user_storage_read(void *target, uint32_t offset, uint32_t length) {
    eeconfig_read_user_datablock(target, offset, length);
}
void kb16_user_storage_write(const void *source, uint32_t offset, uint32_t length) {
    eeconfig_update_user_datablock(source, offset, length);
}
#endif

#define KB16_CONFIG_MAGIC 0x3331424BU /* KB13, little endian */

typedef struct __attribute__((packed)) {
    uint32_t              magic;
    uint8_t               schema_version;
    uint8_t               reserved;
    uint16_t              payload_size;
    uint32_t              generation;
    uint32_t              crc32;
    kb16_config_payload_t payload;
} kb16_config_slot_t;

_Static_assert(sizeof(kb16_action_t) == 4, "action descriptor must remain stable");
_Static_assert(sizeof(kb16_config_payload_t) == KB16_CONFIG_PAYLOAD_SIZE, "configuration payload size changed");
_Static_assert(sizeof(kb16_config_slot_t) == KB16_CONFIG_SLOT_SIZE, "configuration slot size changed");

static kb16_config_payload_t active_config;
static uint32_t              active_generation;
static uint32_t              active_crc;
static uint8_t               active_slot;
static uint8_t               pressed_count;

#define KB16_DEF_KEY(key) {KB16_ACTION_KEYBOARD, 0, (key)}
#define KB16_DEF_CHORD(mods, key) {KB16_ACTION_KEYBOARD, (mods), (key)}
#define KB16_DEF_MOUSE(code) {KB16_ACTION_MOUSE, 0, (code)}
#define KB16_DEF_FIRMWARE(code) {KB16_ACTION_FIRMWARE, 0, (code)}

/* USB HID keyboard usage values. They intentionally match QMK basic keycodes. */
enum {
    HID_A = 0x04,
    HID_C = 0x06,
    HID_D = 0x07,
    HID_V = 0x19,
    HID_Y = 0x1C,
    HID_Z = 0x1D,
    HID_ENTER = 0x28,
    HID_ESCAPE = 0x29,
    HID_BACKSPACE = 0x2A,
    HID_TAB = 0x2B,
    HID_EQUAL = 0x2E,
    HID_NUM_LOCK = 0x53,
    HID_KP_SLASH = 0x54,
    HID_KP_ASTERISK = 0x55,
    HID_KP_MINUS = 0x56,
    HID_KP_PLUS = 0x57,
    HID_KP_ENTER = 0x58,
    HID_KP_1 = 0x59,
    HID_KP_2 = 0x5A,
    HID_KP_3 = 0x5B,
    HID_KP_4 = 0x5C,
    HID_KP_5 = 0x5D,
    HID_KP_6 = 0x5E,
    HID_KP_7 = 0x5F,
    HID_KP_8 = 0x60,
    HID_KP_9 = 0x61,
    HID_KP_0 = 0x62,
    HID_KP_DOT = 0x63,
    HID_RIGHT = 0x4F,
    HID_LEFT = 0x50,
    HID_DOWN = 0x51,
    HID_UP = 0x52,
    HID_HOME = 0x4A,
    HID_PAGE_UP = 0x4B,
    HID_END = 0x4D,
    HID_PAGE_DOWN = 0x4E,
};

static const kb16_config_payload_t default_config = {
    .native_keys = {
        KB16_NATIVE_AG00, KB16_NATIVE_AG01, KB16_NATIVE_JOY_UP, KB16_NATIVE_JOY_RIGHT,
        KB16_NATIVE_AG02, KB16_NATIVE_AG03, KB16_NATIVE_JOY_LEFT, KB16_NATIVE_JOY_DOWN,
        KB16_NATIVE_AG04, KB16_NATIVE_AG05, KB16_NATIVE_ACT06, KB16_NATIVE_ACT07,
        KB16_NATIVE_ACT08, KB16_NATIVE_ACT09, KB16_NATIVE_ACT12, KB16_NATIVE_MIC,
    },
    .codex_encoders = {
        {KB16_DEF_FIRMWARE(KB16_FIRMWARE_NO_LINK), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NO_LINK), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NO_LINK)},
        {KB16_DEF_MOUSE(KB16_MOUSE_WHEEL_DOWN), KB16_DEF_MOUSE(KB16_MOUSE_WHEEL_UP), KB16_DEF_FIRMWARE(KB16_FIRMWARE_LAYER_CYCLE)},
    },
    .layers = {
        {
            KB16_DEF_KEY(HID_KP_SLASH), KB16_DEF_KEY(HID_KP_ASTERISK), KB16_DEF_KEY(HID_KP_MINUS), KB16_DEF_KEY(HID_KP_PLUS),
            KB16_DEF_KEY(HID_KP_7), KB16_DEF_KEY(HID_KP_8), KB16_DEF_KEY(HID_KP_9), KB16_DEF_KEY(HID_KP_ENTER),
            KB16_DEF_KEY(HID_KP_4), KB16_DEF_KEY(HID_KP_5), KB16_DEF_KEY(HID_KP_6), KB16_DEF_KEY(HID_KP_DOT),
            KB16_DEF_KEY(HID_KP_1), KB16_DEF_KEY(HID_KP_2), KB16_DEF_KEY(HID_KP_3), KB16_DEF_KEY(HID_KP_0),
        },
        {
            KB16_DEF_CHORD(0x01, HID_Z), KB16_DEF_CHORD(0x01, HID_Y), KB16_DEF_CHORD(0x01, HID_C), KB16_DEF_CHORD(0x01, HID_V),
            KB16_DEF_KEY(HID_HOME), KB16_DEF_KEY(HID_UP), KB16_DEF_KEY(HID_END), KB16_DEF_KEY(HID_PAGE_UP),
            KB16_DEF_KEY(HID_LEFT), KB16_DEF_KEY(HID_DOWN), KB16_DEF_KEY(HID_RIGHT), KB16_DEF_KEY(HID_PAGE_DOWN),
            KB16_DEF_KEY(HID_ESCAPE), KB16_DEF_KEY(HID_TAB), KB16_DEF_KEY(HID_ENTER), KB16_DEF_KEY(HID_BACKSPACE),
        },
        {
            KB16_DEF_FIRMWARE(KB16_FIRMWARE_NO_LINK), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NO_LINK), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NO_LINK), KB16_DEF_CHORD(0x08, HID_D),
            KB16_DEF_FIRMWARE(KB16_FIRMWARE_NO_LINK), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NO_LINK), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NO_LINK), KB16_DEF_FIRMWARE(KB16_FIRMWARE_ARCHIVE_HOLD),
            KB16_DEF_FIRMWARE(KB16_FIRMWARE_OLED_BRIGHTNESS_DOWN), KB16_DEF_FIRMWARE(KB16_FIRMWARE_OLED_BRIGHTNESS_UP), KB16_DEF_FIRMWARE(KB16_FIRMWARE_RGB_VALUE_DOWN), KB16_DEF_FIRMWARE(KB16_FIRMWARE_RGB_VALUE_UP),
            KB16_DEF_KEY(HID_NUM_LOCK), KB16_DEF_FIRMWARE(KB16_FIRMWARE_RGB_TOGGLE), KB16_DEF_FIRMWARE(KB16_FIRMWARE_RGB_SOLID), KB16_DEF_FIRMWARE(KB16_FIRMWARE_BOOTLOADER_HOLD),
        },
    },
    .encoders = {
        {
            {KB16_DEF_FIRMWARE(KB16_FIRMWARE_NATIVE_ENCODER_CCW), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NATIVE_ENCODER_CW), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NATIVE_ENCODER_PRESS)},
            {KB16_DEF_KEY(HID_LEFT), KB16_DEF_KEY(HID_RIGHT), KB16_DEF_KEY(HID_EQUAL)},
            {KB16_DEF_MOUSE(KB16_MOUSE_WHEEL_DOWN), KB16_DEF_MOUSE(KB16_MOUSE_WHEEL_UP), KB16_DEF_FIRMWARE(KB16_FIRMWARE_LAYER_CYCLE)},
        },
        {
            {KB16_DEF_FIRMWARE(KB16_FIRMWARE_NATIVE_ENCODER_CCW), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NATIVE_ENCODER_CW), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NATIVE_ENCODER_PRESS)},
            {KB16_DEF_KEY(HID_LEFT), KB16_DEF_KEY(HID_RIGHT), KB16_DEF_KEY(HID_ENTER)},
            {KB16_DEF_KEY(HID_UP), KB16_DEF_KEY(HID_DOWN), KB16_DEF_FIRMWARE(KB16_FIRMWARE_LAYER_CYCLE)},
        },
        {
            {KB16_DEF_FIRMWARE(KB16_FIRMWARE_NATIVE_ENCODER_CCW), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NATIVE_ENCODER_CW), KB16_DEF_FIRMWARE(KB16_FIRMWARE_NATIVE_ENCODER_PRESS)},
            {KB16_DEF_FIRMWARE(KB16_FIRMWARE_RGB_MODE_PREVIOUS), KB16_DEF_FIRMWARE(KB16_FIRMWARE_RGB_MODE_NEXT), KB16_DEF_FIRMWARE(KB16_FIRMWARE_RGB_TOGGLE)},
            {KB16_DEF_FIRMWARE(KB16_FIRMWARE_RGB_VALUE_DOWN), KB16_DEF_FIRMWARE(KB16_FIRMWARE_RGB_VALUE_UP), KB16_DEF_FIRMWARE(KB16_FIRMWARE_LAYER_CYCLE)},
        },
    },
};

uint32_t kb16_config_crc32(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t index = 0; index < length; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return ~crc;
}

static bool action_valid(const kb16_action_t *action) {
    if (action->kind == KB16_ACTION_NONE) {
        return action->modifiers == 0 && action->code == 0;
    }
    if (action->kind == KB16_ACTION_KEYBOARD) {
        return action->code >= 0x04 && action->code <= 0xE7;
    }
    if (action->modifiers != 0) {
        return false;
    }
    if (action->kind == KB16_ACTION_CONSUMER) {
        return action->code >= KB16_CONSUMER_VOLUME_UP && action->code <= KB16_CONSUMER_PREVIOUS;
    }
    if (action->kind == KB16_ACTION_MOUSE) {
        return action->code >= KB16_MOUSE_BUTTON_1 && action->code <= KB16_MOUSE_WHEEL_RIGHT;
    }
    if (action->kind == KB16_ACTION_FIRMWARE) {
        return action->code >= KB16_FIRMWARE_LAYER_CYCLE && action->code <= KB16_FIRMWARE_NATIVE_ENCODER_PRESS;
    }
    return false;
}

bool kb16_config_validate(const kb16_config_payload_t *payload) {
    uint32_t native_mask = 0;
    for (uint8_t position = 0; position < KB16_CONFIG_KEY_COUNT; ++position) {
        uint8_t control = payload->native_keys[position];
        if (control >= KB16_NATIVE_COUNT || (native_mask & (1UL << control)) != 0) {
            return false;
        }
        native_mask |= 1UL << control;
    }
    if (native_mask != ((1UL << KB16_NATIVE_COUNT) - 1UL)) {
        return false;
    }
    for (uint8_t encoder = 0; encoder < 2; ++encoder) {
        for (uint8_t action = 0; action < KB16_CONFIG_ENCODER_ACTION_COUNT; ++action) {
            if (!action_valid(&payload->codex_encoders[encoder][action])) {
                return false;
            }
        }
    }
    for (uint8_t layer = 0; layer < KB16_CONFIG_CUSTOM_LAYER_COUNT; ++layer) {
        for (uint8_t position = 0; position < KB16_CONFIG_KEY_COUNT; ++position) {
            if (!action_valid(&payload->layers[layer][position])) {
                return false;
            }
        }
        for (uint8_t encoder = 0; encoder < KB16_CONFIG_ENCODER_COUNT; ++encoder) {
            for (uint8_t action = 0; action < KB16_CONFIG_ENCODER_ACTION_COUNT; ++action) {
                if (!action_valid(&payload->encoders[layer][encoder][action])) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool slot_valid(const kb16_config_slot_t *slot) {
    return slot->magic == KB16_CONFIG_MAGIC && slot->schema_version == KB16_CONFIG_SCHEMA_VERSION &&
           slot->payload_size == sizeof(slot->payload) && kb16_config_validate(&slot->payload) &&
           slot->crc32 == kb16_config_crc32(&slot->payload, sizeof(slot->payload));
}

static bool generation_newer(uint32_t first, uint32_t second) {
    return (int32_t)(first - second) > 0;
}

static void activate_slot(const kb16_config_slot_t *slot, uint8_t slot_index) {
    active_config = slot->payload;
    active_generation = slot->generation;
    active_crc = slot->crc32;
    active_slot = slot_index;
}

bool kb16_config_commit(const kb16_config_payload_t *payload) {
    if (payload == NULL || pressed_count != 0 || !kb16_config_validate(payload)) {
        return false;
    }
    kb16_config_slot_t slot = {
        .magic = KB16_CONFIG_MAGIC,
        .schema_version = KB16_CONFIG_SCHEMA_VERSION,
        .reserved = 0,
        .payload_size = sizeof(*payload),
        .generation = active_generation + 1U,
        .crc32 = kb16_config_crc32(payload, sizeof(*payload)),
        .payload = *payload,
    };
    uint8_t target = active_slot ^ 1U;
    kb16_user_storage_write(&slot, (uint32_t)target * sizeof(slot), sizeof(slot));
    kb16_config_slot_t verify;
    kb16_user_storage_read(&verify, (uint32_t)target * sizeof(verify), sizeof(verify));
    if (!slot_valid(&verify) || verify.generation != slot.generation) {
        return false;
    }
    activate_slot(&verify, target);
    return true;
}

void kb16_config_reset_defaults(void) {
    uint8_t erased[KB16_CONFIG_STORAGE_SIZE];
    memset(erased, 0, sizeof(erased));
    kb16_user_storage_write(erased, 0, sizeof(erased));

    pressed_count = 0;
    active_slot = 1;
    active_generation = 0;
    active_crc = 0;
    active_config = default_config;
    (void)kb16_config_commit(&default_config);
}

void kb16_config_init(void) {
    kb16_config_slot_t slots[2];
    kb16_user_storage_read(&slots[0], 0, sizeof(slots[0]));
    kb16_user_storage_read(&slots[1], sizeof(slots[0]), sizeof(slots[1]));
    bool first_valid = slot_valid(&slots[0]);
    bool second_valid = slot_valid(&slots[1]);
    pressed_count = 0;
    if (!first_valid && !second_valid) {
        kb16_config_reset_defaults();
    } else if (second_valid && (!first_valid || generation_newer(slots[1].generation, slots[0].generation))) {
        activate_slot(&slots[1], 1);
    } else {
        activate_slot(&slots[0], 0);
    }
}

const kb16_config_payload_t *kb16_config_get(void) {
    return &active_config;
}

const kb16_action_t *kb16_config_key_action(uint8_t layer, uint8_t position) {
    if (layer == 0 || layer >= KB16_CONFIG_LAYER_COUNT || position >= KB16_CONFIG_KEY_COUNT) {
        return NULL;
    }
    return &active_config.layers[layer - 1][position];
}

const kb16_action_t *kb16_config_encoder_action(uint8_t layer, uint8_t encoder, uint8_t action_index) {
    if (encoder >= KB16_CONFIG_ENCODER_COUNT || action_index >= KB16_CONFIG_ENCODER_ACTION_COUNT) {
        return NULL;
    }
    if (layer == 0) {
        if (encoder == 0) {
            return NULL;
        }
        return &active_config.codex_encoders[encoder - 1][action_index];
    }
    if (layer >= KB16_CONFIG_LAYER_COUNT) {
        return NULL;
    }
    return &active_config.encoders[layer - 1][encoder][action_index];
}

uint8_t kb16_config_native_at(uint8_t position) {
    return position < KB16_CONFIG_KEY_COUNT ? active_config.native_keys[position] : KB16_NATIVE_COUNT;
}

uint8_t kb16_config_find_native(uint8_t native_control) {
    for (uint8_t position = 0; position < KB16_CONFIG_KEY_COUNT; ++position) {
        if (active_config.native_keys[position] == native_control) {
            return position;
        }
    }
    return 0xFF;
}

uint32_t kb16_config_generation(void) {
    return active_generation;
}

uint32_t kb16_config_crc(void) {
    return active_crc;
}

void kb16_config_input_pressed(void) {
    if (pressed_count != UINT8_MAX) {
        ++pressed_count;
    }
}

void kb16_config_input_released(void) {
    if (pressed_count > 0) {
        --pressed_count;
    }
}

bool kb16_config_input_busy(void) {
    return pressed_count != 0;
}

#ifndef CODEX_MICRO_HOST_TEST
void eeconfig_init_user(void) {
    kb16_config_reset_defaults();
}
#endif
