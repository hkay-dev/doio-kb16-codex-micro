#include "codex_micro_alerts.h"

#include <string.h>

#include "codex_micro_settings.h"

#define CODEX_MICRO_ALERT_GREEN 0x00FF4CU
#define CODEX_MICRO_ALERT_AMBER 0xFF6D00U
#define CODEX_MICRO_ALERT_RED 0xFF0033U
#define CODEX_MICRO_ALERT_BLUE 0x304FFEU
#define CODEX_MICRO_ALERT_NO_SLOT 0xFFU

static codex_micro_alert_t active_alert;
static uint8_t             active_slot;
static uint32_t            active_started_at;
static uint32_t            active_duration;
static bool                active_manual;
static uint8_t             pending_mask;
static uint8_t             pending_manual_mask;
static uint8_t             pending_slots[CODEX_MICRO_ALERT_COUNT];

static uint8_t smoothstep8(uint8_t value) {
    uint32_t squared = (uint32_t)value * value;
    return (uint8_t)((squared * (765U - 2U * value) + 32512U) / 65025U);
}

static uint8_t pulse8(uint8_t phase) {
    uint8_t triangle = phase < 128 ? (uint8_t)(phase * 2U) : (uint8_t)((255U - phase) * 2U);
    return smoothstep8(triangle);
}

static uint8_t alert_priority(codex_micro_alert_t alert) {
    return (uint8_t)alert;
}

static uint32_t scaled_duration(uint32_t duration) {
    uint8_t speed = codex_micro_settings_get()->speed_percent;
    return speed == 0 ? duration : duration * 100U / speed;
}

static uint32_t completion_cycle_duration(void) {
    const codex_micro_settings_t *settings = codex_micro_settings_get();
    if (settings->completion_style == CODEX_MICRO_COMPLETION_SINGLE) return scaled_duration(750);
    if (settings->completion_style == CODEX_MICRO_COMPLETION_WAVE) return scaled_duration(1600);
    return scaled_duration(1700);
}

static uint32_t duration_for(codex_micro_alert_t alert) {
    const codex_micro_settings_t *settings = codex_micro_settings_get();
    switch (alert) {
        case CODEX_MICRO_ALERT_COMPLETION:
            if (settings->completion_repeats == CODEX_MICRO_COMPLETION_UNTIL_FOCUS) return UINT32_MAX;
            return completion_cycle_duration() * settings->completion_repeats;
        case CODEX_MICRO_ALERT_APPROVAL:
            return scaled_duration(settings->approval_style == CODEX_MICRO_APPROVAL_HEARTBEAT ? 1800 : 1700);
        case CODEX_MICRO_ALERT_ERROR:
            return scaled_duration(settings->error_style == CODEX_MICRO_ERROR_SOLID ? 1500 : 2400);
        case CODEX_MICRO_ALERT_RECONNECT: return scaled_duration(1000);
        case CODEX_MICRO_ALERT_REMINDER: return scaled_duration(1000);
        default: return 0;
    }
}

static bool automatic_enabled(codex_micro_alert_t alert) {
    const codex_micro_settings_t *settings = codex_micro_settings_get();
    if ((settings->flags & CODEX_MICRO_SETTING_MUTED) != 0) {
        return false;
    }
    switch (alert) {
        case CODEX_MICRO_ALERT_COMPLETION: return (settings->flags & CODEX_MICRO_SETTING_COMPLETION_ENABLED) != 0;
        case CODEX_MICRO_ALERT_APPROVAL: return (settings->flags & CODEX_MICRO_SETTING_APPROVAL_ENABLED) != 0;
        case CODEX_MICRO_ALERT_ERROR: return (settings->flags & CODEX_MICRO_SETTING_ERROR_ENABLED) != 0;
        case CODEX_MICRO_ALERT_REMINDER:
            return (settings->flags & CODEX_MICRO_SETTING_REMINDER_ENABLED) != 0 && settings->reminder != CODEX_MICRO_REMINDER_OFF;
        case CODEX_MICRO_ALERT_RECONNECT: return true;
        default: return false;
    }
}

static void start_alert(codex_micro_alert_t alert, uint8_t slot, uint32_t now, bool manual) {
    active_alert      = alert;
    active_slot       = slot;
    active_started_at = now;
    active_duration   = duration_for(alert);
    active_manual     = manual;
}

void codex_micro_alerts_init(void) {
    active_alert      = CODEX_MICRO_ALERT_NONE;
    active_slot       = CODEX_MICRO_ALERT_NO_SLOT;
    active_started_at = 0;
    active_duration   = 0;
    active_manual     = false;
    pending_mask      = 0;
    pending_manual_mask = 0;
    memset(pending_slots, CODEX_MICRO_ALERT_NO_SLOT, sizeof(pending_slots));
}

