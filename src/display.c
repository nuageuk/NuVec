#include "display.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *decay_texture = NULL;

typedef struct {
    int x1, y1, x2, y2;
    uint8_t brightness;
} DisplayLine;

typedef struct {
    DisplayLine *lines;
    size_t count;
    size_t capacity;
    uint64_t timestamp;
} HistoryFrame;

static HistoryFrame history[DECAY_FRAMES];
static HistoryFrame *capture_frame = NULL;
static uint64_t frame_counter = 0;
static DisplayDecayMode decay_mode = DECAY_ON;

static void clear_renderer(void) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

static void free_history(void) {
    for (size_t i = 0; i < DECAY_FRAMES; i++) {
        free(history[i].lines);
        history[i].lines = NULL;
        history[i].count = 0;
        history[i].capacity = 0;
        history[i].timestamp = 0;
    }
    capture_frame = NULL;
}

static void capture_line(int x1, int y1, int x2, int y2, uint8_t brightness) {
    if (!capture_frame) {
        return;
    }

    if (capture_frame->count == capture_frame->capacity) {
        size_t new_capacity = capture_frame->capacity ? capture_frame->capacity * 2 : 1024;
        DisplayLine *new_lines = realloc(capture_frame->lines,
                                         new_capacity * sizeof(*new_lines));
        if (!new_lines) {
            return;
        }
        capture_frame->lines = new_lines;
        capture_frame->capacity = new_capacity;
    }

    DisplayLine *line = &capture_frame->lines[capture_frame->count++];
    line->x1 = x1;
    line->y1 = y1;
    line->x2 = x2;
    line->y2 = y2;
    line->brightness = brightness;
}

static void render_decay_history(void) {
    uint64_t frame_count = frame_counter + 1;
    if (frame_count > DECAY_FRAMES) {
        frame_count = DECAY_FRAMES;
    }

    SDL_SetRenderTarget(renderer, decay_texture);
    clear_renderer();

    // Oldest first so a newer, brighter copy of the same vector wins.
    for (uint64_t offset = frame_count; offset > 0; offset--) {
        uint64_t age = offset - 1;
        uint64_t timestamp = frame_counter - age;
        HistoryFrame *frame = &history[timestamp % DECAY_FRAMES];
        if (frame->timestamp != timestamp) {
            continue;
        }

        unsigned remaining = DECAY_FRAMES - (unsigned)age;
        for (size_t i = 0; i < frame->count; i++) {
            DisplayLine *line = &frame->lines[i];
            Uint8 brightness = (Uint8)((line->brightness * remaining) / DECAY_FRAMES);
            SDL_SetRenderDrawColor(renderer, brightness, brightness, brightness, 255);
            SDL_RenderDrawLine(renderer, line->x1, line->y1, line->x2, line->y2);
        }
    }

    SDL_SetRenderTarget(renderer, NULL);
    clear_renderer();
    SDL_RenderCopy(renderer, decay_texture, NULL, NULL);
}

int display_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }

    window = SDL_CreateWindow("NuVec",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               DISPLAY_WIDTH, DISPLAY_HEIGHT,
                               SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }

    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        window = NULL;
        SDL_Quit();
        return 0;
    }

    decay_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET,
                                      DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!decay_texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
        SDL_DestroyWindow(window);
        window = NULL;
        SDL_Quit();
        return 0;
    }
    SDL_SetTextureBlendMode(decay_texture, SDL_BLENDMODE_NONE);

    return 1;
}

void display_clear(void) {
    if (decay_mode == DECAY_ON) {
        capture_frame = &history[frame_counter % DECAY_FRAMES];
        capture_frame->count = 0;
        capture_frame->timestamp = frame_counter;
        return;
    }

    SDL_SetRenderTarget(renderer, NULL);
    clear_renderer();
}

void display_draw_line(int x1, int y1, int x2, int y2, uint8_t brightness) {
    if (decay_mode == DECAY_ON) {
        capture_line(x1, y1, x2, y2, brightness);
        return;
    }

    SDL_SetRenderDrawColor(renderer, brightness, brightness, brightness, 255);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

void display_present(void) {
    if (decay_mode == DECAY_ON) {
        render_decay_history();
        capture_frame = NULL;
        frame_counter++;
    }
    SDL_RenderPresent(renderer);
}

void display_toggle_decay(void) {
    if (decay_mode == DECAY_ON) {
        decay_mode = DECAY_OFF;
        free_history();
    } else {
        decay_mode = DECAY_ON;
        frame_counter = 0;
    }
}

void display_shutdown(void) {
    free_history();
    if (decay_texture) {
        SDL_DestroyTexture(decay_texture);
        decay_texture = NULL;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_Quit();
}
