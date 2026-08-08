/* Keyboard mappings and controller state exposed to the VIA. */

#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include <SDL.h>

#define INPUT_KEY_BUTTON_1 SDLK_z
#define INPUT_KEY_BUTTON_2 SDLK_x
#define INPUT_KEY_BUTTON_3 SDLK_c
#define INPUT_KEY_BUTTON_4 SDLK_v

#define INPUT_KEY_JOYSTICK_UP    SDLK_UP
#define INPUT_KEY_JOYSTICK_DOWN  SDLK_DOWN
#define INPUT_KEY_JOYSTICK_LEFT  SDLK_LEFT
#define INPUT_KEY_JOYSTICK_RIGHT SDLK_RIGHT

#define INPUT_BUTTON_1 (1u << 0)
#define INPUT_BUTTON_2 (1u << 1)
#define INPUT_BUTTON_3 (1u << 2)
#define INPUT_BUTTON_4 (1u << 3)

typedef struct {
    uint8_t buttons;
    int8_t joystick_x;
    int8_t joystick_y;
} InputState;

/* Clears all held buttons and joystick directions. */
void input_reset(void);

/* Applies one SDL key transition to the emulated controller state. */
void input_handle_key(SDL_Keycode key, int pressed);

/* Returns the current read-only controller state. */
const InputState *input_get_state(void);

#endif /* INPUT_H */
