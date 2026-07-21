#include "kb16_config_transport.h"

#include <string.h>

#include "codex_micro_protocol.h"
#include "kb16_config.h"
#include "raw_hid.h"

#ifdef CODEX_MICRO_HOST_TEST
#    include "test_qmk.h"
#else
#    include QMK_KEYBOARD_H
#endif

#define KB16_CONFIG_CHANNEL 3
#define KB16_CONFIG_PROTOCOL_VERSION 1
#define KB16_CONFIG_HEADER_SIZE 9
#define KB16_CONFIG_FRAGMENT_SIZE (CODEX_MICRO_REPORT_SIZE - KB16_CONFIG_HEADER_SIZE)

typedef enum {
    KB16_CONFIG_OP_HELLO = 1,
    KB16_CONFIG_OP_READ,
    KB16_CONFIG_OP_WRITE_BEGIN,
    KB16_CONFIG_OP_WRITE_CHUNK,
    KB16_CONFIG_OP_WRITE_COMMIT,
    KB16_CONFIG_OP_RESET_DEFAULTS,
    KB16_CONFIG_OP_READ_LIGHTING,
    KB16_CONFIG_OP_PREVIEW_LIGHTING,
    KB16_CONFIG_OP_COMMIT_LIGHTING,
    KB16_CONFIG_OP_RESTORE_LIGHTING,
} kb16_config_opcode_t;

typedef enum {
    KB16_CONFIG_STATUS_OK,
    KB16_CONFIG_STATUS_BAD_VERSION,
    KB16_CONFIG_STATUS_BAD_OPCODE,
    KB16_CONFIG_STATUS_BAD_LENGTH,
    KB16_CONFIG_STATUS_BAD_ORDER,
    KB16_CONFIG_STATUS_BAD_CRC,
    KB16_CONFIG_STATUS_INVALID_CONFIG,
    KB16_CONFIG_STATUS_BUSY,
    KB16_CONFIG_STATUS_STORAGE_ERROR,
    KB16_CONFIG_STATUS_NO_SESSION,
} kb16_config_status_t;

typedef struct {
    bool                  active;
    uint16_t              request_id;
    uint16_t              expected_length;
    uint32_t              expected_crc;
    uint8_t               expected_chunks;
    uint8_t               next_chunk;
    uint16_t              received_length;
    kb16_config_payload_t payload;
} write_session_t;

static write_session_t write_session;

static uint16_t read_u16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void send_packet(uint8_t opcode, uint16_t request_id, uint8_t chunk_index, uint8_t chunk_count, const uint8_t *payload, uint8_t payload_length) {
    uint8_t report[CODEX_MICRO_REPORT_SIZE] = {0};
    report[0] = CODEX_MICRO_REPORT_ID;
    report[1] = KB16_CONFIG_CHANNEL;
    report[2] = KB16_CONFIG_PROTOCOL_VERSION;
    report[3] = opcode | 0x80U;
    write_u16(&report[4], request_id);
    report[6] = chunk_index;
    report[7] = chunk_count;
    report[8] = payload_length;
    if (payload_length > 0 && payload != NULL) {
        memcpy(&report[9], payload, payload_length);
    }
    raw_hid_send(report, sizeof(report));
}

static void send_status(uint8_t opcode, uint16_t request_id, kb16_config_status_t status) {
    uint8_t payload = (uint8_t)status;
    send_packet(opcode, request_id, 0, 1, &payload, 1);
}

static void send_hello(uint16_t request_id) {
    uint8_t payload[16] = {0};
    payload[0] = KB16_CONFIG_STATUS_OK;
    payload[1] = KB16_CONFIG_SCHEMA_VERSION;
    payload[2] = KB16_CONFIG_LAYER_COUNT;
    payload[3] = KB16_CONFIG_KEY_COUNT;
    write_u16(&payload[4], sizeof(kb16_config_payload_t));
    write_u32(&payload[6], kb16_config_generation());
    write_u32(&payload[10], kb16_config_crc());
    payload[14] = KB16_CONFIG_ENCODER_COUNT;
    payload[15] = KB16_CONFIG_ENCODER_ACTION_COUNT;
    send_packet(KB16_CONFIG_OP_HELLO, request_id, 0, 1, payload, sizeof(payload));
}

static void send_config(uint16_t request_id) {
    uint8_t response[8 + sizeof(kb16_config_payload_t)];
    write_u32(&response[0], kb16_config_generation());
    write_u32(&response[4], kb16_config_crc());
    memcpy(&response[8], kb16_config_get(), sizeof(kb16_config_payload_t));
    uint8_t chunk_count = (uint8_t)((sizeof(response) + KB16_CONFIG_FRAGMENT_SIZE - 1U) / KB16_CONFIG_FRAGMENT_SIZE);
    for (uint8_t chunk = 0; chunk < chunk_count; ++chunk) {
        size_t offset = (size_t)chunk * KB16_CONFIG_FRAGMENT_SIZE;
        size_t remaining = sizeof(response) - offset;
        uint8_t length = remaining > KB16_CONFIG_FRAGMENT_SIZE ? KB16_CONFIG_FRAGMENT_SIZE : (uint8_t)remaining;
        send_packet(KB16_CONFIG_OP_READ, request_id, chunk, chunk_count, &response[offset], length);
    }
}

