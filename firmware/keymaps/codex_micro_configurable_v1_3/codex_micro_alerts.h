#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CODEX_MICRO_ALERT_NONE,
    CODEX_MICRO_ALERT_REMINDER,
    CODEX_MICRO_ALERT_RECONNECT,
    CODEX_MICRO_ALERT_COMPLETION,
    CODEX_MICRO_ALERT_APPROVAL,
    CODEX_MICRO_ALERT_ERROR,
    CODEX_MICRO_ALERT_COUNT,
} codex_micro_alert_t;

typedef struct {
    uint32_t color;
    uint8_t  amount;
} codex_micro_alert_sample_t;

void codex_micro_alerts_init(void);
bool codex_micro_alerts_queue(codex_micro_alert_t alert, uint8_t slot, uint32_t now, bool manual);
void codex_micro_alerts_tick(uint32_t now);
bool codex_micro_alerts_sample(uint8_t position, uint8_t origin_position, uint8_t led_count, uint32_t now, codex_micro_alert_sample_t *sample);
void codex_micro_alerts_cancel_preview(uint32_t now);
void codex_micro_alerts_acknowledge_completion(uint8_t slot, uint32_t now);
codex_micro_alert_t codex_micro_alerts_active(void);
uint8_t codex_micro_alerts_active_slot(void);
uint8_t codex_micro_alerts_pending_mask(void);

#ifdef CODEX_MICRO_HOST_TEST
uint32_t codex_micro_alerts_started_at(void);
#endif
