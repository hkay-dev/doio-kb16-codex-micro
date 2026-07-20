from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
KEYMAP = ROOT / "keymap.c"
PROTOCOL = ROOT / "codex_micro_protocol.h"


def layout_entries(source, layer_name):
    pattern = rf"\[{re.escape(layer_name)}\]\s*=\s*LAYOUT\("
    match = re.search(pattern, source)
    if not match:
        raise AssertionError(f"Missing LAYOUT for {layer_name}")

    index = match.end()
    depth = 1
    start = index
    while depth:
        char = source[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        index += 1

    body = source[start:index - 1]
    entries = []
    token_start = 0
    token_depth = 0
    for offset, char in enumerate(body):
        if char == "(":
            token_depth += 1
        elif char == ")":
            token_depth -= 1
        elif char == "," and token_depth == 0:
            entries.append(body[token_start:offset].strip())
            token_start = offset + 1
    entries.append(body[token_start:].strip())
    return entries


def main():
    source = KEYMAP.read_text(encoding="utf-8")
    expected_layers = ["_CODEX", "_NUM", "_NAV", "_SYS"]

    expected_codex = [
        "CDX_TASK_1", "CDX_TASK_2", "CDX_JOYSTICK_UP", "CDX_JOYSTICK_RIGHT", "CDX_ENCODER_PRESS",
        "CDX_TASK_3", "CDX_TASK_4", "CDX_JOYSTICK_LEFT", "CDX_JOYSTICK_DOWN", "CDX_REASON_CONFIRM",
        "CDX_TASK_5", "CDX_TASK_6", "CDX_ACTION_06", "CDX_ACTION_07", "LAYER_CYCLE",
        "CDX_ACTION_08", "CDX_ACTION_09", "CDX_ACTION_12", "CDX_ACTION_MIC",
    ]
    assert layout_entries(source, "_CODEX") == expected_codex, "_CODEX: native 16-key layout mismatch"

    for layer in expected_layers:
        entries = layout_entries(source, layer)
        assert len(entries) == 19, f"{layer}: expected 19 entries, found {len(entries)}"
        presses = [entries[4], entries[9], entries[14]]
        assert entries[4] == "CDX_ENCODER_PRESS", f"{layer}: physical left press must be the native encoder"
        assert entries[14] == "LAYER_CYCLE", f"{layer}: physical middle press must cycle layers"
        print(f"{layer}: 19 entries; physical left/right/middle presses={presses}")

    expected_right_presses = {
        "_CODEX": "CDX_REASON_CONFIRM",
        "_NUM": "KC_EQUAL",
        "_NAV": "KC_ENTER",
        "_SYS": "RM_TOGG",
    }
    for layer, expected in expected_right_presses.items():
        assert layout_entries(source, layer)[9] == expected, f"{layer}: physical right press changed"

    native_left_encoder = "ENCODER_CCW_CW(CDX_ENCODER_CCW, CDX_ENCODER_CW)"
    assert source.count(native_left_encoder) == 4, "Physical left encoder must be native on all four layers"
    for unchanged in (
        "ENCODER_CCW_CW(CDX_REASON_DOWN, CDX_REASON_UP)",
        "ENCODER_CCW_CW(MS_WHLD, MS_WHLU)",
        "ENCODER_CCW_CW(KC_LEFT, KC_RIGHT)",
        "ENCODER_CCW_CW(KC_UP, KC_DOWN)",
        "ENCODER_CCW_CW(RM_PREV, RM_NEXT)",
        "ENCODER_CCW_CW(RM_VALD, RM_VALU)",
    ):
        assert unchanged in source, f"Right/middle encoder mapping changed: {unchanged}"

    forbidden = ["VIA_ENABLE", "SERIAL_DRIVER", "KC_F13", "KC_F14", "KC_F15", "KC_F16", "KC_F17", "KC_F18", "KC_F19", "KC_F20", "KC_F21", "KC_F22", "KC_F23", "KC_F24"]
    combined = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (ROOT / "keymap.c", ROOT / "config.h", ROOT / "rules.mk", ROOT / "codex_micro_protocol.c", PROTOCOL)
    )
    leaks = [token for token in forbidden if token in combined]
    assert not leaks, f"Forbidden interface or placeholder keycodes found: {leaks}"
    rules = (ROOT / "rules.mk").read_text(encoding="utf-8")
    config = (ROOT / "config.h").read_text(encoding="utf-8")
    protocol = PROTOCOL.read_text(encoding="utf-8")
    assert "RAW_ENABLE = yes" in rules, "Raw HID must be enabled"
    expected = {
        "#define VENDOR_ID 0x303A",
        "#define PRODUCT_ID 0x8360",
        "#define DEVICE_VER 0x0100",
        "#define RAW_USAGE_PAGE 0xFF00",
        "#define RAW_USAGE_ID 0x01",
        "#define RAW_REPORT_ID 0x06",
        "#define RAW_REPORT_COUNT 63",
        "#define RAW_EPSIZE 64",
    }
    missing = sorted(token for token in expected if token not in config)
    assert not missing, f"Missing Codex Micro USB identity fields: {missing}"
    assert "#define CODEX_MICRO_REPORT_SIZE 64" in protocol
    assert "#define CODEX_MICRO_FRAGMENT_SIZE 61" in protocol
    assert "#define CODEX_MICRO_HOST_SLOT_COUNT 6" in protocol
    assert "#define CODEX_MICRO_TASK_KEY_COUNT 6" in protocol
    protocol_source = (ROOT / "codex_micro_protocol.c").read_text(encoding="utf-8")
    assert "{0, 1, 4, 5, 8, 9}" in protocol_source, "Six physical task LED indices are not configured"
    for token in ("AG%02u", "ACT06", "ACT07", "ACT08", "ACT09", "ACT12", "ACT10", "ENC_CW", "ENC_CC", "ENC_SW", "v.oai.rad"):
        assert token in protocol_source, f"Missing native protocol token: {token}"
    assert "ACT11" not in protocol_source, "ACT11 must not be emitted"
    print("Interface check: Codex Micro 303A:8360 / FF00 / report ID 6; six agents, native ACT/joystick/encoder; no ACT11, VIA, serial, or F13-F24 placeholders")


if __name__ == "__main__":
    main()
