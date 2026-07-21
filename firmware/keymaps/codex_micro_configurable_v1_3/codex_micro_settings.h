#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CODEX_MICRO_SETTINGS_OFFSET 712U
#define CODEX_MICRO_SETTINGS_SIZE 16U
#define CODEX_MICRO_SETTINGS_SCHEMA 1U
#define CODEX_MICRO_COMPLETION_UNTIL_FOCUS 10U

#define CODEX_MICRO_SETTING_COMPLETION_ENABLED (1U << 0)
#define CODEX_MICRO_SETTING_APPROVAL_ENABLED (1U << 1)
#define CODEX_MICRO_SETTING_ERROR_ENABLED (1U << 2)
#define CODEX_MICRO_SETTING_REMINDER_ENABLED (1U << 3)
#define CODEX_MICRO_SETTING_MUTED (1U << 4)
#define CODEX_MICRO_SETTING_NIGHT_MODE (1U << 5)
#define CODEX_MICRO_SETTING_PERIMETER (1U << 6)
#define CODEX_MICRO_SETTING_DASHBOARD (1U << 7)

typedef enum {
    CODEX_MICRO_COMPLETION_SINGLE,
    CODEX_MICRO_COMPLETION_DOUBLE,
    CODEX_MICRO_COMPLETION_WAVE,
    CODEX_MICRO_COMPLETION_STYLE_COUNT,
} codex_micro_completion_style_t;

typedef enum {
    CODEX_MICRO_APPROVAL_DOUBLE,
    CODEX_MICRO_APPROVAL_HEARTBEAT,
    CODEX_MICRO_APPROVAL_SNAKE,
    CODEX_MICRO_APPROVAL_STYLE_COUNT,
} codex_micro_approval_style_t;

typedef enum {
    CODEX_MICRO_ERROR_CHASE,
    CODEX_MICRO_ERROR_PULSE,
    CODEX_MICRO_ERROR_SOLID,
    CODEX_MICRO_ERROR_STYLE_COUNT,
} codex_micro_error_style_t;

typedef enum {
    CODEX_MICRO_REMINDER_OFF,
    CODEX_MICRO_REMINDER_15_SECONDS,
    CODEX_MICRO_REMINDER_30_SECONDS,
    CODEX_MICRO_REMINDER_60_SECONDS,
    CODEX_MICRO_REMINDER_120_SECONDS,
    CODEX_MICRO_REMINDER_COUNT,
} codex_micro_reminder_t;

typedef enum {
    CODEX_MICRO_ALERT_LAYOUT_SLOT_FOCUS,
    CODEX_MICRO_ALERT_LAYOUT_FULL_BOARD,
    CODEX_MICRO_ALERT_LAYOUT_PERIMETER,
    CODEX_MICRO_ALERT_LAYOUT_COUNT,
} codex_micro_alert_layout_t;

typedef struct {
    uint8_t flags;
    uint8_t brightness_percent;
    uint8_t speed_percent;
    uint8_t background_percent;
    uint8_t reminder;
    uint8_t completion_style;
    uint8_t completion_repeats;
    uint8_t alert_layout;
    uint8_t approval_style;
    uint8_t error_style;
    uint8_t oled_brightness;
} codex_micro_settings_t;

void codex_micro_settings_init(void);
void codex_micro_settings_defaults(codex_micro_settings_t *settings);
const codex_micro_settings_t *codex_micro_settings_get(void);
bool codex_micro_settings_validate(const codex_micro_settings_t *settings);
bool codex_micro_settings_save(const codex_micro_settings_t *settings);
void codex_micro_settings_apply(const codex_micro_settings_t *settings);
void codex_micro_settings_reset(bool persist);
uint32_t codex_micro_settings_reminder_ms(const codex_micro_settings_t *settings);

#ifdef CODEX_MICRO_HOST_TEST
uint16_t codex_micro_settings_host_write_count(void);
void codex_micro_settings_host_reset_write_count(void);
void codex_micro_settings_host_corrupt(uint8_t offset);
#endif
