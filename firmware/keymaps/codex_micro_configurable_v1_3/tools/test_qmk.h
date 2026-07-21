#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t layer_state_t;

extern layer_state_t layer_state;

#define RGB_MATRIX_LED_COUNT 16
#define RGB_MATRIX_SOLID_COLOR 1
#define RGB_MATRIX_EFFECT_MAX 8
#define PROGMEM

uint8_t  get_highest_layer(layer_state_t state);
uint32_t timer_read32(void);
uint16_t timer_read(void);
uint16_t timer_elapsed(uint16_t last);
void     wait_ms(uint16_t milliseconds);
void     oled_clear(void);
void     oled_set_cursor(uint8_t column, uint8_t row);
void     oled_write_ln(const char *text, bool invert);
void     oled_write_raw_P(const char *data, uint16_t size);
void     test_set_color(uint8_t index, uint8_t red, uint8_t green, uint8_t blue);
uint8_t  rgb_matrix_get_hue(void);
uint8_t  rgb_matrix_get_sat(void);
uint8_t  rgb_matrix_get_val(void);
uint8_t  rgb_matrix_get_mode(void);
uint8_t  rgb_matrix_is_enabled(void);
void     rgb_matrix_sethsv_noeeprom(uint16_t hue, uint8_t sat, uint8_t val);
void     rgb_matrix_sethsv(uint16_t hue, uint8_t sat, uint8_t val);
void     rgb_matrix_mode_noeeprom(uint8_t mode);
void     rgb_matrix_mode(uint8_t mode);
void     rgb_matrix_enable_noeeprom(void);
void     rgb_matrix_enable(void);
void     rgb_matrix_disable_noeeprom(void);

#define RGB_MATRIX_INDICATOR_SET_COLOR(index, red, green, blue) test_set_color((index), (red), (green), (blue))
