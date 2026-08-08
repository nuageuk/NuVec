#include "input.h"
#include <string.h>

enum {
    DIRECTION_UP    = 1u << 0,
    DIRECTION_DOWN  = 1u << 1,
    DIRECTION_LEFT  = 1u << 2,
    DIRECTION_RIGHT = 1u << 3
};

static InputState input;
static uint8_t held_directions;

static void set_button(uint8_t button, int pressed) {
    if (pressed) {
        input.buttons |= button;
    } else {
        input.buttons &= (uint8_t)~button;
    }
}

static void set_direction(uint8_t direction, int pressed) {
    if (pressed) {
        held_directions |= direction;
    } else {
        held_directions &= (uint8_t)~direction;
    }

    input.joystick_x = (held_directions & DIRECTION_LEFT) ? -128 :
                       (held_directions & DIRECTION_RIGHT) ? 127 : 0;
    input.joystick_y = (held_directions & DIRECTION_DOWN) ? -128 :
                       (held_directions & DIRECTION_UP) ? 127 : 0;
}

void input_reset(void) {
    memset(&input, 0, sizeof(input));
    held_directions = 0;
}

void input_handle_key(SDL_Keycode key, int pressed) {
    switch (key) {
        case INPUT_KEY_BUTTON_1: set_button(INPUT_BUTTON_1, pressed); return;
        case INPUT_KEY_BUTTON_2: set_button(INPUT_BUTTON_2, pressed); return;
        case INPUT_KEY_BUTTON_3: set_button(INPUT_BUTTON_3, pressed); return;
        case INPUT_KEY_BUTTON_4: set_button(INPUT_BUTTON_4, pressed); return;
        case INPUT_KEY_JOYSTICK_UP: set_direction(DIRECTION_UP, pressed); return;
        case INPUT_KEY_JOYSTICK_DOWN: set_direction(DIRECTION_DOWN, pressed); return;
        case INPUT_KEY_JOYSTICK_LEFT: set_direction(DIRECTION_LEFT, pressed); return;
        case INPUT_KEY_JOYSTICK_RIGHT: set_direction(DIRECTION_RIGHT, pressed); return;
        default: return;
    }
}

const InputState *input_get_state(void) {
    return &input;
}
