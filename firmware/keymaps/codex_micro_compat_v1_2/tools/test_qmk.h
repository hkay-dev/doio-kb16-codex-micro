#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t layer_state_t;

extern layer_state_t layer_state;

#define RGB_MATRIX_LED_COUNT 16

uint8_t  get_highest_layer(layer_state_t state);
uint32_t timer_read32(void);
void     wait_ms(uint16_t milliseconds);
void     test_set_color(uint8_t index, uint8_t red, uint8_t green, uint8_t blue);
uint8_t  rgb_matrix_get_hue(void);
uint8_t  rgb_matrix_get_sat(void);
void     rgb_matrix_sethsv_noeeprom(uint16_t hue, uint8_t sat, uint8_t val);

#define RGB_MATRIX_INDICATOR_SET_COLOR(index, red, green, blue) test_set_color((index), (red), (green), (blue))
