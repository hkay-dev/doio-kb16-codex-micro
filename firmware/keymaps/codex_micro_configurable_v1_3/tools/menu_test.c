#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "codex_micro_alerts.h"
#include "codex_micro_menu.h"
#include "codex_micro_settings.h"
#include "kb16_config.h"

static uint8_t preview_effect;
void codex_micro_preview_effect(uint8_t effect) { preview_effect = effect; }
void codex_micro_preview_alert(uint8_t alert) { (void)alert; }
void codex_micro_preview_status_demo(void) { preview_effect = 99; }
void codex_micro_cancel_previews(void) { preview_effect = 0; }

static void assert_safe_lines(char lines[4][22]) {
    for (uint8_t row = 0; row < 4; ++row) assert(strlen(lines[row]) <= 17);
}

int main(void) {
    char lines[4][22];
    kb16_config_host_clear_storage();
    codex_micro_settings_init();
    codex_micro_menu_init();
    codex_micro_settings_host_reset_write_count();

    codex_micro_menu_open();
    assert(codex_micro_menu_active());
    codex_micro_menu_lines(lines);
    assert(strcmp(lines[0], "1/8 ALERTS") == 0);
    assert_safe_lines(lines);
    codex_micro_menu_navigate(true);
    codex_micro_menu_select();
    codex_micro_menu_lines(lines);
    assert(strcmp(lines[0], "1/5 LIGHTING AREA") == 0);
    assert_safe_lines(lines);
    codex_micro_menu_adjust(true);
    assert((codex_micro_settings_get()->flags & CODEX_MICRO_SETTING_PERIMETER) != 0);
    codex_micro_menu_navigate(true);
    codex_micro_menu_adjust(true);
    assert(codex_micro_settings_get()->background_percent == 5);
    codex_micro_menu_back();
    for (uint8_t i = 0; i < 5; ++i) codex_micro_menu_navigate(true);
    codex_micro_menu_select();
    assert(!codex_micro_menu_active());
    assert(codex_micro_settings_host_write_count() == 1);

    codex_micro_menu_open();
    codex_micro_menu_select();
    codex_micro_menu_navigate(true);
    codex_micro_menu_navigate(true);
    codex_micro_menu_adjust(false);
    codex_micro_menu_lines(lines);
    assert(strcmp(lines[0], "3/10 DONE REPEATS") == 0);
    assert(strcmp(lines[1], "UNTIL FOCUSED") == 0);
    assert(codex_micro_settings_get()->completion_repeats == CODEX_MICRO_COMPLETION_UNTIL_FOCUS);
    codex_micro_menu_navigate(true);
    codex_micro_menu_adjust(true);
    codex_micro_menu_lines(lines);
    assert(strcmp(lines[0], "4/10 ALERT LAYOUT") == 0);
    assert(strcmp(lines[1], "FULL BOARD") == 0);
    codex_micro_menu_cancel();

    codex_micro_menu_open();
    for (uint8_t i = 0; i < 4; ++i) codex_micro_menu_navigate(true);
    codex_micro_menu_select();
    codex_micro_menu_lines(lines);
    assert(strcmp(lines[0], "1/2 SLOT STATES") == 0);
    assert(strstr(lines[1], "I IDLE") != NULL);
    assert_safe_lines(lines);
    codex_micro_menu_cancel();

    codex_micro_menu_open();
    codex_micro_menu_navigate(true);
    codex_micro_menu_navigate(true);
    codex_micro_menu_select();
    codex_micro_menu_navigate(true);
    codex_micro_menu_select();
    assert(preview_effect == 2);
    codex_micro_menu_cancel();
    assert(codex_micro_settings_host_write_count() == 1);

    codex_micro_menu_open();
    for (uint8_t i = 0; i < 7; ++i) codex_micro_menu_navigate(true);
    codex_micro_menu_select();
    codex_micro_menu_lines(lines);
    assert(strstr(lines[1], "Right again") != NULL);
    assert_safe_lines(lines);
    codex_micro_menu_select();
    assert(codex_micro_settings_get()->brightness_percent == 100);
    codex_micro_menu_cancel();

    puts("Codex Micro menu tests passed");
    return 0;
}