static void send_lighting(uint16_t request_id) {
    uint8_t payload[5] = {
        rgb_matrix_is_enabled(),
        rgb_matrix_get_mode(),
        rgb_matrix_get_hue(),
        rgb_matrix_get_sat(),
        rgb_matrix_get_val(),
    };
    send_packet(KB16_CONFIG_OP_READ_LIGHTING, request_id, 0, 1, payload, sizeof(payload));
}

static void apply_lighting_noeeprom(uint8_t hue, uint8_t saturation, uint8_t value) {
    rgb_matrix_enable_noeeprom();
    rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);
    rgb_matrix_sethsv_noeeprom(hue, saturation, value);
}

static void commit_lighting(uint8_t hue, uint8_t saturation, uint8_t value) {
    rgb_matrix_enable();
    rgb_matrix_mode(RGB_MATRIX_SOLID_COLOR);
    rgb_matrix_sethsv(hue, saturation, value);
}

static bool restore_lighting_noeeprom(const uint8_t *payload) {
    if (payload[0] > 1 || payload[1] == 0 || payload[1] >= RGB_MATRIX_EFFECT_MAX) {
        return false;
    }
    rgb_matrix_enable_noeeprom();
    rgb_matrix_mode_noeeprom(payload[1]);
    rgb_matrix_sethsv_noeeprom(payload[2], payload[3], payload[4]);
    if (!payload[0]) {
        rgb_matrix_disable_noeeprom();
    }
    return true;
}

static void handle_write_begin(uint16_t request_id, const uint8_t *payload, uint8_t length, uint8_t chunk_count) {
    if (length != 6 || chunk_count == 0) {
        send_status(KB16_CONFIG_OP_WRITE_BEGIN, request_id, KB16_CONFIG_STATUS_BAD_LENGTH);
        return;
    }
    uint16_t expected_length = read_u16(payload);
    uint8_t expected_chunks = (uint8_t)((expected_length + KB16_CONFIG_FRAGMENT_SIZE - 1U) / KB16_CONFIG_FRAGMENT_SIZE);
    if (expected_length != sizeof(kb16_config_payload_t) || expected_chunks != chunk_count) {
        send_status(KB16_CONFIG_OP_WRITE_BEGIN, request_id, KB16_CONFIG_STATUS_BAD_LENGTH);
        return;
    }
    memset(&write_session, 0, sizeof(write_session));
    write_session.active = true;
    write_session.request_id = request_id;
    write_session.expected_length = expected_length;
    write_session.expected_crc = read_u32(&payload[2]);
    write_session.expected_chunks = chunk_count;
    send_status(KB16_CONFIG_OP_WRITE_BEGIN, request_id, KB16_CONFIG_STATUS_OK);
}

static void handle_write_chunk(uint16_t request_id, uint8_t chunk_index, uint8_t chunk_count, const uint8_t *payload, uint8_t length) {
    if (!write_session.active || write_session.request_id != request_id) {
        send_status(KB16_CONFIG_OP_WRITE_CHUNK, request_id, KB16_CONFIG_STATUS_NO_SESSION);
        return;
    }
    if (chunk_count != write_session.expected_chunks || chunk_index != write_session.next_chunk) {
        send_status(KB16_CONFIG_OP_WRITE_CHUNK, request_id, KB16_CONFIG_STATUS_BAD_ORDER);
        return;
    }
    uint16_t remaining = write_session.expected_length - write_session.received_length;
    uint8_t expected = remaining > KB16_CONFIG_FRAGMENT_SIZE ? KB16_CONFIG_FRAGMENT_SIZE : (uint8_t)remaining;
    if (length != expected) {
        send_status(KB16_CONFIG_OP_WRITE_CHUNK, request_id, KB16_CONFIG_STATUS_BAD_LENGTH);
        return;
    }
    memcpy((uint8_t *)&write_session.payload + write_session.received_length, payload, length);
    write_session.received_length += length;
    ++write_session.next_chunk;
    send_status(KB16_CONFIG_OP_WRITE_CHUNK, request_id, KB16_CONFIG_STATUS_OK);
}

