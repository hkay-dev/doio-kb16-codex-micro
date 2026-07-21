#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "codex_micro_settings.h"
#include "kb16_config.h"

static void assert_defaults(const codex_micro_settings_t *settings) {
    assert(settings->brightness_percent == 100);
    assert(settings->speed_percent == 100);
    assert(settings->background_percent == 0);
    assert(settings->reminder == CODEX_MICRO_REMINDER_30_SECONDS);
    assert(settings->completion_style == CODEX_MICRO_COMPLETION_DOUBLE);
    assert(settings->completion_repeats == 1);
    assert(settings->alert_layout == CODEX_MICRO_ALERT_LAYOUT_SLOT_FOCUS);
    assert(settings->approval_style == CODEX_MICRO_APPROVAL_DOUBLE);
    assert(settings->error_style == CODEX_MICRO_ERROR_CHASE);
    assert((settings->flags & CODEX_MICRO_SETTING_DASHBOARD) != 0);
}

int main(void) {
    kb16_config_host_clear_storage();
    codex_micro_settings_host_reset_write_count();
    codex_micro_settings_init();
    assert_defaults(codex_micro_settings_get());
    assert(codex_micro_settings_host_write_count() == 1);

    uint8_t config_before[KB16_CONFIG_STORAGE_SIZE];
    kb16_config_host_copy_storage(config_before, 0, sizeof(config_before));

    codex_micro_settings_t changed = *codex_micro_settings_get();
    changed.flags |= CODEX_MICRO_SETTING_NIGHT_MODE | CODEX_MICRO_SETTING_PERIMETER;
    changed.brightness_percent = 70;
    changed.speed_percent = 125;
    changed.background_percent = 10;
    changed.reminder = CODEX_MICRO_REMINDER_60_SECONDS;
    changed.completion_style = CODEX_MICRO_COMPLETION_WAVE;
    changed.completion_repeats = 9;
    changed.alert_layout = CODEX_MICRO_ALERT_LAYOUT_PERIMETER;
    changed.approval_style = CODEX_MICRO_APPROVAL_HEARTBEAT;
    changed.error_style = CODEX_MICRO_ERROR_SOLID;
    changed.oled_brightness = 200;
    assert(codex_micro_settings_save(&changed));
    assert(codex_micro_settings_host_write_count() == 2);

    uint8_t config_after[KB16_CONFIG_STORAGE_SIZE];
    kb16_config_host_copy_storage(config_after, 0, sizeof(config_after));
    assert(memcmp(config_before, config_after, sizeof(config_before)) == 0);

    codex_micro_settings_init();
    assert(memcmp(codex_micro_settings_get(), &changed, sizeof(changed)) == 0);
    assert(codex_micro_settings_reminder_ms(&changed) == 60000);

    codex_micro_settings_t invalid = changed;
    invalid.brightness_percent = 9;
    assert(!codex_micro_settings_save(&invalid));
    invalid = changed;
    invalid.speed_percent = 151;
    assert(!codex_micro_settings_save(&invalid));
    invalid = changed;
    invalid.background_percent = 31;
    assert(!codex_micro_settings_save(&invalid));
    invalid = changed;
    invalid.completion_repeats = 0;
    assert(!codex_micro_settings_save(&invalid));
    invalid.completion_repeats = CODEX_MICRO_COMPLETION_UNTIL_FOCUS + 1;
    assert(!codex_micro_settings_save(&invalid));
    invalid = changed;
    invalid.alert_layout = CODEX_MICRO_ALERT_LAYOUT_COUNT;
    assert(!codex_micro_settings_save(&invalid));

    codex_micro_settings_t unsaved = changed;
    unsaved.brightness_percent = 45;
    kb16_config_host_fail_writes(true);
    assert(!codex_micro_settings_save(&unsaved));
    assert(codex_micro_settings_get()->brightness_percent == changed.brightness_percent);
    assert(codex_micro_settings_host_write_count() == 2);
    kb16_config_host_fail_writes(false);

    codex_micro_settings_host_corrupt(4);
    codex_micro_settings_init();
    assert_defaults(codex_micro_settings_get());

    puts("Codex Micro settings tests passed");
    return 0;
}
