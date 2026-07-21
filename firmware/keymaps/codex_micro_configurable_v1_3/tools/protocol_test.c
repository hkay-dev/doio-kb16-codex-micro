#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "codex_micro_protocol.h"
#include "codex_micro_settings.h"
#include "kb16_config.h"
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
static uint8_t           matrix_mode = RGB_MATRIX_SOLID_COLOR;
static uint8_t           matrix_enabled = 1;
static uint8_t           matrix_eeprom_writes;

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

uint8_t rgb_matrix_get_val(void) {
    return matrix_val;
}

uint8_t rgb_matrix_get_mode(void) {
    return matrix_mode;
}

uint8_t rgb_matrix_is_enabled(void) {
    return matrix_enabled;
}

void rgb_matrix_sethsv_noeeprom(uint16_t hue, uint8_t sat, uint8_t val) {
    matrix_hue = (uint8_t)hue;
    matrix_sat = sat;
    matrix_val = val;
}

void rgb_matrix_sethsv(uint16_t hue, uint8_t sat, uint8_t val) {
    rgb_matrix_sethsv_noeeprom(hue, sat, val);
    ++matrix_eeprom_writes;
}

void rgb_matrix_mode_noeeprom(uint8_t mode) {
    matrix_mode = mode;
}

void rgb_matrix_mode(uint8_t mode) {
    rgb_matrix_mode_noeeprom(mode);
    ++matrix_eeprom_writes;
}

void rgb_matrix_enable_noeeprom(void) {
    matrix_enabled = 1;
}

void rgb_matrix_enable(void) {
    rgb_matrix_enable_noeeprom();
    ++matrix_eeprom_writes;
}