bool codex_micro_alerts_queue(codex_micro_alert_t alert, uint8_t slot, uint32_t now, bool manual) {
    if (alert <= CODEX_MICRO_ALERT_NONE || alert >= CODEX_MICRO_ALERT_COUNT || (!manual && !automatic_enabled(alert))) {
        return false;
    }
    if (manual) {
        if (active_alert != CODEX_MICRO_ALERT_NONE && !active_manual) {
            pending_mask |= (uint8_t)(1U << active_alert);
            pending_manual_mask &= (uint8_t)~(1U << active_alert);
            pending_slots[active_alert] = active_slot;
        }
        start_alert(alert, slot, now, true);
        return true;
    }
    if (active_alert == alert) {
        if (active_slot != slot) {
            pending_mask |= (uint8_t)(1U << alert);
            pending_manual_mask &= (uint8_t)~(1U << alert);
            pending_slots[alert] = slot;
            return true;
        }
        return false;
    }
    if (active_alert == CODEX_MICRO_ALERT_NONE || alert_priority(alert) > alert_priority(active_alert)) {
        if (active_alert != CODEX_MICRO_ALERT_NONE && !active_manual) {
            pending_mask |= (uint8_t)(1U << active_alert);
            pending_slots[active_alert] = active_slot;
        }
        start_alert(alert, slot, now, manual);
        return true;
    }
    pending_mask |= (uint8_t)(1U << alert);
    pending_manual_mask &= (uint8_t)~(1U << alert);
    pending_slots[alert] = slot;
    return true;
}

static void start_next(uint32_t now) {
    for (int8_t alert = CODEX_MICRO_ALERT_COUNT - 1; alert > CODEX_MICRO_ALERT_NONE; --alert) {
        uint8_t mask = (uint8_t)(1U << alert);
        if ((pending_mask & mask) == 0) {
            continue;
        }
        pending_mask &= (uint8_t)~mask;
        bool manual = (pending_manual_mask & mask) != 0;
        pending_manual_mask &= (uint8_t)~mask;
        if (!manual && !automatic_enabled((codex_micro_alert_t)alert)) {
            pending_slots[alert] = CODEX_MICRO_ALERT_NO_SLOT;
            continue;
        }
        start_alert((codex_micro_alert_t)alert, pending_slots[alert], now, manual);
        pending_slots[alert] = CODEX_MICRO_ALERT_NO_SLOT;
        return;
    }
    active_alert  = CODEX_MICRO_ALERT_NONE;
    active_slot   = CODEX_MICRO_ALERT_NO_SLOT;
    active_manual = false;
}

void codex_micro_alerts_tick(uint32_t now) {
    if (active_alert != CODEX_MICRO_ALERT_NONE && now - active_started_at >= active_duration) {
        start_next(now);
    }
}

static uint8_t pulse_window(uint32_t elapsed, uint32_t start, uint32_t length) {
    if (elapsed < start || elapsed >= start + length) {
        return 0;
    }
    return pulse8((uint8_t)((elapsed - start) * 255U / length));
}

static uint8_t snake_amount(uint8_t position, uint8_t led_count, uint32_t elapsed, uint32_t duration, uint8_t laps) {
    uint16_t cycle    = led_count * 256U;
    uint16_t head     = (uint16_t)(((uint64_t)elapsed * cycle * laps / duration) % cycle);
    uint16_t led_at   = position * 256U;
    uint16_t distance = (uint16_t)((head + cycle - led_at) % cycle);
    uint16_t tail     = 6U * 256U;
    return distance >= tail ? 0 : smoothstep8((uint8_t)(255U - (uint32_t)distance * 255U / tail));
}

static void path_coordinates(uint8_t position, uint8_t *row, uint8_t *column) {
    *row = position / 4U;
    uint8_t offset = position % 4U;
    *column = (*row & 1U) != 0 ? (uint8_t)(3U - offset) : offset;
}

static uint8_t apply_alert_layout(uint8_t amount, uint8_t position, uint8_t origin_position, uint8_t led_count) {
    const codex_micro_settings_t *settings = codex_micro_settings_get();
    if (amount == 0 || origin_position >= led_count || settings->alert_layout == CODEX_MICRO_ALERT_LAYOUT_FULL_BOARD) return amount;

    uint8_t row, column, origin_row, origin_column;
    path_coordinates(position, &row, &column);
    path_coordinates(origin_position, &origin_row, &origin_column);
    if (settings->alert_layout == CODEX_MICRO_ALERT_LAYOUT_PERIMETER) {
        bool edge = row == 0 || row == 3 || column == 0 || column == 3;
        return edge || position == origin_position ? amount : 0;
    }

    uint8_t row_distance = row > origin_row ? row - origin_row : origin_row - row;
    uint8_t column_distance = column > origin_column ? column - origin_column : origin_column - column;
    uint8_t distance = row_distance + column_distance;
    static const uint8_t distance_scale[] = {255, 220, 175, 130, 90, 60, 40};
    uint8_t scaled = (uint8_t)((uint16_t)amount * distance_scale[distance] / 255U);
    if (position == origin_position && scaled > 0 && scaled < 220) scaled = 220;
    return scaled;
}

