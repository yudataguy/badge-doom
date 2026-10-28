//
// Copyright(C) 2026
//
// Tufty/GitHub badge hardware bridge, implemented in badger_hw_tufty.c.
//

#ifndef BADGER_HW_H
#define BADGER_HW_H

#include <stdint.h>

uint32_t badger_read_buttons(void);
void badger_lcd_present_565(const uint16_t *framebuffer, int width, int height);

#if PICO_DOOM_BADGE_APP
// Reboot into MonaOS, which comes back up in its launcher menu.
void __attribute__((noreturn)) badger_return_to_launcher(void);
#endif

#endif
