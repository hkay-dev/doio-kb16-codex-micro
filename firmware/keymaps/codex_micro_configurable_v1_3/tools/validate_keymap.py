from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(name):
    return (ROOT / name).read_text(encoding="utf-8")


def require(source, token, message):
    assert token in source, message


def main():
    keymap = read("keymap.c")
    config_h = read("config.h")
    rules = read("rules.mk")
    model_h = read("kb16_config.h")
    model_c = read("kb16_config.c")
    transport_h = read("kb16_config_transport.h")
    transport_c = read("kb16_config_transport.c")
    protocol_h = read("codex_micro_protocol.h")
    protocol_c = read("codex_micro_protocol.c")
    settings_h = read("codex_micro_settings.h")
    settings_c = read("codex_micro_settings.c")
    alerts_c = read("codex_micro_alerts.c")
    menu_c = read("codex_micro_menu.c")
    combined = "\n".join((keymap, config_h, rules, model_h, model_c,
                            transport_h, transport_c, protocol_h, protocol_c))

    require(keymap, "#define CONFIGURABLE_LAYOUT LAYOUT(", "missing configurable physical layout")
    assert keymap.count("= CONFIGURABLE_LAYOUT") == 4, "all four fixed layers must use the configurable layout"
    for index in range(16):
        require(keymap, f"CFG_KEY_{index}", f"missing physical key {index}")
    for control in ("LEFT_CCW", "LEFT_CW", "RIGHT_CCW", "RIGHT_CW",
                    "MIDDLE_CCW", "MIDDLE_CW", "LEFT_PRESS", "RIGHT_PRESS", "MIDDLE_PRESS"):
        require(keymap, f"CFG_ENCODER_{control}", f"missing encoder control {control}")
    assert keymap.count("ENCODER_CCW_CW(CFG_ENCODER_LEFT_CCW, CFG_ENCODER_LEFT_CW)") == 4
    assert keymap.count("ENCODER_CCW_CW(CFG_ENCODER_RIGHT_CCW, CFG_ENCODER_RIGHT_CW)") == 4
    assert keymap.count("ENCODER_CCW_CW(CFG_ENCODER_MIDDLE_CCW, CFG_ENCODER_MIDDLE_CW)") == 4
    require(keymap, "#define MIDDLE_RETURN_MS 3000", "middle encoder hold threshold changed")
    require(keymap, "if (layer == _CODEX && encoder == 0)", "Codex left encoder is not locked")
    require(keymap, "layer_move(_CODEX)", "middle hold does not return to Codex")
    for token in ("case KB16_ACTION_KEYBOARD", "case KB16_ACTION_CONSUMER", "case KB16_ACTION_MOUSE",
                  "case KB16_ACTION_FIRMWARE", "register_code((uint8_t)action->code)",
                  "unregister_code((uint8_t)action->code)", "update_action_modifiers(action->modifiers, false)"):
        require(keymap, token, f"missing action press/release path: {token}")
    assert keymap.count("kb16_config_input_released();") == 3, "input releases must only match tracked key and encoder presses"
    assert "if (action->press_count++ > 0)" in keymap, "guarded actions must keep the original hold timer when mapped to several controls"
    assert "action->press_count == 0 || --action->press_count > 0" in keymap, "one release must not cancel another held guarded action"

    for token in ("KB16_CONFIG_PAYLOAD_SIZE 340", "KB16_CONFIG_SLOT_SIZE 356",
                  "KB16_CONFIG_STORAGE_SIZE (KB16_CONFIG_SLOT_SIZE * 2)", "KB16_CONFIG_SCHEMA_VERSION 1",
                  "uint8_t       native_keys[KB16_CONFIG_KEY_COUNT]",
                  "kb16_action_t layers[KB16_CONFIG_CUSTOM_LAYER_COUNT][KB16_CONFIG_KEY_COUNT]",
                  "kb16_action_t encoders[KB16_CONFIG_CUSTOM_LAYER_COUNT][KB16_CONFIG_ENCODER_COUNT][KB16_CONFIG_ENCODER_ACTION_COUNT]"):
        require(model_h, token, f"configuration model changed: {token}")
    for token in ("KB16_NATIVE_AG00", "KB16_NATIVE_AG05", "KB16_NATIVE_MIC",
                  "KB16_NATIVE_ACT12", "kb16_config_find_native",
                  "kb16_config_crc32", "generation_newer", "slot_valid"):
        require(model_c + model_h, token, f"missing model behavior: {token}")
    require(model_c, "storage_write(erased, 0, sizeof(erased))", "default reset does not invalidate stale A/B slots")

    for token in ("KB16_CONFIG_CHANNEL 3", "KB16_CONFIG_PROTOCOL_VERSION 1",
                  "KB16_CONFIG_FRAGMENT_SIZE", "KB16_CONFIG_OP_HELLO",
                  "KB16_CONFIG_OP_READ", "KB16_CONFIG_OP_WRITE_BEGIN",
                  "KB16_CONFIG_OP_WRITE_CHUNK", "KB16_CONFIG_OP_WRITE_COMMIT",
                  "KB16_CONFIG_OP_RESET_DEFAULTS", "KB16_CONFIG_OP_READ_LIGHTING",
                  "KB16_CONFIG_OP_PREVIEW_LIGHTING", "KB16_CONFIG_OP_COMMIT_LIGHTING",
                  "KB16_CONFIG_OP_RESTORE_LIGHTING"):
        require(transport_c, token, f"missing Channel 3 definition: {token}")
    for token in ("KB16_CONFIG_STATUS_BAD_VERSION", "KB16_CONFIG_STATUS_BAD_ORDER",
                  "KB16_CONFIG_STATUS_BAD_CRC", "KB16_CONFIG_STATUS_BUSY",
                  "kb16_config_input_busy()"):
        require(transport_c + transport_h, token, f"missing write rejection: {token}")
    require(protocol_c, "kb16_config_transport_receive(data, length)", "Channel 3 is not dispatched before Channel 2")
    require(protocol_c, "kb16_config_find_native", "AG LEDs do not follow native controls")
    require(protocol_c, "get_highest_layer(layer_state) != 0", "ordinary layers do not bypass Codex lighting indicators")

    required_usb = ("#define VENDOR_ID 0x303A", "#define PRODUCT_ID 0x8360",
                    "#define RAW_USAGE_PAGE 0xFF00", "#define RAW_USAGE_ID 0x01",
                    "#define RAW_REPORT_ID 0x06", "#define RAW_REPORT_COUNT 63",
                    "#define RAW_EPSIZE 64", "#define EECONFIG_USER_DATA_SIZE 728")
    for token in required_usb:
        require(config_h, token, f"missing USB/storage identity: {token}")
    require(rules, "RAW_ENABLE = yes", "Raw HID disabled")
    require(rules, "WS2812_DRIVER = pwm", "PWM lighting driver changed")
    require(config_h, "#define WS2812_PWM_DMA_STREAM STM32_DMA1_STREAM6", "DMA6 lighting route changed")
    require(protocol_h, "#define CODEX_MICRO_REPORT_SIZE 64", "Codex report size changed")
    for token in ("AG%02u", "ACT06", "ACT07", "ACT08", "ACT09", "ACT10", "ACT12",
                  "ENC_CW", "ENC_CC", "ENC_SW", "v.oai.rad"):
        require(protocol_c, token, f"missing native protocol token: {token}")
    assert "ACT11" not in protocol_c, "ACT11 must never be emitted"
    for token in ("CODEX_MICRO_SETTINGS_OFFSET 712U", "CODEX_MICRO_SETTINGS_SIZE 16U", "CODEX_MICRO_COMPLETION_UNTIL_FOCUS", "CODEX_MICRO_ALERT_LAYOUT_SLOT_FOCUS", "crc16_ccitt"):
        require(settings_h + settings_c, token, f"missing settings storage boundary: {token}")
    for token in ("CODEX_MICRO_ALERT_COMPLETION", "CODEX_MICRO_ALERT_APPROVAL", "CODEX_MICRO_ALERT_ERROR", "CODEX_MICRO_ALERT_REMINDER", "apply_alert_layout", "path_coordinates"):
        require(alerts_c, token, f"missing alert behavior: {token}")
    for token in ("codex_micro_menu_save_close", "codex_micro_menu_cancel", "CONFIRM RESET", "ALERT LAYOUT", "STATUS DEMO"):
        require(menu_c, token, f"missing on-device menu behavior: {token}")
    for forbidden in ("VIA_ENABLE", "SERIAL_DRIVER", "Smart Action", "PROGRAM_EXEC"):
        assert forbidden not in combined, f"forbidden interface or action found: {forbidden}"

    print("Static validation passed: four configurable layers, 16 native controls, 25 controls per ordinary layer")
    print("Channel 2/3 split, A/B CRC storage plus isolated settings, moving AG LEDs, 303A:8360 report ID 6 and 64-byte reports verified")


if __name__ == "__main__":
    main()
