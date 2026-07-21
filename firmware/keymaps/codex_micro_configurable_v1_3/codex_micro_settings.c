#include "codex_micro_settings.h"

#include <string.h>

#include "kb16_config.h"

#define CODEX_MICRO_SETTINGS_MAGIC 0x4C43U
#define CODEX_MICRO_SETTINGS_DEFAULT_FLAGS                                                                                  \
    (CODEX_MICRO_SETTING_COMPLETION_ENABLED | CODEX_MICRO_SETTING_APPROVAL_ENABLED | CODEX_MICRO_SETTING_ERROR_ENABLED | \
     CODEX_MICRO_SETTING_REMINDER_ENABLED | CODEX_MICRO_SETTING_DASHBOARD)

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  schema;
    uint8_t  flags;
    uint8_t  brightness_percent;
    uint8_t  speed_percent;
    uint8_t  background_percent;
    uint8_t  reminder;
    uint8_t  completion_style;
    uint8_t  approval_style;
    uint8_t  error_style;
    uint8_t  oled_brightness;
    uint8_t  reserved[2];
    uint16_t crc16;
} codex_micro_settings_record_t;

_Static_assert(sizeof(codex_micro_settings_record_t) == CODEX_MICRO_SETTINGS_SIZE, "settings record size changed");
_Static_assert(CODEX_MICRO_SETTINGS_OFFSET + CODEX_MICRO_SETTINGS_SIZE <= KB16_USER_STORAGE_SIZE, "settings exceed user storage");

static codex_micro_settings_t active_settings;

#ifdef CODEX_MICRO_HOST_TEST
static uint16_t host_write_count;
#endif