bool codex_micro_alerts_sample(uint8_t position, uint8_t origin_position, uint8_t led_count, uint32_t now, codex_micro_alert_sample_t *sample) {
    if (sample == NULL || active_alert == CODEX_MICRO_ALERT_NONE || led_count == 0) {
        return false;
    }
    uint32_t elapsed = now - active_started_at;
    uint8_t  amount  = 0;
    uint32_t color   = 0;
    const codex_micro_settings_t *settings = codex_micro_settings_get();
    switch (active_alert) {
        case CODEX_MICRO_ALERT_COMPLETION:
            color = CODEX_MICRO_ALERT_GREEN;
            {
                uint32_t cycle_duration = completion_cycle_duration();
                elapsed %= cycle_duration;
                if (settings->completion_style == CODEX_MICRO_COMPLETION_SINGLE) {
                    amount = pulse_window(elapsed, 0, cycle_duration);
                } else if (settings->completion_style == CODEX_MICRO_COMPLETION_WAVE) {
                    amount = snake_amount(position, led_count, elapsed, cycle_duration, 1);
                } else {
                    uint32_t pulse = scaled_duration(750);
                    uint32_t gap   = scaled_duration(200);
                    amount = pulse_window(elapsed, 0, pulse);
                    uint8_t second = pulse_window(elapsed, pulse + gap, pulse);
                    if (second > amount) {
                        amount = second;
                    }
                }
            }
            break;
        case CODEX_MICRO_ALERT_APPROVAL:
            color = CODEX_MICRO_ALERT_AMBER;
            if (settings->approval_style == CODEX_MICRO_APPROVAL_SNAKE) {
                amount = snake_amount(position, led_count, elapsed, active_duration, 1);
            } else if (settings->approval_style == CODEX_MICRO_APPROVAL_HEARTBEAT) {
                uint32_t short_pulse = scaled_duration(350);
                amount = pulse_window(elapsed, 0, short_pulse);
                uint8_t second = pulse_window(elapsed, scaled_duration(450), short_pulse);
                if (second > amount) {
                    amount = second;
                }
            } else {
                uint32_t pulse = scaled_duration(750);
                uint32_t gap   = scaled_duration(200);
                amount = pulse_window(elapsed, 0, pulse);
                uint8_t second = pulse_window(elapsed, pulse + gap, pulse);
                if (second > amount) {
                    amount = second;
                }
            }
            break;
        case CODEX_MICRO_ALERT_ERROR:
            color = CODEX_MICRO_ALERT_RED;
            if (settings->error_style == CODEX_MICRO_ERROR_CHASE) {
                amount = snake_amount(position, led_count, elapsed, active_duration, 2);
            } else if (settings->error_style == CODEX_MICRO_ERROR_PULSE) {
                uint32_t pulse = active_duration / 3U;
                amount = pulse_window(elapsed % pulse, 0, pulse);
            } else {
                uint32_t fade = scaled_duration(400);
                amount = elapsed + fade < active_duration ? 255 : (uint8_t)((active_duration - elapsed) * 255U / fade);
            }
            break;
        case CODEX_MICRO_ALERT_RECONNECT:
            color  = CODEX_MICRO_ALERT_BLUE;
            amount = snake_amount(position, led_count, elapsed, active_duration, 1);
            break;
        case CODEX_MICRO_ALERT_REMINDER:
            color  = CODEX_MICRO_ALERT_GREEN;
            amount = (uint8_t)(96U + pulse_window(elapsed, 0, active_duration) * 159U / 255U);
            break;
        default: return false;
    }
    sample->color  = color;
    sample->amount = apply_alert_layout(amount, position, origin_position, led_count);
    return true;
}

void codex_micro_alerts_cancel_preview(uint32_t now) {
    if (active_manual) {
        start_next(now);
    }
}

void codex_micro_alerts_acknowledge_completion(uint8_t slot, uint32_t now) {
    uint8_t mask = (uint8_t)(1U << CODEX_MICRO_ALERT_COMPLETION);
    if ((pending_mask & mask) != 0 && pending_slots[CODEX_MICRO_ALERT_COMPLETION] == slot) {
        pending_mask &= (uint8_t)~mask;
        pending_manual_mask &= (uint8_t)~mask;
        pending_slots[CODEX_MICRO_ALERT_COMPLETION] = CODEX_MICRO_ALERT_NO_SLOT;
    }
    if (active_alert == CODEX_MICRO_ALERT_COMPLETION && active_slot == slot) start_next(now);
}

codex_micro_alert_t codex_micro_alerts_active(void) {
    return active_alert;
}

uint8_t codex_micro_alerts_active_slot(void) {
    return active_slot;
}

uint8_t codex_micro_alerts_pending_mask(void) {
    return pending_mask;
}

#ifdef CODEX_MICRO_HOST_TEST
uint32_t codex_micro_alerts_started_at(void) {
    return active_started_at;
}
#endif
