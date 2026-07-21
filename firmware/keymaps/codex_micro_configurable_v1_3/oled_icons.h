#pragma once

#include <stdint.h>

#define OLED_POPUP_NORMAL_MS 1200
#define OLED_POPUP_ERROR_MS 2500

void oled_controller_show_popup(const char *line1, const char *line2, uint16_t duration_ms);
void oled_controller_render(void);
