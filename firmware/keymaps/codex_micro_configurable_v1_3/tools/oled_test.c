#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "codex_micro_menu.h"
#include "codex_micro_protocol.h"
#include "codex_micro_settings.h"
#include "oled_icons.h"

layer_state_t layer_state = 1;

static bool menu_active;
static uint16_t now;
static uint8_t raw_writes;
static codex_micro_settings_t settings;

uint8_t get_highest_layer(layer_state_t state) {
    for (int8_t layer = 31; layer >= 0; --layer) {
        if ((state & (1UL << layer)) != 0) return (uint8_t)layer;
    }
    return 0;
}
uint32_t timer_read32(void) { return now; }
uint16_t timer_read(void) { return now; }
uint16_t timer_elapsed(uint16_t last) { return (uint16_t)(now - last); }
void wait_ms(uint16_t milliseconds) { (void)milliseconds; }
void oled_clear(void) {}
void oled_set_cursor(uint8_t column, uint8_t row) { (void)column; (void)row; }
void oled_write_ln(const char *text, bool invert) { (void)text; (void)invert; }
void oled_write_raw_P(const char *data, uint16_t size) { (void)data; (void)size; ++raw_writes; }

bool codex_micro_menu_active(void) { return menu_active; }
void codex_micro_menu_lines(char lines[4][22]) {
    snprintf(lines[0], 22, "1/1 TEST");
    lines[1][0] = '\0';
    lines[2][0] = '\0';
    lines[3][0] = '\0';
}
const codex_micro_settings_t *codex_micro_settings_get(void) { return &settings; }
bool codex_micro_host_connected(void) { return false; }
int8_t codex_micro_selected_slot(void) { return -1; }
const char *codex_micro_alert_label(void) { return "READY"; }
uint8_t codex_micro_alert_slot(void) { return 0xFF; }
char codex_micro_slot_mark(uint8_t slot) { (void)slot; return '-'; }

int main(void) {
    memset(&settings, 0, sizeof(settings));
    oled_controller_render();
    assert(raw_writes == 1);

    menu_active = true;
    oled_controller_render();
    assert(raw_writes == 1);

    menu_active = false;
    oled_controller_render();
    assert(raw_writes == 2);

    puts("Codex Micro OLED tests passed");
    return 0;
}