static uint16_t crc16_ccitt(const void *data, uint16_t length) {
    const uint8_t *bytes = data;
    uint16_t       crc   = 0xFFFFU;
    for (uint16_t index = 0; index < length; ++index) {
        crc ^= (uint16_t)bytes[index] << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0 ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void codex_micro_settings_defaults(codex_micro_settings_t *settings) {
    if (settings == NULL) {
        return;
    }
    *settings = (codex_micro_settings_t){
        .flags              = CODEX_MICRO_SETTINGS_DEFAULT_FLAGS,
        .brightness_percent = 100,
        .speed_percent      = 100,
        .background_percent = 0,
        .reminder           = CODEX_MICRO_REMINDER_30_SECONDS,
        .completion_style   = CODEX_MICRO_COMPLETION_DOUBLE,
        .completion_repeats = 1,
        .alert_layout       = CODEX_MICRO_ALERT_LAYOUT_SLOT_FOCUS,
        .approval_style     = CODEX_MICRO_APPROVAL_DOUBLE,
        .error_style        = CODEX_MICRO_ERROR_CHASE,
        .oled_brightness    = 128,
    };
}

bool codex_micro_settings_validate(const codex_micro_settings_t *settings) {
    return settings != NULL && settings->brightness_percent >= 10 && settings->brightness_percent <= 100 &&
           settings->speed_percent >= 50 && settings->speed_percent <= 150 && settings->background_percent <= 30 &&
           settings->reminder < CODEX_MICRO_REMINDER_COUNT && settings->completion_style < CODEX_MICRO_COMPLETION_STYLE_COUNT &&
           settings->completion_repeats >= 1 && settings->completion_repeats <= CODEX_MICRO_COMPLETION_UNTIL_FOCUS &&
           settings->alert_layout < CODEX_MICRO_ALERT_LAYOUT_COUNT &&
           settings->approval_style < CODEX_MICRO_APPROVAL_STYLE_COUNT && settings->error_style < CODEX_MICRO_ERROR_STYLE_COUNT;
}

static codex_micro_settings_record_t encode_record(const codex_micro_settings_t *settings) {
    codex_micro_settings_record_t record = {
        .magic              = CODEX_MICRO_SETTINGS_MAGIC,
        .schema             = CODEX_MICRO_SETTINGS_SCHEMA,
        .flags              = settings->flags,
        .brightness_percent = settings->brightness_percent,
        .speed_percent      = settings->speed_percent,
        .background_percent = settings->background_percent,
        .reminder           = settings->reminder,
        .completion_style   = settings->completion_style,
        .approval_style     = settings->approval_style,
        .error_style        = settings->error_style,
        .oled_brightness    = settings->oled_brightness,
        .reserved           = {settings->completion_repeats, settings->alert_layout},
        .crc16              = 0,
    };
    record.crc16 = crc16_ccitt(&record, sizeof(record) - sizeof(record.crc16));
    return record;
}

static bool decode_record(const codex_micro_settings_record_t *record, codex_micro_settings_t *settings) {
    if (record->magic != CODEX_MICRO_SETTINGS_MAGIC || record->schema != CODEX_MICRO_SETTINGS_SCHEMA ||
        record->crc16 != crc16_ccitt(record, sizeof(*record) - sizeof(record->crc16))) {
        return false;
    }
    codex_micro_settings_t decoded = {
        .flags              = record->flags,
        .brightness_percent = record->brightness_percent,
        .speed_percent      = record->speed_percent,
        .background_percent = record->background_percent,
        .reminder           = record->reminder,
        .completion_style   = record->completion_style,
        .completion_repeats = record->reserved[0] == 0 ? 1 : record->reserved[0],
        .alert_layout       = record->reserved[1],
        .approval_style     = record->approval_style,
        .error_style        = record->error_style,
        .oled_brightness    = record->oled_brightness,
    };
    if (!codex_micro_settings_validate(&decoded)) {
        return false;
    }
    *settings = decoded;
    return true;
}

void codex_micro_settings_init(void) {
    codex_micro_settings_record_t record;
    kb16_user_storage_read(&record, CODEX_MICRO_SETTINGS_OFFSET, sizeof(record));
    if (!decode_record(&record, &active_settings)) {
        codex_micro_settings_reset(true);
    }
}

const codex_micro_settings_t *codex_micro_settings_get(void) {
    return &active_settings;
}

bool codex_micro_settings_save(const codex_micro_settings_t *settings) {
    if (!codex_micro_settings_validate(settings)) {
        return false;
    }
    codex_micro_settings_record_t record = encode_record(settings);
    kb16_user_storage_write(&record, CODEX_MICRO_SETTINGS_OFFSET, sizeof(record));
    codex_micro_settings_record_t verify;
    codex_micro_settings_t        decoded;
    kb16_user_storage_read(&verify, CODEX_MICRO_SETTINGS_OFFSET, sizeof(verify));
    if (memcmp(&verify, &record, sizeof(record)) != 0 || !decode_record(&verify, &decoded)) {
        return false;
    }
    active_settings = decoded;
#ifdef CODEX_MICRO_HOST_TEST
    ++host_write_count;
#endif
    return true;
}

void codex_micro_settings_apply(const codex_micro_settings_t *settings) {
    if (codex_micro_settings_validate(settings)) {
        active_settings = *settings;
    }
}

void codex_micro_settings_reset(bool persist) {
    codex_micro_settings_defaults(&active_settings);
    if (persist) {
        codex_micro_settings_save(&active_settings);
    }
}

uint32_t codex_micro_settings_reminder_ms(const codex_micro_settings_t *settings) {
    static const uint32_t intervals[] = {0, 15000, 30000, 60000, 120000};
    if (settings == NULL || settings->reminder >= CODEX_MICRO_REMINDER_COUNT) {
        return 0;
    }
    return intervals[settings->reminder];
}

#ifdef CODEX_MICRO_HOST_TEST
uint16_t codex_micro_settings_host_write_count(void) {
    return host_write_count;
}

void codex_micro_settings_host_reset_write_count(void) {
    host_write_count = 0;
}

void codex_micro_settings_host_corrupt(uint8_t offset) {
    if (offset < CODEX_MICRO_SETTINGS_SIZE) {
        uint8_t value;
        kb16_user_storage_read(&value, CODEX_MICRO_SETTINGS_OFFSET + offset, 1);
        value ^= 0x5AU;
        kb16_user_storage_write(&value, CODEX_MICRO_SETTINGS_OFFSET + offset, 1);
    }
}
#endif
