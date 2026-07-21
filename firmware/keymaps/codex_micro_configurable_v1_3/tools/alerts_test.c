#include <assert.h>
#include <stdio.h>

#include "codex_micro_alerts.h"
#include "codex_micro_settings.h"
#include "kb16_config.h"

int main(void) {
    kb16_config_host_clear_storage();
    codex_micro_settings_init();
    codex_micro_alerts_init();

    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 2, 100, false));
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_COMPLETION);
    uint32_t completion_started = codex_micro_alerts_started_at();
    assert(!codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 2, 200, false));
    assert(codex_micro_alerts_started_at() == completion_started);

    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_REMINDER, 1, 250, false));
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_COMPLETION);
    assert((codex_micro_alerts_pending_mask() & (1U << CODEX_MICRO_ALERT_REMINDER)) != 0);

    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_APPROVAL, 3, 300, false));
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_APPROVAL);
    assert((codex_micro_alerts_pending_mask() & (1U << CODEX_MICRO_ALERT_COMPLETION)) != 0);

    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_ERROR, 4, 400, false));
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_ERROR);
    assert(codex_micro_alerts_active_slot() == 4);
    assert((codex_micro_alerts_pending_mask() & (1U << CODEX_MICRO_ALERT_APPROVAL)) != 0);

    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 1, 450, true));
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_COMPLETION);
    assert(codex_micro_alerts_active_slot() == 1);
    codex_micro_alerts_cancel_preview(451);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_ERROR);
    assert(codex_micro_alerts_active_slot() == 4);

    codex_micro_alert_sample_t sample;
    assert(codex_micro_alerts_sample(0, 0xFF, 16, 700, &sample));
    assert(sample.color == 0xFF0033U);

    codex_micro_alerts_tick(3000);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_APPROVAL);
    codex_micro_alerts_tick(4800);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_COMPLETION);
    codex_micro_alerts_tick(6600);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_REMINDER);
    codex_micro_alerts_tick(7700);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_NONE);

    codex_micro_settings_t changed = *codex_micro_settings_get();
    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 0, 7800, false));
    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_REMINDER, 0, 7801, false));
    changed.flags |= CODEX_MICRO_SETTING_MUTED;
    codex_micro_settings_apply(&changed);
    codex_micro_alerts_tick(9501);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_NONE);
    assert(!codex_micro_alerts_queue(CODEX_MICRO_ALERT_ERROR, 0, 10000, false));
    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_ERROR, 0, 10000, true));
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_ERROR);

    changed.flags &= (uint8_t)~CODEX_MICRO_SETTING_MUTED;
    changed.completion_style = CODEX_MICRO_COMPLETION_WAVE;
    codex_micro_settings_apply(&changed);
    codex_micro_alerts_init();
    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 0, 11000, true));
    codex_micro_alert_sample_t first;
    codex_micro_alert_sample_t distant;
    assert(codex_micro_alerts_sample(0, 0, 16, 11000, &first));
    assert(codex_micro_alerts_sample(8, 0, 16, 11000, &distant));
    assert(first.amount > distant.amount);

    changed.completion_style = CODEX_MICRO_COMPLETION_SINGLE;
    changed.completion_repeats = 3;
    codex_micro_settings_apply(&changed);
    codex_micro_alerts_init();
    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 1, 12000, false));
    assert(codex_micro_alerts_sample(1, 1, 16, 12375, &first));
    assert(codex_micro_alerts_sample(1, 1, 16, 13125, &distant));
    assert(first.amount == distant.amount);
    assert(first.amount > 240);
    codex_micro_alerts_tick(14249);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_COMPLETION);
    codex_micro_alerts_tick(14250);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_NONE);

    changed.completion_repeats = CODEX_MICRO_COMPLETION_UNTIL_FOCUS;
    codex_micro_settings_apply(&changed);
    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 1, 15000, false));
    assert(codex_micro_alerts_sample(1, 1, 16, 15375, &first));
    assert(codex_micro_alerts_sample(1, 1, 16, 90375, &distant));
    assert(first.amount == distant.amount);
    assert(first.amount > 240);
    codex_micro_alerts_tick(115000);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_COMPLETION);
    codex_micro_alerts_acknowledge_completion(1, 115001);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_NONE);

    changed.completion_repeats = 1;
    changed.alert_layout = CODEX_MICRO_ALERT_LAYOUT_SLOT_FOCUS;
    codex_micro_settings_apply(&changed);
    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 0, 120000, false));
    assert(codex_micro_alerts_sample(0, 0, 16, 120375, &first));
    assert(codex_micro_alerts_sample(15, 0, 16, 120375, &distant));
    assert(first.amount > distant.amount);
    codex_micro_alerts_acknowledge_completion(0, 120376);

    changed.alert_layout = CODEX_MICRO_ALERT_LAYOUT_PERIMETER;
    codex_micro_settings_apply(&changed);
    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 0, 121000, false));
    assert(codex_micro_alerts_sample(6, 0, 16, 121375, &distant));
    assert(distant.amount == 0);

    changed.alert_layout = CODEX_MICRO_ALERT_LAYOUT_FULL_BOARD;
    changed.completion_style = CODEX_MICRO_COMPLETION_DOUBLE;
    codex_micro_settings_apply(&changed);
    codex_micro_alerts_init();
    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 2, 130000, false));
    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 5, 130001, false));
    assert((codex_micro_alerts_pending_mask() & (1U << CODEX_MICRO_ALERT_COMPLETION)) != 0);
    codex_micro_alerts_tick(131700);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_COMPLETION);
    assert(codex_micro_alerts_active_slot() == 5);
    codex_micro_alerts_tick(133400);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_NONE);

    changed.completion_repeats = CODEX_MICRO_COMPLETION_UNTIL_FOCUS;
    codex_micro_settings_apply(&changed);
    assert(codex_micro_alerts_queue(CODEX_MICRO_ALERT_COMPLETION, 1, 140000, false));
    changed.flags |= CODEX_MICRO_SETTING_MUTED;
    codex_micro_settings_apply(&changed);
    codex_micro_alerts_tick(140001);
    assert(codex_micro_alerts_active() == CODEX_MICRO_ALERT_NONE);

    puts("Codex Micro alert tests passed");
    return 0;
}
