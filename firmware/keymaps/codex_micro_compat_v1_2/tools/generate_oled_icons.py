from pathlib import Path

from PIL import Image, ImageDraw


WIDTH = 128
HEIGHT = 32
ICON_X = 5
ICON_Y = 4
TEXT_X = 36
TEXT_Y = 9
TEXT_SCALE = 2

ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = ROOT / "assets"
ICON_DIR = ASSET_DIR / "icons-24"

LAYERS = (
    ("layer_icon_codex", "codex.png", "CODEX"),
    ("layer_icon_num", "number.png", "NUMPAD"),
    ("layer_icon_nav", "navigation.png", "NAV"),
    ("layer_icon_sys", "system.png", "SYSTEM"),
)

# A compact 5 x 7 uppercase bitmap alphabet. Only glyphs used by the four
# layer names are included so the layout remains deterministic and dependency-free.
FONT_5X7 = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "C": ("01111", "10000", "10000", "10000", "10000", "10000", "01111"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "X": ("10001", "01010", "00100", "00100", "00100", "01010", "10001"),
    "Y": ("10001", "01010", "00100", "00100", "00100", "00100", "00100"),
}


def load_icon(filename):
    source = Image.open(ICON_DIR / filename).convert("L")
    if source.size != (24, 24):
        raise ValueError(f"{filename} must be exactly 24 x 24 pixels")
    return source.point(lambda value: 1 if value < 160 else 0, mode="1")


def draw_label(image, label):
    draw = ImageDraw.Draw(image)
    advance = 6 * TEXT_SCALE
    for index, character in enumerate(label):
        glyph = FONT_5X7[character]
        origin_x = TEXT_X + index * advance
        for row, bits in enumerate(glyph):
            for column, bit in enumerate(bits):
                if bit == "1":
                    x = origin_x + column * TEXT_SCALE
                    y = TEXT_Y + row * TEXT_SCALE
                    draw.rectangle(
                        (x, y, x + TEXT_SCALE - 1, y + TEXT_SCALE - 1),
                        fill=1,
                    )


def layer_frame(icon_filename, label):
    image = Image.new("1", (WIDTH, HEIGHT), 0)
    image.paste(load_icon(icon_filename), (ICON_X, ICON_Y))
    draw_label(image, label)
    return image


def pack_oled(image):
    packed = []
    for page in range(HEIGHT // 8):
        for x in range(WIDTH):
            value = 0
            for bit in range(8):
                if image.getpixel((x, page * 8 + bit)):
                    value |= 1 << bit
            packed.append(value)
    return packed


def c_array(name, data):
    rows = []
    for offset in range(0, len(data), 16):
        rows.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset:offset + 16]))
    return f"static const char PROGMEM {name}[OLED_ICON_BYTES] = {{\n" + ",\n".join(rows) + "\n};\n"


def oled_source(arrays):
    return f'''#include QMK_KEYBOARD_H

#include "oled_icons.h"

#include <string.h>

#define OLED_ICON_BYTES 512
#define OLED_POPUP_COLS 21

{arrays}
static char popup_line1[OLED_POPUP_COLS + 1];
static char popup_line2[OLED_POPUP_COLS + 1];
static uint16_t popup_timer;
static uint16_t popup_duration;
static bool popup_active;
static bool display_dirty = true;
static uint8_t rendered_layer = 0xff;

static void copy_popup_line(char *target, const char *source) {{
    strncpy(target, source, OLED_POPUP_COLS);
    target[OLED_POPUP_COLS] = '\\0';
}}

void oled_controller_show_popup(const char *line1, const char *line2, uint16_t duration_ms) {{
    copy_popup_line(popup_line1, line1);
    copy_popup_line(popup_line2, line2);
    popup_timer = timer_read();
    popup_duration = duration_ms;
    popup_active = true;
    display_dirty = true;
}}

static const char *icon_for_layer(uint8_t layer) {{
    switch (layer) {{
        case 1: return layer_icon_num;
        case 2: return layer_icon_nav;
        case 3: return layer_icon_sys;
        default: return layer_icon_codex;
    }}
}}

void oled_controller_render(void) {{
    uint8_t active_layer = get_highest_layer(layer_state);

    if (popup_active && timer_elapsed(popup_timer) >= popup_duration) {{
        popup_active = false;
        display_dirty = true;
    }}

    if (popup_active) {{
        if (display_dirty) {{
            oled_clear();
            oled_set_cursor(0, 1);
            oled_write_ln(popup_line1, false);
            oled_write_ln(popup_line2, false);
            display_dirty = false;
        }}
        return;
    }}

    if (display_dirty || rendered_layer != active_layer) {{
        oled_clear();
        oled_set_cursor(0, 0);
        oled_write_raw_P(icon_for_layer(active_layer), OLED_ICON_BYTES);
        rendered_layer = active_layer;
        display_dirty = false;
    }}
}}
'''


def main():
    frames = []
    array_sources = []
    for array_name, icon_filename, label in LAYERS:
        frame = layer_frame(icon_filename, label)
        frames.append(frame)
        array_sources.append(c_array(array_name, pack_oled(frame)))

    preview = Image.new("1", (WIDTH * len(frames), HEIGHT), 0)
    for index, frame in enumerate(frames):
        preview.paste(frame, (index * WIDTH, 0))
    preview.save(ASSET_DIR / "layer-layout-preview.png")
    preview.resize((preview.width * 4, preview.height * 4), Image.Resampling.NEAREST).save(
        ASSET_DIR / "layer-layout-preview-4x.png"
    )

    (ROOT / "oled_icons.c").write_text(
        oled_source("\n".join(array_sources)),
        encoding="utf-8",
        newline="\n",
    )


if __name__ == "__main__":
    main()
