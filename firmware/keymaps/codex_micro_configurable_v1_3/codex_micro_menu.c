#include "codex_micro_menu.h"

#include <stdio.h>
#include <string.h>

#include "codex_micro_protocol.h"
#include "codex_micro_settings.h"

typedef enum { PAGE_ROOT, PAGE_ALERTS, PAGE_AMBIENT, PAGE_PREVIEW, PAGE_DISPLAY, PAGE_LEGEND } menu_page_t;

static bool active;
static menu_page_t page;
static uint8_t cursor;
static codex_micro_settings_t opening;
static codex_micro_settings_t draft;
static bool reset_armed;
static bool save_failed;

static const char *on_off(bool value) { return value ? "ON" : "OFF"; }
static void apply(void) { codex_micro_settings_apply(&draft); }
static void toggle(uint8_t flag);
static uint8_t count(void) {
    static const uint8_t counts[] = {8, 10, 5, 11, 2, 2};
    return counts[page];
}
static void close_common(void) {
    active = false;
    page = PAGE_ROOT;
    cursor = 0;
    codex_micro_cancel_previews();
}

void codex_micro_menu_init(void) { active = false; page = PAGE_ROOT; cursor = 0; reset_armed = false; save_failed = false; }
bool codex_micro_menu_active(void) { return active; }
void codex_micro_menu_open(void) {
    opening = *codex_micro_settings_get();
    draft = opening;
    active = true;
    page = PAGE_ROOT;
    cursor = 0;
    reset_armed = false;
    save_failed = false;
}
bool codex_micro_menu_save_close(void) {
    if (!codex_micro_settings_save(&draft)) {
        page = PAGE_ROOT;
        cursor = 5;
        save_failed = true;
        return false;
    }
    close_common();
    return true;
}
void codex_micro_menu_cancel(void) { codex_micro_settings_apply(&opening); close_common(); }
void codex_micro_menu_navigate(bool clockwise) {
    codex_micro_cancel_previews();
    reset_armed = false;
    save_failed = false;
    cursor = clockwise ? (uint8_t)((cursor + 1U) % count()) : (uint8_t)((cursor + count() - 1U) % count());
}
static uint8_t step(uint8_t value, uint8_t minimum, uint8_t maximum, uint8_t amount, bool up) {
    if (up) return value > maximum - amount ? maximum : (uint8_t)(value + amount);
    return value < minimum + amount ? minimum : (uint8_t)(value - amount);
}
void codex_micro_menu_adjust(bool clockwise) {
    save_failed = false;
    switch (page) {
        case PAGE_ALERTS:
            if (cursor == 0) toggle(CODEX_MICRO_SETTING_COMPLETION_ENABLED);
            else if (cursor == 1) draft.completion_style = (draft.completion_style + (clockwise ? 1 : CODEX_MICRO_COMPLETION_STYLE_COUNT - 1)) % CODEX_MICRO_COMPLETION_STYLE_COUNT;
            else if (cursor == 2) draft.completion_repeats = clockwise ? (draft.completion_repeats == CODEX_MICRO_COMPLETION_UNTIL_FOCUS ? 1 : draft.completion_repeats + 1U) : (draft.completion_repeats == 1 ? CODEX_MICRO_COMPLETION_UNTIL_FOCUS : draft.completion_repeats - 1U);
            else if (cursor == 3) draft.alert_layout = (draft.alert_layout + (clockwise ? 1 : CODEX_MICRO_ALERT_LAYOUT_COUNT - 1)) % CODEX_MICRO_ALERT_LAYOUT_COUNT;
            else if (cursor == 4) toggle(CODEX_MICRO_SETTING_APPROVAL_ENABLED);
            else if (cursor == 5) draft.approval_style = (draft.approval_style + (clockwise ? 1 : CODEX_MICRO_APPROVAL_STYLE_COUNT - 1)) % CODEX_MICRO_APPROVAL_STYLE_COUNT;
            else if (cursor == 6) toggle(CODEX_MICRO_SETTING_ERROR_ENABLED);
            else if (cursor == 7) draft.error_style = (draft.error_style + (clockwise ? 1 : CODEX_MICRO_ERROR_STYLE_COUNT - 1)) % CODEX_MICRO_ERROR_STYLE_COUNT;
            else if (cursor == 8) draft.reminder = (draft.reminder + (clockwise ? 1 : CODEX_MICRO_REMINDER_COUNT - 1)) % CODEX_MICRO_REMINDER_COUNT;
            else toggle(CODEX_MICRO_SETTING_MUTED);
            break;
        case PAGE_AMBIENT:
            if (cursor == 0) toggle(CODEX_MICRO_SETTING_PERIMETER);
            else if (cursor == 1) draft.background_percent = step(draft.background_percent, 0, 30, 5, clockwise);
            else if (cursor == 2) draft.brightness_percent = step(draft.brightness_percent, 10, 100, 5, clockwise);
            else if (cursor == 3) draft.speed_percent = step(draft.speed_percent, 50, 150, 10, clockwise);
            else toggle(CODEX_MICRO_SETTING_NIGHT_MODE);
            break;
        case PAGE_DISPLAY:
            if (cursor == 0) draft.oled_brightness = step(draft.oled_brightness, 16, 255, 16, clockwise);
            else toggle(CODEX_MICRO_SETTING_DASHBOARD);
            break;
        default: break;
    }
    apply();
}
static void toggle(uint8_t flag) { draft.flags ^= flag; apply(); }
void codex_micro_menu_select(void) {
    if (!(page == PAGE_ROOT && cursor == 5)) save_failed = false;
    if (page == PAGE_ROOT) {
        if (cursor < 5) { page = (menu_page_t)(cursor + 1U); cursor = 0; return; }
        if (cursor == 5) { (void)codex_micro_menu_save_close(); return; }
        if (cursor == 6) { codex_micro_menu_cancel(); return; }
        if (!reset_armed) { reset_armed = true; return; }
        codex_micro_settings_defaults(&draft); apply(); reset_armed = false; return;
    }
    if (page == PAGE_PREVIEW) {
        if (cursor < 6) codex_micro_preview_effect((uint8_t)(cursor + 1U));
        else if (cursor < 10) codex_micro_preview_alert((uint8_t)(cursor - 3U));
        else codex_micro_preview_status_demo();
    }
}
void codex_micro_menu_back(void) {
    codex_micro_cancel_previews();
    if (page == PAGE_ROOT) codex_micro_menu_cancel();
    else { page = PAGE_ROOT; cursor = 0; }
}
static const char *completion_name(void) { static const char *v[] = {"SINGLE", "DOUBLE", "WAVE"}; return v[draft.completion_style]; }
static const char *approval_name(void) { static const char *v[] = {"DOUBLE", "HEART", "SNAKE"}; return v[draft.approval_style]; }
static const char *error_name(void) { static const char *v[] = {"CHASE", "PULSE", "SOLID"}; return v[draft.error_style]; }
void codex_micro_menu_lines(char lines[4][22]) {
    static const char *root[] = {"ALERTS", "AMBIENT", "PREVIEWS", "DISPLAY", "SLOT KEY", "SAVE & EXIT", "CANCEL", "RESET LIGHTS"};
    static const char *root_detail1[] = {"Done input error", "Area glow level", "Try every effect", "OLED and dashboard", "Meaning of symbols", "Store changes", "Discard changes", "Default menu"};
    static const char *root_detail2[] = {"reminders mute", "speed night mode", "nothing is saved", "", "I W C ! E - ?", "and close", "and close", "values only"};
    static const char *previews[] = {"SOLID", "SNAKE", "RAINBOW", "BREATH", "GRADIENT", "SOFT BREATH", "COMPLETE", "APPROVAL", "ERROR", "RECONNECT", "STATUS DEMO"};
    const char *name = "";
    const char *value = "";
    const char *context = "";
    char value_buffer[18];

    if (page == PAGE_ROOT) {
        if (cursor == 5 && save_failed) {
            name = "SAVE FAILED";
            value = "Hold Right retry";
            context = "Settings kept live";
        } else {
            name = cursor == 7 && reset_armed ? "CONFIRM RESET" : root[cursor];
            value = cursor == 7 && reset_armed ? "Press Right again" : root_detail1[cursor];
            context = cursor == 7 && reset_armed ? "or press M back" : root_detail2[cursor];
        }
    } else if (page == PAGE_PREVIEW) {
        name = previews[cursor];
        value = cursor == 10 ? "Shows 6 states" : "Press Right to run";
        context = cursor == 10 ? "Matches Slot Key" : "PREVIEW";
    } else if (page == PAGE_ALERTS) {
        if (cursor == 0) { name = "DONE ALERT"; value = on_off((draft.flags & CODEX_MICRO_SETTING_COMPLETION_ENABLED) != 0); context = "When a task ends"; }
        else if (cursor == 1) { name = "DONE STYLE"; value = completion_name(); context = "Board-wide effect"; }
        else if (cursor == 2) {
            name = "DONE REPEATS";
            if (draft.completion_repeats == CODEX_MICRO_COMPLETION_UNTIL_FOCUS) value = "UNTIL FOCUSED";
            else { snprintf(value_buffer, sizeof(value_buffer), "%u %s", draft.completion_repeats, draft.completion_repeats == 1 ? "TIME" : "TIMES"); value = value_buffer; }
            context = "Stops when opened";
        }
        else if (cursor == 3) {
            static const char *layouts[] = {"SLOT FOCUS", "FULL BOARD", "PERIMETER"};
            name = "ALERT LAYOUT"; value = layouts[draft.alert_layout]; context = "Shows source slot";
        }
        else if (cursor == 4) { name = "INPUT ALERT"; value = on_off((draft.flags & CODEX_MICRO_SETTING_APPROVAL_ENABLED) != 0); context = "When input needed"; }
        else if (cursor == 5) { name = "INPUT STYLE"; value = approval_name(); context = "Board-wide effect"; }
        else if (cursor == 6) { name = "ERROR ALERT"; value = on_off((draft.flags & CODEX_MICRO_SETTING_ERROR_ENABLED) != 0); context = "When a task fails"; }
        else if (cursor == 7) { name = "ERROR STYLE"; value = error_name(); context = "Board-wide effect"; }
        else if (cursor == 8) {
            static const char *reminders[] = {"OFF", "15 SECONDS", "30 SECONDS", "60 SECONDS", "120 SECONDS"};
            name = "REMINDER"; value = reminders[draft.reminder]; context = "Repeat until read";
        } else { name = "MUTE ALERTS"; value = on_off((draft.flags & CODEX_MICRO_SETTING_MUTED) != 0); context = "Agent keys stay on"; }
    } else if (page == PAGE_AMBIENT) {
        if (cursor == 0) { name = "LIGHTING AREA"; value = (draft.flags & CODEX_MICRO_SETTING_PERIMETER) ? "PERIMETER" : "FULL BOARD"; context = "Where ambient runs"; }
        else if (cursor == 1) { name = "IDLE GLOW"; snprintf(value_buffer, sizeof(value_buffer), "%u PERCENT", draft.background_percent); value = value_buffer; context = "Non-Agent keys"; }
        else if (cursor == 2) { name = "BRIGHTNESS"; snprintf(value_buffer, sizeof(value_buffer), "%u PERCENT", draft.brightness_percent); value = value_buffer; context = "Global light cap"; }
        else if (cursor == 3) { name = "SPEED"; snprintf(value_buffer, sizeof(value_buffer), "%u PERCENT", draft.speed_percent); value = value_buffer; context = "All animations"; }
        else { name = "NIGHT MODE"; value = on_off((draft.flags & CODEX_MICRO_SETTING_NIGHT_MODE) != 0); context = "35 percent cap"; }
    } else if (page == PAGE_DISPLAY) {
        if (cursor == 0) { name = "OLED LEVEL"; snprintf(value_buffer, sizeof(value_buffer), "%u OF 255", draft.oled_brightness); value = value_buffer; context = "Screen brightness"; }
        else { name = "DASHBOARD"; value = on_off((draft.flags & CODEX_MICRO_SETTING_DASHBOARD) != 0); context = "Codex status view"; }
    } else {
        if (cursor == 0) { name = "SLOT STATES"; value = "I IDLE  W WORK"; context = "C DONE  ! INPUT"; }
        else { name = "MORE STATES"; value = "E ERROR  - EMPTY"; context = "? OTHER"; }
    }

    snprintf(lines[0], 18, "%u/%u %s", cursor + 1U, count(), name);
    snprintf(lines[1], 18, "%s", value);
    snprintf(lines[2], 18, "%s", context);
    lines[3][0] = '\0';
}
