#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "codex_micro_protocol.h"
#include "oled_icons.h"

#define MAX_REPORTS 64

typedef struct {
    uint8_t data[CODEX_MICRO_REPORT_SIZE];
} captured_report_t;

layer_state_t layer_state;

static captured_report_t reports[MAX_REPORTS];
static uint8_t           report_count;
static uint32_t          fake_timer;
static uint8_t           colors[RGB_MATRIX_LED_COUNT][3];
static char              popup_line1[32];
static char              popup_line2[32];
static uint8_t           matrix_hue = 170;
static uint8_t           matrix_sat = 140;
static uint8_t           matrix_val = 120;

uint8_t get_highest_layer(layer_state_t state) {
    for (int8_t bit = 31; bit >= 0; --bit) {
        if ((state & (1UL << bit)) != 0) {
            return (uint8_t)bit;
        }
    }
    return 0;
}

uint32_t timer_read32(void) {
    return fake_timer;
}

void wait_ms(uint16_t milliseconds) {
    (void)milliseconds;
}

void test_set_color(uint8_t index, uint8_t red, uint8_t green, uint8_t blue) {
    assert(index < RGB_MATRIX_LED_COUNT);
    colors[index][0] = red;
    colors[index][1] = green;
    colors[index][2] = blue;
}

uint8_t rgb_matrix_get_hue(void) {
    return matrix_hue;
}

uint8_t rgb_matrix_get_sat(void) {
    return matrix_sat;
}

void rgb_matrix_sethsv_noeeprom(uint16_t hue, uint8_t sat, uint8_t val) {
    matrix_hue = (uint8_t)hue;
    matrix_sat = sat;
    matrix_val = val;
}

void oled_controller_show_popup(const char *line1, const char *line2, uint16_t duration_ms) {
    (void)duration_ms;
    snprintf(popup_line1, sizeof(popup_line1), "%s", line1);
    snprintf(popup_line2, sizeof(popup_line2), "%s", line2);
}

void oled_controller_render(void) {}

void raw_hid_send(uint8_t *data, uint8_t length) {
    assert(length == CODEX_MICRO_REPORT_SIZE);
    assert(report_count < MAX_REPORTS);
    memcpy(reports[report_count++].data, data, length);
}

void raw_hid_receive(uint8_t *data, uint8_t length);

static void clear_reports(void) {
    report_count = 0;
    memset(reports, 0, sizeof(reports));
}

static void feed_json(const char *json) {
    size_t length = strlen(json);
    size_t offset = 0;
    while (offset < length) {
        uint8_t report[CODEX_MICRO_REPORT_SIZE] = {0};
        size_t  chunk = length - offset;
        if (chunk > CODEX_MICRO_FRAGMENT_SIZE) {
            chunk = CODEX_MICRO_FRAGMENT_SIZE;
        }
        report[0] = CODEX_MICRO_REPORT_ID;
        report[1] = 2;
        report[2] = (uint8_t)chunk;
        memcpy(&report[3], json + offset, chunk);
        raw_hid_receive(report, sizeof(report));
        offset += chunk;
    }
}

static void joined_reports(char *target, size_t target_size) {
    size_t output = 0;
    for (uint8_t report = 0; report < report_count; ++report) {
        assert(reports[report].data[0] == CODEX_MICRO_REPORT_ID);
        assert(reports[report].data[1] == 2);
        uint8_t length = reports[report].data[2];
        assert(length <= CODEX_MICRO_FRAGMENT_SIZE);
        assert(output + length < target_size);
        memcpy(target + output, &reports[report].data[3], length);
        output += length;
    }
    target[output] = '\0';
}

static void expect_output_contains(const char *needle) {
    char output[1024];
    joined_reports(output, sizeof(output));
    assert(strstr(output, needle) != NULL);
}

