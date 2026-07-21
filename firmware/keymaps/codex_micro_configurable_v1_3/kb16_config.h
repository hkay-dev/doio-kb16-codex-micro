#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KB16_CONFIG_SCHEMA_VERSION 1
#define KB16_CONFIG_LAYER_COUNT 4
#define KB16_CONFIG_KEY_COUNT 16
#define KB16_CONFIG_ENCODER_COUNT 3
#define KB16_CONFIG_ENCODER_ACTION_COUNT 3
#define KB16_CONFIG_CUSTOM_LAYER_COUNT 3
#define KB16_CONFIG_PAYLOAD_SIZE 340
#define KB16_CONFIG_SLOT_SIZE 356
#define KB16_CONFIG_STORAGE_SIZE (KB16_CONFIG_SLOT_SIZE * 2)
#define KB16_USER_STORAGE_SIZE 728

typedef enum {
    KB16_NATIVE_AG00,
    KB16_NATIVE_AG01,
    KB16_NATIVE_JOY_UP,
    KB16_NATIVE_JOY_RIGHT,
    KB16_NATIVE_AG02,
    KB16_NATIVE_AG03,
    KB16_NATIVE_JOY_LEFT,
    KB16_NATIVE_JOY_DOWN,
    KB16_NATIVE_AG04,
    KB16_NATIVE_AG05,
    KB16_NATIVE_ACT06,
    KB16_NATIVE_ACT07,
    KB16_NATIVE_ACT08,
    KB16_NATIVE_ACT09,
    KB16_NATIVE_ACT12,
    KB16_NATIVE_MIC,
    KB16_NATIVE_COUNT,
} kb16_native_control_t;

typedef enum {
    KB16_ACTION_NONE,
    KB16_ACTION_KEYBOARD,
    KB16_ACTION_CONSUMER,
    KB16_ACTION_MOUSE,
    KB16_ACTION_FIRMWARE,
} kb16_action_kind_t;

typedef enum {
    KB16_ENCODER_CCW,
    KB16_ENCODER_CW,
    KB16_ENCODER_PRESS,
} kb16_encoder_action_index_t;

typedef enum {
    KB16_CONSUMER_VOLUME_UP = 1,
    KB16_CONSUMER_VOLUME_DOWN,
    KB16_CONSUMER_MUTE,
    KB16_CONSUMER_PLAY_PAUSE,
    KB16_CONSUMER_NEXT,
    KB16_CONSUMER_PREVIOUS,
} kb16_consumer_code_t;

typedef enum {
    KB16_MOUSE_BUTTON_1 = 1,
    KB16_MOUSE_BUTTON_2,
    KB16_MOUSE_BUTTON_3,
    KB16_MOUSE_BUTTON_4,
    KB16_MOUSE_BUTTON_5,
    KB16_MOUSE_WHEEL_UP,
    KB16_MOUSE_WHEEL_DOWN,
    KB16_MOUSE_WHEEL_LEFT,
    KB16_MOUSE_WHEEL_RIGHT,
} kb16_mouse_code_t;

typedef enum {
    KB16_FIRMWARE_LAYER_CYCLE = 1,
    KB16_FIRMWARE_LAYER_CODEX,
    KB16_FIRMWARE_LAYER_NUM,
    KB16_FIRMWARE_LAYER_NAV,
    KB16_FIRMWARE_LAYER_SYS,
    KB16_FIRMWARE_RGB_TOGGLE,
    KB16_FIRMWARE_RGB_MODE_PREVIOUS,
    KB16_FIRMWARE_RGB_MODE_NEXT,
    KB16_FIRMWARE_RGB_VALUE_DOWN,
    KB16_FIRMWARE_RGB_VALUE_UP,
    KB16_FIRMWARE_OLED_BRIGHTNESS_DOWN,
    KB16_FIRMWARE_OLED_BRIGHTNESS_UP,
    KB16_FIRMWARE_RGB_SOLID,
    KB16_FIRMWARE_BOOTLOADER_HOLD,
    KB16_FIRMWARE_NO_LINK,
    KB16_FIRMWARE_ARCHIVE_HOLD,
    KB16_FIRMWARE_NATIVE_ENCODER_CCW,
    KB16_FIRMWARE_NATIVE_ENCODER_CW,
    KB16_FIRMWARE_NATIVE_ENCODER_PRESS,
} kb16_firmware_code_t;

typedef struct __attribute__((packed)) {
    uint8_t  kind;
    uint8_t  modifiers;
    uint16_t code;
} kb16_action_t;

typedef struct __attribute__((packed)) {
    uint8_t       native_keys[KB16_CONFIG_KEY_COUNT];
    kb16_action_t codex_encoders[2][KB16_CONFIG_ENCODER_ACTION_COUNT];
    kb16_action_t layers[KB16_CONFIG_CUSTOM_LAYER_COUNT][KB16_CONFIG_KEY_COUNT];
    kb16_action_t encoders[KB16_CONFIG_CUSTOM_LAYER_COUNT][KB16_CONFIG_ENCODER_COUNT][KB16_CONFIG_ENCODER_ACTION_COUNT];
} kb16_config_payload_t;

void kb16_config_init(void);
void kb16_config_reset_defaults(void);
bool kb16_config_commit(const kb16_config_payload_t *payload);
bool kb16_config_validate(const kb16_config_payload_t *payload);

const kb16_config_payload_t *kb16_config_get(void);
const kb16_action_t *kb16_config_key_action(uint8_t layer, uint8_t position);
const kb16_action_t *kb16_config_encoder_action(uint8_t layer, uint8_t encoder, uint8_t action_index);
uint8_t kb16_config_native_at(uint8_t position);
uint8_t kb16_config_find_native(uint8_t native_control);
uint32_t kb16_config_generation(void);
uint32_t kb16_config_crc(void);

void kb16_config_input_pressed(void);
void kb16_config_input_released(void);
bool kb16_config_input_busy(void);

uint32_t kb16_config_crc32(const void *data, size_t length);

void kb16_user_storage_read(void *target, uint32_t offset, uint32_t length);
void kb16_user_storage_write(const void *source, uint32_t offset, uint32_t length);

#ifdef CODEX_MICRO_HOST_TEST
void kb16_config_host_clear_storage(void);
void kb16_config_host_fail_writes(bool fail);
void kb16_config_host_corrupt_slot(uint8_t slot, uint16_t offset);
void kb16_config_host_copy_storage(void *target, uint32_t offset, uint32_t length);
#endif
