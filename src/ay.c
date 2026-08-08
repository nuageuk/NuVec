#include "ay.h"
#include <SDL.h>

#define AY_REGISTER_COUNT 16
#define AY_SAMPLE_RATE 44100
#define AY_BUFFER_SAMPLES 512

static uint8_t registers[AY_REGISTER_COUNT];
static uint8_t selected_register;
static SDL_AudioDeviceID audio_device;

static void audio_callback(void *userdata, Uint8 *stream, int length) {
    (void)userdata;
    SDL_memset(stream, 0, (size_t)length);
}

int ay_init(void) {
    SDL_AudioSpec desired = { 0 };

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        return 0;
    }

    desired.freq = AY_SAMPLE_RATE;
    desired.format = AUDIO_F32SYS;
    desired.channels = 1;
    desired.samples = AY_BUFFER_SAMPLES;
    desired.callback = audio_callback;

    audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
    if (!audio_device) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }

    SDL_PauseAudioDevice(audio_device, 0);
    return 1;
}

void ay_shutdown(void) {
    if (audio_device) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void ay_write_addr(uint8_t address) {
    selected_register = address & 0x0F;
}

void ay_write_data(uint8_t value) {
    registers[selected_register] = value;
}

uint8_t ay_read_data(void) {
    return registers[selected_register];
}
