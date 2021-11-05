#pragma once
#include "stdint.h"
void console_init(void);
void get_console(void);
void abandon_console(void);
void console_put_str(char *str);
void console_put_char(uint8_t char_asci);
void console_put_int(uint32_t num);