int main(void) {
    codex_micro_init();
    layer_state = 1;
    assert(matrix_val == 0);

    memset(colors, 0xAA, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    for (uint8_t led = 0; led < RGB_MATRIX_LED_COUNT; ++led) {
        assert(colors[led][0] == 0 && colors[led][1] == 0 && colors[led][2] == 0);
    }

    clear_reports();
    feed_json("{\"method\":\"sys.version\",\"params\":{},\"id\":41}");
    expect_output_contains("\"id\":41");
    expect_output_contains("0.1.0-qmk");
    assert(strcmp(popup_line1, "CODEX MICRO") == 0);
    assert(strcmp(popup_line2, "CONNECTED") == 0);

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":16777215,\"b\":0.5,\"e\":\"solid\"},{\"id\":1,\"c\":255,\"b\":0.5,\"e\":\"breath\"},{\"id\":2,\"c\":16760576,\"b\":0.5,\"e\":\"solid\"},{\"id\":3,\"c\":65280,\"b\":0.5,\"e\":\"solid\"},{\"id\":4,\"c\":16777215,\"b\":0.5,\"e\":\"solid\"},{\"id\":5,\"c\":16777215,\"b\":0.5,\"e\":\"solid\"}],\"id\":42}");
    expect_output_contains("\"id\":42");
    expect_output_contains("\"ok\":true");

    fake_timer = 800;
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(matrix_val == 128);
    assert(colors[0][0] == 107 && colors[0][1] == 105 && colors[0][2] == 128);
    assert(colors[1][0] == 0 && colors[1][1] == 0 && colors[1][2] == 128);
    assert(colors[2][0] == 107 && colors[2][1] == 105 && colors[2][2] == 128);
    assert(colors[4][0] == 128 && colors[4][1] == 95 && colors[4][2] == 0);
    assert(colors[5][0] == 0 && colors[5][1] == 128 && colors[5][2] == 0);
    assert(colors[8][0] == colors[0][0] && colors[8][1] == colors[0][1] && colors[8][2] == colors[0][2]);
    assert(colors[9][0] == colors[0][0] && colors[9][1] == colors[0][1] && colors[9][2] == colors[0][2]);
    assert(colors[15][0] == colors[0][0] && colors[15][1] == colors[0][1] && colors[15][2] == colors[0][2]);

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"},{\"id\":1,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"},{\"id\":2,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"},{\"id\":3,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"},{\"id\":4,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"},{\"id\":5,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"}],\"id\":45}");
    assert(matrix_val == 204);
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] == 172 && colors[0][1] == 168 && colors[0][2] == 204);
    assert(colors[15][0] == colors[0][0] && colors[15][1] == colors[0][1] && colors[15][2] == colors[0][2]);

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":0,\"b\":0,\"e\":\"off\"}],\"id\":46}");
    assert(matrix_val == 204);
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[2][0] == 172 && colors[2][1] == 168 && colors[2][2] == 204);

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":0,\"b\":0,\"e\":\"off\"},{\"id\":1,\"c\":0,\"b\":0,\"e\":\"off\"},{\"id\":2,\"c\":0,\"b\":0,\"e\":\"off\"},{\"id\":3,\"c\":0,\"b\":0,\"e\":\"off\"},{\"id\":4,\"c\":0,\"b\":0,\"e\":\"off\"},{\"id\":5,\"c\":0,\"b\":0,\"e\":\"off\"}],\"id\":47}");
    assert(matrix_val == 0);
    for (uint8_t layer = 0; layer < 4; ++layer) {
        layer_state = 1UL << layer;
        memset(colors, 0xAA, sizeof(colors));
        assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
        for (uint8_t led = 0; led < RGB_MATRIX_LED_COUNT; ++led) {
            assert(colors[led][0] == 0 && colors[led][1] == 0 && colors[led][2] == 0);
        }
    }

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":5,\"c\":16777215,\"b\":0.2,\"e\":\"solid\"}],\"id\":48}");
    assert(matrix_val == 51);
    memset(colors, 0xAA, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] == 0xAA && colors[15][2] == 0xAA);
    layer_state = 1;
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] == 0 && colors[0][1] == 0 && colors[0][2] == 0);
    assert(colors[4][0] == 0 && colors[4][1] == 0 && colors[4][2] == 0);
    assert(colors[9][0] == 43 && colors[9][1] == 42 && colors[9][2] == 51);
    assert(colors[2][0] == 43 && colors[2][1] == 42 && colors[2][2] == 51);
    assert(colors[15][0] == 43 && colors[15][1] == 42 && colors[15][2] == 51);

    clear_reports();
    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":0,\"b\":0,\"e\":\"off\"},\"ambient\":{\"c\":0,\"b\":0,\"e\":\"off\"}},\"id\":49}");
    assert(matrix_val == 51);

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":16777215,\"b\":1,\"e\":\"solid\"},{\"id\":1,\"c\":16777215,\"b\":1,\"e\":\"solid\"},{\"id\":2,\"c\":16777215,\"b\":1,\"e\":\"solid\"},{\"id\":3,\"c\":16777215,\"b\":1,\"e\":\"solid\"},{\"id\":4,\"c\":16777215,\"b\":1,\"e\":\"solid\"},{\"id\":5,\"c\":16777215,\"b\":1,\"e\":\"solid\"}],\"id\":50}");
    assert(matrix_val == 255);
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] == 215 && colors[0][1] == 211 && colors[0][2] == 255);
    assert(colors[15][0] == colors[0][0] && colors[15][1] == colors[0][1] && colors[15][2] == colors[0][2]);

    clear_reports();
    codex_micro_send_agent_key(0, true);
    codex_micro_send_agent_key(0, false);
    expect_output_contains("\"k\":\"AG00\",\"act\":1,\"ag\":0");
    expect_output_contains("\"k\":\"AG00\",\"act\":0,\"ag\":0");

    clear_reports();
    codex_micro_send_agent_key(5, true);
    codex_micro_send_agent_key(5, false);
    expect_output_contains("\"k\":\"AG05\",\"act\":1,\"ag\":5");
    expect_output_contains("\"k\":\"AG05\",\"act\":0,\"ag\":5");

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":3,\"c\":0,\"b\":0,\"e\":\"off\"}],\"id\":51}");
    clear_reports();
    codex_micro_send_agent_key(3, true);
    codex_micro_send_agent_key(3, false);
    assert(report_count == 0);
    assert(strcmp(popup_line1, "EMPTY SLOT") == 0);

    clear_reports();
    codex_micro_send_agent_key(6, true);
    codex_micro_send_agent_key(6, false);
    assert(report_count == 0);

    static const char *const action_names[CODEX_MICRO_ACTION_COUNT] = {"ACT06", "ACT07", "ACT08", "ACT09", "ACT12", "ACT10"};
    for (uint8_t action = 0; action < CODEX_MICRO_ACTION_COUNT; ++action) {
        clear_reports();
        codex_micro_send_action_key((codex_micro_action_t)action, true);
        codex_micro_send_action_key((codex_micro_action_t)action, false);
        char pressed[48];
        char released[48];
        snprintf(pressed, sizeof(pressed), "\"k\":\"%s\",\"act\":1", action_names[action]);
        snprintf(released, sizeof(released), "\"k\":\"%s\",\"act\":0", action_names[action]);
        expect_output_contains(pressed);
        expect_output_contains(released);
    }

    clear_reports();
    codex_micro_send_encoder_turn(true);
    codex_micro_send_encoder_turn(false);
    expect_output_contains("\"k\":\"ENC_CW\",\"act\":2");
    expect_output_contains("\"k\":\"ENC_CC\",\"act\":2");

    clear_reports();
    codex_micro_send_encoder_press(true);
    codex_micro_send_encoder_press(false);
    expect_output_contains("\"k\":\"ENC_SW\",\"act\":1");
    expect_output_contains("\"k\":\"ENC_SW\",\"act\":0");

    clear_reports();
    codex_micro_send_joystick_direction(CODEX_MICRO_JOYSTICK_UP, true);
    expect_output_contains("\"a\":0.75,\"d\":1");

    clear_reports();
    codex_micro_send_joystick_direction(CODEX_MICRO_JOYSTICK_RIGHT, true);
    expect_output_contains("\"a\":0.00,\"d\":1");

    clear_reports();
    codex_micro_send_joystick_direction(CODEX_MICRO_JOYSTICK_RIGHT, false);
    expect_output_contains("\"a\":0.75,\"d\":1");

    clear_reports();
    codex_micro_send_joystick_direction(CODEX_MICRO_JOYSTICK_LEFT, true);
    expect_output_contains("\"a\":0.50,\"d\":1");

    clear_reports();
    codex_micro_send_joystick_direction(CODEX_MICRO_JOYSTICK_UP, false);
    assert(report_count == 0);

    clear_reports();
    codex_micro_send_joystick_direction(CODEX_MICRO_JOYSTICK_LEFT, false);
    expect_output_contains("\"a\":0.50,\"d\":0");

    clear_reports();
    feed_json("{\"method\":\"device.status\",\"params\":{},\"id\":43}");
    expect_output_contains("\"battery\":100");
    expect_output_contains("\"is_charging\":false");

    clear_reports();
    feed_json("{\"method\":\"unknown.method\",\"params\":{},\"id\":44}");
    expect_output_contains("\"code\":-32601");

    codex_micro_init();
    clear_reports();
    codex_micro_send_action_key(CODEX_MICRO_ACTION_06, true);
    codex_micro_send_action_key(CODEX_MICRO_ACTION_06, false);
    codex_micro_send_encoder_press(false);
    codex_micro_send_joystick_direction(CODEX_MICRO_JOYSTICK_DOWN, false);
    assert(report_count == 0);
    assert(strcmp(popup_line1, "NO LINK") == 0);

    puts("Codex Micro protocol tests passed");
    return 0;
}
