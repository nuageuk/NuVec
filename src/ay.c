#include "ay.h"
#include <SDL.h>

#define AY_REGISTER_COUNT 16
#define AY_CHANNEL_COUNT 3
#define AY_CLOCK_RATE 93750
#define AY_SAMPLE_RATE 44100
#define AY_BUFFER_SAMPLES 512
#define AY_NOISE_PERIOD_REGISTER 6
#define AY_MIXER_REGISTER 7
#define AY_AMPLITUDE_REGISTER 8
#define AY_NOISE_LFSR_SEED 0x1FFFFu
#define AY_VIA_MODE_INACTIVE 0x00
#define AY_VIA_MODE_WRITE_DATA 0x02
#define AY_VIA_MODE_LATCH_ADDRESS 0x03

static uint8_t registers[AY_REGISTER_COUNT];
static uint8_t selected_register;
static SDL_AudioDeviceID audio_device;
static uint16_t tone_counters[AY_CHANNEL_COUNT];
static uint8_t tone_outputs[AY_CHANNEL_COUNT];
static uint16_t noise_counter;
static uint32_t noise_lfsr;
static uint32_t tone_clock_accumulator;
static uint8_t last_ora;
static uint8_t previous_mode;

static uint16_t tone_period(int channel) {
    int fine_register = channel * 2;
    uint16_t period = registers[fine_register] |
                      ((uint16_t)(registers[fine_register + 1] & 0x0F) << 8);
    return period ? period : 1;
}

static void clock_tones(void) {
    for (int channel = 0; channel < AY_CHANNEL_COUNT; channel++) {
        if (tone_counters[channel] == 0) {
            tone_counters[channel] = tone_period(channel);
        }

        tone_counters[channel]--;
        if (tone_counters[channel] == 0) {
            tone_outputs[channel] ^= 1;
        }
    }
}

static uint8_t noise_period(void) {
    uint8_t period = registers[AY_NOISE_PERIOD_REGISTER] & 0x1F;
    return period ? period : 1;
}

static void clock_noise(void) {
    if (noise_counter == 0) {
        noise_counter = (uint16_t)noise_period() * 2;
    }

    noise_counter--;
    if (noise_counter == 0) {
        uint32_t feedback = (noise_lfsr ^ (noise_lfsr >> 3)) & 1u;
        noise_lfsr = (noise_lfsr >> 1) | (feedback << 16);
    }
}

static float mix_channels(void) {
    float mixed = 0.0f;
    uint8_t mixer = registers[AY_MIXER_REGISTER];
    uint8_t noise_output = noise_lfsr & 1u;

    for (int channel = 0; channel < AY_CHANNEL_COUNT; channel++) {
        uint8_t tone_disabled = (mixer >> channel) & 1u;
        uint8_t noise_disabled = (mixer >> (channel + 3)) & 1u;
        uint8_t channel_output = (tone_outputs[channel] | tone_disabled) &
                                 (noise_output | noise_disabled);
        float amplitude = (float)(registers[AY_AMPLITUDE_REGISTER + channel] & 0x0F) /
                          15.0f;
        mixed += channel_output ? amplitude : -amplitude;
    }

    return mixed / (float)AY_CHANNEL_COUNT;
}

static void audio_callback(void *userdata, Uint8 *stream, int length) {
    (void)userdata;

    float *samples = (float *)stream;
    int sample_count = length / (int)sizeof(*samples);
    for (int sample = 0; sample < sample_count; sample++) {
        tone_clock_accumulator += AY_CLOCK_RATE;
        while (tone_clock_accumulator >= AY_SAMPLE_RATE) {
            tone_clock_accumulator -= AY_SAMPLE_RATE;
            clock_tones();
            clock_noise();
        }
        samples[sample] = mix_channels();
    }
}

int ay_init(void) {
    SDL_AudioSpec desired = { 0 };

    SDL_memset(tone_counters, 0, sizeof(tone_counters));
    SDL_memset(tone_outputs, 0, sizeof(tone_outputs));
    noise_counter = 0;
    noise_lfsr = AY_NOISE_LFSR_SEED;
    tone_clock_accumulator = 0;
    last_ora = 0;
    previous_mode = AY_VIA_MODE_INACTIVE;

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
    uint8_t register_index = address & 0x0F;

    if (audio_device) {
        SDL_LockAudioDevice(audio_device);
    }
    selected_register = register_index;
    if (audio_device) {
        SDL_UnlockAudioDevice(audio_device);
    }
}

void ay_write_data(uint8_t value) {
    uint8_t register_index;

    if (audio_device) {
        SDL_LockAudioDevice(audio_device);
    }
    register_index = selected_register;
    registers[register_index] = value;
    if (audio_device) {
        SDL_UnlockAudioDevice(audio_device);
    }
}

uint8_t ay_read_data(void) {
    uint8_t value;

    if (audio_device) {
        SDL_LockAudioDevice(audio_device);
    }
    value = registers[selected_register];
    if (audio_device) {
        SDL_UnlockAudioDevice(audio_device);
    }

    return value;
}

void ay_via_orb(uint8_t orb) {
    uint8_t mode = (orb >> 3) & 0x03;

    if (mode == previous_mode) {
        return;
    }
    previous_mode = mode;

    if (mode == AY_VIA_MODE_WRITE_DATA) {
        ay_write_data(last_ora);
    } else if (mode == AY_VIA_MODE_LATCH_ADDRESS) {
        ay_write_addr(last_ora);
    }
}

void ay_via_ora(uint8_t ora) {
    last_ora = ora;
}
