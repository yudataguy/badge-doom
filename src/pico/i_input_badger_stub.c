//
// Copyright(C) 2026
//
// Experimental Tufty/GitHub badge input scaffold.
//

#include "config.h"

#include <stdint.h>

#include "d_event.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "i_input.h"
#include "m_config.h"

float mouse_acceleration = 2.0f;
int mouse_threshold = 10;
int novert = 0;

#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
int vanilla_keyboard_mapping = true;
#endif

enum
{
    BADGER_BUTTON_UP   = 1u << 0,
    BADGER_BUTTON_DOWN = 1u << 1,
    BADGER_BUTTON_A    = 1u << 2,
    BADGER_BUTTON_B    = 1u << 3,
    BADGER_BUTTON_C    = 1u << 4,
    BADGER_BUTTON_HOME = 1u << 5,
};

static uint32_t previous_buttons;

uint32_t __attribute__((weak)) badger_read_buttons(void)
{
    return 0;
}

static void post_button_event(boolean pressed, int keycode)
{
    event_t event;

    event.type = pressed ? ev_keydown : ev_keyup;
    event.data1 = keycode;
    event.data2 = 0;
    event.data3 = 0;
    D_PostEvent(&event);
}

static void sync_button(uint32_t current_buttons, uint32_t mask, int keycode)
{
    if ((current_buttons ^ previous_buttons) & mask)
    {
        post_button_event((current_buttons & mask) != 0, keycode);
    }
}

void I_BindInputVariables(void)
{
#if !NO_USE_MOUSE
    M_BindFloatVariable("mouse_acceleration",      &mouse_acceleration);
    M_BindIntVariable("mouse_threshold",           &mouse_threshold);
#endif
#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
    M_BindIntVariable("vanilla_keyboard_mapping",  &vanilla_keyboard_mapping);
#endif
    M_BindIntVariable("novert",                    &novert);
}

void I_ReadMouse(void)
{
}

void I_StartTextInput(int x1, int y1, int x2, int y2)
{
    (void) x1;
    (void) y1;
    (void) x2;
    (void) y2;
}

void I_StopTextInput(void)
{
}

void I_InputInit(void)
{
    previous_buttons = badger_read_buttons();
}

void I_GetEvent(void)
{
    uint32_t current_buttons = badger_read_buttons();

    // Tufty badge mapping:
    // A=left, B=right, C=fire/use/select, UP/DOWN=forward/back, HOME=escape.
    sync_button(current_buttons, BADGER_BUTTON_UP, KEY_UPARROW);
    sync_button(current_buttons, BADGER_BUTTON_DOWN, KEY_DOWNARROW);
    sync_button(current_buttons, BADGER_BUTTON_A, KEY_LEFTARROW);
    sync_button(current_buttons, BADGER_BUTTON_B, KEY_RIGHTARROW);
    sync_button(current_buttons, BADGER_BUTTON_C, KEY_RCTRL);
    sync_button(current_buttons, BADGER_BUTTON_C, ' ');
    sync_button(current_buttons, BADGER_BUTTON_C, KEY_ENTER);
    sync_button(current_buttons, BADGER_BUTTON_HOME, KEY_ESCAPE);

    previous_buttons = current_buttons;
}

void I_GetEventTimeout(int key_timeout)
{
    (void) key_timeout;
    I_GetEvent();
}

int GetTypedChar(int scancode, boolean shiftdown)
{
    (void) scancode;
    (void) shiftdown;
    return 0;
}