void rgb_matrix_disable_noeeprom(void) {
    matrix_enabled = 0;
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

static void feed_config_packet(uint8_t opcode, uint16_t request_id, uint8_t chunk_index, uint8_t chunk_count, const uint8_t *payload, uint8_t payload_length) {
    uint8_t report[CODEX_MICRO_REPORT_SIZE] = {0};
    report[0] = CODEX_MICRO_REPORT_ID;
    report[1] = 3;
    report[2] = 1;
    report[3] = opcode;
    report[4] = (uint8_t)request_id;
    report[5] = (uint8_t)(request_id >> 8);
    report[6] = chunk_index;
    report[7] = chunk_count;
    report[8] = payload_length;
    if (payload_length > 0) {
        memcpy(&report[9], payload, payload_length);
    }
    raw_hid_receive(report, sizeof(report));
}

static void expect_config_status(uint8_t opcode, uint16_t request_id, uint8_t status) {
    assert(report_count > 0);
    const uint8_t *report = reports[report_count - 1].data;
    assert(report[0] == CODEX_MICRO_REPORT_ID && report[1] == 3 && report[2] == 1);
    assert(report[3] == (uint8_t)(opcode | 0x80));
    assert(report[4] == (uint8_t)request_id && report[5] == (uint8_t)(request_id >> 8));
    assert(report[8] == 1 && report[9] == status);
}

static void stage_config(const kb16_config_payload_t *payload, uint16_t request_id, uint32_t announced_crc) {
    const uint8_t *bytes = (const uint8_t *)payload;
    const uint8_t fragment_size = 55;
    const uint8_t chunk_count = (uint8_t)((sizeof(*payload) + fragment_size - 1) / fragment_size);
    uint8_t begin[6] = {(uint8_t)sizeof(*payload), (uint8_t)(sizeof(*payload) >> 8), (uint8_t)announced_crc, (uint8_t)(announced_crc >> 8), (uint8_t)(announced_crc >> 16), (uint8_t)(announced_crc >> 24)};
    feed_config_packet(3, request_id, 0, chunk_count, begin, sizeof(begin));
    expect_config_status(3, request_id, 0);
    for (uint8_t chunk = 0; chunk < chunk_count; ++chunk) {
        uint16_t offset = (uint16_t)chunk * fragment_size;
        uint16_t remaining = sizeof(*payload) - offset;
        uint8_t length = remaining > fragment_size ? fragment_size : (uint8_t)remaining;
        feed_config_packet(4, request_id, chunk, chunk_count, bytes + offset, length);
        expect_config_status(4, request_id, 0);
    }
}

static void test_lighting_transport(void) {
    clear_reports();
    feed_config_packet(7, 0x4001, 0, 1, NULL, 0);
    assert(report_count == 1);
    assert(reports[0].data[3] == 0x87 && reports[0].data[8] == 5);
    assert(reports[0].data[9] == 1 && reports[0].data[10] == RGB_MATRIX_SOLID_COLOR);
    assert(reports[0].data[11] == 170 && reports[0].data[12] == 140 && reports[0].data[13] == 120);

    uint8_t preview[3] = {42, 200, 99};
    matrix_eeprom_writes = 0;
    clear_reports();
    feed_config_packet(8, 0x4002, 0, 1, preview, sizeof(preview));
    expect_config_status(8, 0x4002, 0);
    assert(matrix_enabled == 1 && matrix_mode == RGB_MATRIX_SOLID_COLOR);
    assert(matrix_hue == 42 && matrix_sat == 200 && matrix_val == 99);
    assert(matrix_eeprom_writes == 0);

    uint8_t restore[5] = {0, 3, 170, 140, 120};
    clear_reports();
    feed_config_packet(10, 0x4003, 0, 1, restore, sizeof(restore));
    expect_config_status(10, 0x4003, 0);
    assert(matrix_enabled == 0 && matrix_mode == 3);
    assert(matrix_hue == 170 && matrix_sat == 140 && matrix_val == 120);
    assert(matrix_eeprom_writes == 0);

    uint8_t commit[3] = {77, 88, 99};
    clear_reports();
    feed_config_packet(9, 0x4004, 0, 1, commit, sizeof(commit));
    expect_config_status(9, 0x4004, 0);
    assert(matrix_enabled == 1 && matrix_mode == RGB_MATRIX_SOLID_COLOR);
    assert(matrix_hue == 77 && matrix_sat == 88 && matrix_val == 99);
    assert(matrix_eeprom_writes == 3);

    clear_reports();
    feed_config_packet(8, 0x4005, 0, 1, preview, 2);
    expect_config_status(8, 0x4005, 3);
    uint8_t invalid_restore[5] = {1, 0, 1, 2, 3};
    feed_config_packet(10, 0x4006, 0, 1, invalid_restore, sizeof(invalid_restore));
    expect_config_status(10, 0x4006, 6);

    kb16_config_input_pressed();
    feed_config_packet(9, 0x4007, 0, 1, commit, sizeof(commit));
    expect_config_status(9, 0x4007, 7);
    kb16_config_input_released();

    matrix_enabled = 1;
    matrix_mode = RGB_MATRIX_SOLID_COLOR;
    matrix_hue = 170;
    matrix_sat = 140;
    matrix_val = 120;
    matrix_eeprom_writes = 0;
}

static void test_config_transport(void) {
    clear_reports();
    feed_config_packet(1, 0x1234, 0, 1, NULL, 0);
    assert(report_count == 1);
    assert(reports[0].data[1] == 3 && reports[0].data[3] == 0x81);
    assert(reports[0].data[9] == 0 && reports[0].data[10] == 1);

    clear_reports();
    feed_config_packet(2, 0x1235, 0, 1, NULL, 0);
    assert(report_count == 7);
    for (uint8_t chunk = 0; chunk < report_count; ++chunk) {
        assert(reports[chunk].data[1] == 3 && reports[chunk].data[3] == 0x82);
        assert(reports[chunk].data[6] == chunk && reports[chunk].data[7] == 7);
    }

    kb16_config_payload_t changed = *kb16_config_get();
    uint8_t temporary = changed.native_keys[0];
    changed.native_keys[0] = changed.native_keys[15];
    changed.native_keys[15] = temporary;
    const uint8_t *bytes = (const uint8_t *)&changed;
    uint32_t crc = kb16_config_crc32(bytes, sizeof(changed));
    uint8_t begin[6] = {(uint8_t)sizeof(changed), (uint8_t)(sizeof(changed) >> 8), (uint8_t)crc, (uint8_t)(crc >> 8), (uint8_t)(crc >> 16), (uint8_t)(crc >> 24)};
    const uint8_t fragment_size = 55;
    const uint8_t chunk_count = (uint8_t)((sizeof(changed) + fragment_size - 1) / fragment_size);
    clear_reports();
    feed_config_packet(3, 0x2000, 0, chunk_count, begin, sizeof(begin));
    expect_config_status(3, 0x2000, 0);
    for (uint8_t chunk = 0; chunk < chunk_count; ++chunk) {
        uint16_t offset = (uint16_t)chunk * fragment_size;
        uint16_t remaining = sizeof(changed) - offset;
        uint8_t length = remaining > fragment_size ? fragment_size : (uint8_t)remaining;
        feed_config_packet(4, 0x2000, chunk, chunk_count, bytes + offset, length);
        expect_config_status(4, 0x2000, 0);
    }
    feed_config_packet(5, 0x2000, 0, 1, NULL, 0);
    expect_config_status(5, 0x2000, 0);
    assert(kb16_config_find_native(KB16_NATIVE_AG00) == 15);

    feed_config_packet(5, 0x2000, 0, 1, NULL, 0);
    expect_config_status(5, 0x2000, 9);

    uint8_t unknown_version[CODEX_MICRO_REPORT_SIZE] = {CODEX_MICRO_REPORT_ID, 3, 99, 1, 0x01, 0x30, 0, 1, 0};
    raw_hid_receive(unknown_version, sizeof(unknown_version));
    expect_config_status(1, 0x3001, 1);

    clear_reports();
    feed_config_packet(3, 0x3002, 0, chunk_count, begin, sizeof(begin));
    expect_config_status(3, 0x3002, 0);
    feed_config_packet(4, 0x3002, 1, chunk_count, bytes + fragment_size, fragment_size);
    expect_config_status(4, 0x3002, 4);

    clear_reports();
    stage_config(&changed, 0x3003, crc ^ 1U);
    feed_config_packet(5, 0x3003, 0, 1, NULL, 0);
    expect_config_status(5, 0x3003, 5);

    kb16_config_payload_t illegal = changed;
    illegal.native_keys[0] = illegal.native_keys[1];
    uint32_t illegal_crc = kb16_config_crc32(&illegal, sizeof(illegal));
    clear_reports();
    stage_config(&illegal, 0x3004, illegal_crc);
    feed_config_packet(5, 0x3004, 0, 1, NULL, 0);
    expect_config_status(5, 0x3004, 6);

    clear_reports();
    stage_config(&changed, 0x3005, crc);
    kb16_config_input_pressed();
    feed_config_packet(5, 0x3005, 0, 1, NULL, 0);
    expect_config_status(5, 0x3005, 7);
    feed_config_packet(6, 0x3006, 0, 1, NULL, 0);
    expect_config_status(6, 0x3006, 7);
    kb16_config_input_released();

    clear_reports();
    feed_config_packet(3, 0x3007, 0, chunk_count, begin, sizeof(begin));
    expect_config_status(3, 0x3007, 0);
    feed_config_packet(4, 0x3007, 0, chunk_count, bytes, fragment_size);
    expect_config_status(4, 0x3007, 0);
    feed_config_packet(3, 0x3008, 0, chunk_count, begin, sizeof(begin));
    expect_config_status(3, 0x3008, 0);
    feed_config_packet(5, 0x3007, 0, 1, NULL, 0);
    expect_config_status(5, 0x3007, 9);

    kb16_config_host_corrupt_slot(0, 20);
    kb16_config_host_fail_writes(true);
    feed_config_packet(6, 0x3009, 0, 1, NULL, 0);
    expect_config_status(6, 0x3009, 8);
    kb16_config_host_fail_writes(false);
    feed_config_packet(6, 0x3010, 0, 1, NULL, 0);
    expect_config_status(6, 0x3010, 0);
    test_lighting_transport();
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
    kb16_config_host_clear_storage();
    kb16_config_init();
    test_config_transport();
    codex_micro_settings_init();
    codex_micro_init();
    layer_state = 1;
    assert(matrix_val == 120);

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
    fake_timer = 1001;
    codex_micro_task();
    fake_timer = 0;

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":16777215,\"b\":0.5,\"e\":\"solid\"},{\"id\":1,\"c\":255,\"b\":0.5,\"e\":\"breath\"},{\"id\":2,\"c\":16739584,\"b\":0.5,\"e\":\"solid\"},{\"id\":3,\"c\":16711731,\"b\":0.5,\"e\":\"solid\"},{\"id\":4,\"c\":16777215,\"b\":0.5,\"e\":\"solid\"},{\"id\":5,\"c\":16777215,\"b\":0.5,\"e\":\"solid\"}],\"id\":42}");
    expect_output_contains("\"id\":42");
    expect_output_contains("\"ok\":true");
    assert(codex_micro_slot_state(0) == CODEX_MICRO_SLOT_IDLE);
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":4294967296,\"c\":16711731,\"b\":1,\"e\":\"solid\"}],\"id\":421}");
    assert(codex_micro_slot_state(0) == CODEX_MICRO_SLOT_IDLE);
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0x,\"c\":16711731,\"b\":1,\"e\":\"solid\"}],\"id\":4211}");
    assert(codex_micro_slot_state(0) == CODEX_MICRO_SLOT_IDLE);
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":16777215,\"b\":4294967296,\"e\":\"solid\"}],\"id\":422}");
    assert(codex_micro_slot_state(0) == CODEX_MICRO_SLOT_IDLE);
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":16777215,\"b\":0.5,\"e\":\"solid\"}],\"id\":423}");

    fake_timer = 800;
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(matrix_val == 120);
    assert(colors[0][0] == 128 && colors[0][1] == 104 && colors[0][2] == 77);
    assert(colors[1][0] == 0 && colors[1][1] == 0 && colors[1][2] > 0 && colors[1][2] < 128);
    assert(colors[2][0] == 107 && colors[2][1] == 105 && colors[2][2] == 128);
    assert(colors[4][0] > colors[4][1] && colors[4][1] > 0 && colors[4][2] == 0);
    assert(colors[5][0] > colors[5][2] && colors[5][2] > colors[5][1]);
    assert(colors[8][0] == colors[0][0] && colors[8][1] == colors[0][1] && colors[8][2] == colors[0][2]);
    assert(colors[9][0] == colors[0][0] && colors[9][1] == colors[0][1] && colors[9][2] == colors[0][2]);
    assert(colors[15][0] == colors[2][0] && colors[15][1] == colors[2][1] && colors[15][2] == colors[2][2]);

    kb16_config_payload_t moved_lights = *kb16_config_get();
    uint8_t moved_control = moved_lights.native_keys[1];
    moved_lights.native_keys[1] = moved_lights.native_keys[15];
    moved_lights.native_keys[15] = moved_control;
    assert(kb16_config_commit(&moved_lights));
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[15][0] == 0 && colors[15][1] == 0 && colors[15][2] > 0 && colors[15][2] < 128);
    assert(colors[1][0] == 107 && colors[1][1] == 105 && colors[1][2] == 128);
    assert(kb16_config_reset_defaults());

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":65356,\"b\":1,\"e\":\"solid\"},{\"id\":1,\"c\":3166206,\"b\":1,\"e\":\"solid\"},{\"id\":2,\"c\":16739584,\"b\":1,\"e\":\"solid\"},{\"id\":3,\"c\":16711731,\"b\":1,\"e\":\"solid\"},{\"id\":4,\"c\":1193046,\"b\":1,\"e\":\"solid\"},{\"id\":5,\"c\":16777215,\"b\":1,\"e\":\"solid\"}],\"id\":43}");
    expect_output_contains("\"id\":43");
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][1] > colors[0][0] && colors[0][1] > colors[0][2]);
    assert(colors[1][2] > colors[1][1] && colors[1][1] > colors[1][0]);
    assert(colors[4][0] > colors[4][1] && colors[4][1] > 0 && colors[4][2] == 0);
    assert(colors[5][0] > colors[5][2] && colors[5][2] > colors[5][1]);
    assert(colors[8][0] == 0x12 && colors[8][1] == 0x34 && colors[8][2] == 0x56);
    assert(colors[9][0] == 255 && colors[9][1] == 208 && colors[9][2] == 154);

    // A new unread task produces two smooth, whole-matrix green pulses and is
    // visible even when a local utility layer is active.
    fake_timer = 1175;
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][1] > 245 && colors[0][2] > 65);
    assert(colors[15][1] > colors[15][0]);
    assert(colors[0][1] > colors[15][1]);
    layer_state = 1UL << 2;
    memset(colors, 0xAA, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] == 0 && colors[0][1] > 245 && colors[0][2] > 65);
    assert(colors[15][0] == 0 && colors[15][1] > 0);
    assert(colors[0][1] > colors[15][1]);
    fake_timer = 2701;
    memset(colors, 0xAA, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] == 0xAA && colors[0][1] == 0xAA && colors[0][2] == 0xAA);
    layer_state = 1;

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"},{\"id\":1,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"},{\"id\":2,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"},{\"id\":3,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"},{\"id\":4,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"},{\"id\":5,\"c\":16777215,\"b\":0.8,\"e\":\"solid\"}],\"id\":45}");
    assert(matrix_val == 120);
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] == 204 && colors[0][1] == 166 && colors[0][2] == 123);
    assert(colors[15][0] == 172 && colors[15][1] == 168 && colors[15][2] == 204);

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":0,\"b\":0,\"e\":\"off\"}],\"id\":46}");
    assert(matrix_val == 120);
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[2][0] == 172 && colors[2][1] == 168 && colors[2][2] == 204);

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":0,\"b\":0,\"e\":\"off\"},{\"id\":1,\"c\":0,\"b\":0,\"e\":\"off\"},{\"id\":2,\"c\":0,\"b\":0,\"e\":\"off\"},{\"id\":3,\"c\":0,\"b\":0,\"e\":\"off\"},{\"id\":4,\"c\":0,\"b\":0,\"e\":\"off\"},{\"id\":5,\"c\":0,\"b\":0,\"e\":\"off\"}],\"id\":47}");
    assert(matrix_val == 120);
    for (uint8_t layer = 0; layer < 4; ++layer) {
        layer_state = 1UL << layer;
        memset(colors, 0xAA, sizeof(colors));
        assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
        for (uint8_t led = 0; led < RGB_MATRIX_LED_COUNT; ++led) {
            if (layer == 0) {
                assert(colors[led][0] == 0 && colors[led][1] == 0 && colors[led][2] == 0);
            } else {
                assert(colors[led][0] == 0xAA && colors[led][1] == 0xAA && colors[led][2] == 0xAA);
            }
        }
    }

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":5,\"c\":16777215,\"b\":0.2,\"e\":\"solid\"}],\"id\":48}");
    assert(matrix_val == 120);
    memset(colors, 0xAA, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] == 0xAA && colors[15][2] == 0xAA);
    layer_state = 1;
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] == 0 && colors[0][1] == 0 && colors[0][2] == 0);
    assert(colors[4][0] == 0 && colors[4][1] == 0 && colors[4][2] == 0);
    assert(colors[9][0] == 51 && colors[9][1] == 41 && colors[9][2] == 30);
    assert(colors[2][0] == 43 && colors[2][1] == 42 && colors[2][2] == 51);
    assert(colors[15][0] == 43 && colors[15][1] == 42 && colors[15][2] == 51);

    clear_reports();
    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":0,\"b\":0,\"e\":\"off\"},\"ambient\":{\"c\":0,\"b\":0,\"e\":\"off\"}},\"id\":49}");
    assert(matrix_val == 120);

    // The app sends numeric Work Louder effect ids. Cover the complete effect
    // catalog on the shared 16-key lighting zone.
    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0},\"ambient\":{\"c\":3166206,\"b\":1,\"e\":2,\"s\":0.4,\"m\":0}},\"id\":52}");
    fake_timer = 0;
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] == 0 && colors[0][1] == 0 && colors[0][2] == 0);
    assert(colors[15][0] > 0 && colors[15][2] > colors[15][0]);
    assert(colors[3][0] == 0 && colors[3][1] == 0 && colors[3][2] == 0);

    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":65356,\"b\":1,\"e\":1,\"s\":0,\"m\":0},\"ambient\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0}},\"id\":53}");
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[2][0] == 0 && colors[2][1] == 255 && colors[2][2] == 76);

    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":0,\"b\":1,\"e\":3,\"s\":0.4,\"m\":0},\"ambient\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0}},\"id\":54}");
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(memcmp(colors[2], colors[3], sizeof(colors[2])) != 0);

    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":255,\"b\":1,\"e\":4,\"s\":0.4,\"m\":0},\"ambient\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0}},\"id\":55}");
    fake_timer = 0;
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[2][2] == 0);
    fake_timer = 1460;
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[2][2] > 245);

    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":16711680,\"b\":1,\"e\":5,\"s\":0,\"m\":0},\"ambient\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0}},\"id\":56}");
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] < colors[3][0]);

    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":16777215,\"b\":1,\"e\":6,\"s\":0.4,\"m\":0},\"ambient\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0}},\"id\":57}");
    fake_timer = 0;
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[2][0] >= 128 && colors[2][1] >= 128 && colors[2][2] >= 128);

    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":3166206,\"b\":1,\"e\":1,\"s\":0.4,\"sk\":1,\"sa\":0}],\"id\":58}");
    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0},\"ambient\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0}},\"id\":59}");
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[2][0] == 0 && colors[2][1] == 92 && colors[2][2] == 255);

    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"sk\":0,\"sa\":0}],\"id\":60}");
    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0},\"ambient\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0}},\"id\":61}");

    // A selected working task asks for solid keys and a snake ambient ring at
    // once. The KB16 composites both onto its single physical LED zone.
    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":3166206,\"b\":1,\"e\":1,\"s\":0,\"m\":0},\"ambient\":{\"c\":3166206,\"b\":1,\"e\":2,\"s\":0.4,\"m\":0}},\"id\":62}");
    fake_timer = 0;
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[3][2] > 0 && colors[3][2] < 171);
    assert(colors[14][2] > colors[3][2]);
    feed_json("{\"method\":\"v.oai.rgbcfg\",\"params\":{\"keys\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0},\"ambient\":{\"c\":0,\"b\":0,\"e\":0,\"s\":0,\"m\":0}},\"id\":63}");

    clear_reports();
    feed_json("{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":16777215,\"b\":1,\"e\":\"solid\"},{\"id\":1,\"c\":16777215,\"b\":1,\"e\":\"solid\"},{\"id\":2,\"c\":16777215,\"b\":1,\"e\":\"solid\"},{\"id\":3,\"c\":16777215,\"b\":1,\"e\":\"solid\"},{\"id\":4,\"c\":16777215,\"b\":1,\"e\":\"solid\"},{\"id\":5,\"c\":16777215,\"b\":1,\"e\":\"solid\"}],\"id\":50}");
    assert(matrix_val == 120);
    memset(colors, 0, sizeof(colors));
    assert(codex_micro_rgb_indicators(0, RGB_MATRIX_LED_COUNT));
    assert(colors[0][0] == 255 && colors[0][1] == 208 && colors[0][2] == 154);
    assert(colors[15][0] == 0 && colors[15][1] == 0 && colors[15][2] == 0);

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
