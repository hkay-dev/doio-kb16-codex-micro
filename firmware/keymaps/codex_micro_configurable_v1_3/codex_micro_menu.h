#pragma once

#include <stdbool.h>
#include <stdint.h>

void codex_micro_menu_init(void);
bool codex_micro_menu_active(void);
void codex_micro_menu_open(void);
void codex_micro_menu_save_close(void);
void codex_micro_menu_cancel(void);
void codex_micro_menu_navigate(bool clockwise);
void codex_micro_menu_adjust(bool clockwise);
void codex_micro_menu_select(void);
void codex_micro_menu_back(void);
void codex_micro_menu_lines(char lines[4][22]);