static void handle_write_commit(uint16_t request_id, uint8_t length) {
    if (!write_session.active || write_session.request_id != request_id) {
        send_status(KB16_CONFIG_OP_WRITE_COMMIT, request_id, KB16_CONFIG_STATUS_NO_SESSION);
        return;
    }
    if (length != 0 || write_session.received_length != write_session.expected_length || write_session.next_chunk != write_session.expected_chunks) {
        send_status(KB16_CONFIG_OP_WRITE_COMMIT, request_id, KB16_CONFIG_STATUS_BAD_ORDER);
        return;
    }
    if (kb16_config_input_busy()) {
        send_status(KB16_CONFIG_OP_WRITE_COMMIT, request_id, KB16_CONFIG_STATUS_BUSY);
        return;
    }
    if (kb16_config_crc32(&write_session.payload, sizeof(write_session.payload)) != write_session.expected_crc) {
        send_status(KB16_CONFIG_OP_WRITE_COMMIT, request_id, KB16_CONFIG_STATUS_BAD_CRC);
        write_session.active = false;
        return;
    }
    if (!kb16_config_validate(&write_session.payload)) {
        send_status(KB16_CONFIG_OP_WRITE_COMMIT, request_id, KB16_CONFIG_STATUS_INVALID_CONFIG);
        write_session.active = false;
        return;
    }
    bool committed = kb16_config_commit(&write_session.payload);
    write_session.active = false;
    send_status(KB16_CONFIG_OP_WRITE_COMMIT, request_id, committed ? KB16_CONFIG_STATUS_OK : KB16_CONFIG_STATUS_STORAGE_ERROR);
}

bool kb16_config_transport_receive(uint8_t *data, uint8_t length) {
    if (length != CODEX_MICRO_REPORT_SIZE || data[0] != CODEX_MICRO_REPORT_ID || data[1] != KB16_CONFIG_CHANNEL) {
        return false;
    }
    uint8_t opcode = data[3];
    uint16_t request_id = read_u16(&data[4]);
    uint8_t chunk_index = data[6];
    uint8_t chunk_count = data[7];
    uint8_t payload_length = data[8];
    if (data[2] != KB16_CONFIG_PROTOCOL_VERSION) {
        send_status(opcode, request_id, KB16_CONFIG_STATUS_BAD_VERSION);
        return true;
    }
    if (payload_length > KB16_CONFIG_FRAGMENT_SIZE) {
        send_status(opcode, request_id, KB16_CONFIG_STATUS_BAD_LENGTH);
        return true;
    }
    const uint8_t *payload = &data[9];
    switch (opcode) {
        case KB16_CONFIG_OP_HELLO:
            payload_length == 0 ? send_hello(request_id) : send_status(opcode, request_id, KB16_CONFIG_STATUS_BAD_LENGTH);
            break;
        case KB16_CONFIG_OP_READ:
            payload_length == 0 ? send_config(request_id) : send_status(opcode, request_id, KB16_CONFIG_STATUS_BAD_LENGTH);
            break;
        case KB16_CONFIG_OP_WRITE_BEGIN:
            handle_write_begin(request_id, payload, payload_length, chunk_count);
            break;
        case KB16_CONFIG_OP_WRITE_CHUNK:
            handle_write_chunk(request_id, chunk_index, chunk_count, payload, payload_length);
            break;
        case KB16_CONFIG_OP_WRITE_COMMIT:
            handle_write_commit(request_id, payload_length);
            break;
        case KB16_CONFIG_OP_RESET_DEFAULTS:
            if (payload_length != 0) {
                send_status(opcode, request_id, KB16_CONFIG_STATUS_BAD_LENGTH);
            } else if (kb16_config_input_busy()) {
                send_status(opcode, request_id, KB16_CONFIG_STATUS_BUSY);
            } else {
                bool reset = kb16_config_reset_defaults();
                send_status(opcode, request_id, reset ? KB16_CONFIG_STATUS_OK : KB16_CONFIG_STATUS_STORAGE_ERROR);
            }
            break;
        case KB16_CONFIG_OP_READ_LIGHTING:
            payload_length == 0 ? send_lighting(request_id) : send_status(opcode, request_id, KB16_CONFIG_STATUS_BAD_LENGTH);
            break;
        case KB16_CONFIG_OP_PREVIEW_LIGHTING:
            if (payload_length != 3) {
                send_status(opcode, request_id, KB16_CONFIG_STATUS_BAD_LENGTH);
            } else {
                apply_lighting_noeeprom(payload[0], payload[1], payload[2]);
                send_status(opcode, request_id, KB16_CONFIG_STATUS_OK);
            }
            break;
        case KB16_CONFIG_OP_COMMIT_LIGHTING:
            if (payload_length != 3) {
                send_status(opcode, request_id, KB16_CONFIG_STATUS_BAD_LENGTH);
            } else if (kb16_config_input_busy()) {
                send_status(opcode, request_id, KB16_CONFIG_STATUS_BUSY);
            } else {
                commit_lighting(payload[0], payload[1], payload[2]);
                send_status(opcode, request_id, KB16_CONFIG_STATUS_OK);
            }
            break;
        case KB16_CONFIG_OP_RESTORE_LIGHTING:
            if (payload_length != 5) {
                send_status(opcode, request_id, KB16_CONFIG_STATUS_BAD_LENGTH);
            } else if (!restore_lighting_noeeprom(payload)) {
                send_status(opcode, request_id, KB16_CONFIG_STATUS_INVALID_CONFIG);
            } else {
                send_status(opcode, request_id, KB16_CONFIG_STATUS_OK);
            }
            break;
        default:
            send_status(opcode, request_id, KB16_CONFIG_STATUS_BAD_OPCODE);
            break;
    }
    return true;
}
