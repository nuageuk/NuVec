#include "display.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *decay_texture = NULL;
static int logical_width = DISPLAY_WIDTH;
static int logical_height = DISPLAY_HEIGHT;

typedef struct {
    int x1, y1, x2, y2;
    uint8_t brightness;
} DisplayLine;

typedef struct {
    DisplayLine *lines;
    size_t first;
    size_t count;
    size_t capacity;
    uint64_t timestamp;
} HistoryFrame;

static HistoryFrame history[DECAY_FRAMES];
static HistoryFrame *capture_frame = NULL;
static uint64_t frame_counter = 0;
static DisplayDecayMode decay_mode = DECAY_ON;
static DisplayBloomMode bloom_mode = BLOOM_ON;
static DisplayVsyncMode vsync_mode = VSYNC_ON;

static void clear_renderer(void) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

static void fitted_render_size(int window_width, int window_height,
                               int *render_width, int *render_height) {
    if (window_width * DISPLAY_HEIGHT > window_height * DISPLAY_WIDTH) {
        *render_height = window_height;
        *render_width = window_height * DISPLAY_WIDTH / DISPLAY_HEIGHT;
    } else {
        *render_width = window_width;
        *render_height = window_width * DISPLAY_HEIGHT / DISPLAY_WIDTH;
    }
}

static int recreate_decay_texture(int width, int height) {
    SDL_Texture *new_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                                 SDL_TEXTUREACCESS_TARGET,
                                                 width, height);
    if (!new_texture) {
        return 0;
    }

    SDL_SetTextureBlendMode(new_texture, SDL_BLENDMODE_NONE);
    if (decay_texture) {
        SDL_DestroyTexture(decay_texture);
    }
    decay_texture = new_texture;
    return 1;
}

static void destroy_renderer_resources(void) {
    if (renderer) {
        SDL_SetRenderTarget(renderer, NULL);
    }
    if (decay_texture) {
        SDL_DestroyTexture(decay_texture);
        decay_texture = NULL;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
}

static int create_renderer(DisplayVsyncMode mode) {
    Uint32 flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
    if (mode == VSYNC_ON) {
        flags |= SDL_RENDERER_PRESENTVSYNC;
    }

    renderer = SDL_CreateRenderer(window, -1, flags);
    if (!renderer) {
        return 0;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (SDL_RenderSetLogicalSize(renderer, logical_width, logical_height) != 0 ||
        !recreate_decay_texture(logical_width, logical_height)) {
        destroy_renderer_resources();
        return 0;
    }

    return 1;
}

static int recreate_renderer(DisplayVsyncMode mode) {
    destroy_renderer_resources();
    return create_renderer(mode);
}

static int scale_coordinate(int value, int logical_size, int reference_size) {
    return (int)((int64_t)value * logical_size / reference_size);
}

static void render_line(int x1, int y1, int x2, int y2, uint8_t brightness,
                        int apply_bloom) {
    if (apply_bloom && bloom_mode == BLOOM_ON) {
        static const int offsets[4][2] = {
            { -BLOOM_OFFSET, 0 },
            {  BLOOM_OFFSET, 0 },
            { 0, -BLOOM_OFFSET },
            { 0,  BLOOM_OFFSET }
        };

        SDL_SetRenderDrawColor(renderer, brightness, brightness, brightness, BLOOM_ALPHA);
        for (size_t i = 0; i < 4; i++) {
            SDL_RenderDrawLine(renderer,
                               x1 + offsets[i][0], y1 + offsets[i][1],
                               x2 + offsets[i][0], y2 + offsets[i][1]);
        }
    }

    SDL_SetRenderDrawColor(renderer, brightness, brightness, brightness, 255);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

static void free_history(void) {
    for (size_t i = 0; i < DECAY_FRAMES; i++) {
        free(history[i].lines);
        history[i].lines = NULL;
        history[i].first = 0;
        history[i].count = 0;
        history[i].capacity = 0;
        history[i].timestamp = 0;
    }
    capture_frame = NULL;
}

static void purge_expired_history(void) {
    if (frame_counter < DECAY_FRAMES) {
        return;
    }

    HistoryFrame *expired = &history[(frame_counter - DECAY_FRAMES) % DECAY_FRAMES];
    expired->first = 0;
    expired->count = 0;
    expired->timestamp = 0;
}

static void capture_line(int x1, int y1, int x2, int y2, uint8_t brightness) {
    if (!capture_frame) {
        return;
    }

    if (capture_frame->count == capture_frame->capacity &&
        capture_frame->capacity < DECAY_MAX_SEGMENTS) {
        size_t new_capacity = capture_frame->capacity ? capture_frame->capacity * 2 : 1024;
        if (new_capacity > DECAY_MAX_SEGMENTS) {
            new_capacity = DECAY_MAX_SEGMENTS;
        }
        DisplayLine *new_lines = realloc(capture_frame->lines,
                                         new_capacity * sizeof(*new_lines));
        if (new_lines) {
            capture_frame->lines = new_lines;
            capture_frame->capacity = new_capacity;
        }
    }

    if (capture_frame->capacity == 0) {
        return;
    }

    size_t index;
    if (capture_frame->count < capture_frame->capacity) {
        index = (capture_frame->first + capture_frame->count) % capture_frame->capacity;
        capture_frame->count++;
    } else {
        index = capture_frame->first;
        capture_frame->first = (capture_frame->first + 1) % capture_frame->capacity;
    }

    DisplayLine *line = &capture_frame->lines[index];
    line->x1 = x1;
    line->y1 = y1;
    line->x2 = x2;
    line->y2 = y2;
    line->brightness = brightness;
}

static void trim_decay_history(uint64_t frame_count) {
    size_t total = 0;
    for (uint64_t offset = frame_count; offset > 0; offset--) {
        uint64_t timestamp = frame_counter - (offset - 1);
        HistoryFrame *frame = &history[timestamp % DECAY_FRAMES];
        if (frame->timestamp == timestamp) {
            total += frame->count;
        }
    }

    if (total <= DECAY_MAX_SEGMENTS) {
        return;
    }

    size_t drop = total - DECAY_MAX_SEGMENTS;
    for (uint64_t offset = frame_count; offset > 0 && drop > 0; offset--) {
        uint64_t timestamp = frame_counter - (offset - 1);
        HistoryFrame *frame = &history[timestamp % DECAY_FRAMES];
        if (frame->timestamp != timestamp) {
            continue;
        }

        size_t frame_drop = drop < frame->count ? drop : frame->count;
        if (frame_drop > 0) {
            frame->first = (frame->first + frame_drop) % frame->capacity;
        }
        frame->count -= frame_drop;
        drop -= frame_drop;
    }
}

static void render_decay_history(void) {
    uint64_t frame_count = frame_counter + 1;
    if (frame_count > DECAY_FRAMES) {
        frame_count = DECAY_FRAMES;
    }
    trim_decay_history(frame_count);

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
            DisplayLine *line = &frame->lines[(frame->first + i) % frame->capacity];
            Uint8 brightness = (Uint8)((line->brightness * remaining) / DECAY_FRAMES);
            render_line(scale_coordinate(line->x1, logical_width, DISPLAY_WIDTH),
                        scale_coordinate(line->y1, logical_height, DISPLAY_HEIGHT),
                        scale_coordinate(line->x2, logical_width, DISPLAY_WIDTH),
                        scale_coordinate(line->y2, logical_height, DISPLAY_HEIGHT),
                        brightness, age == 0);
        }
    }

    SDL_SetRenderTarget(renderer, NULL);
    clear_renderer();
    SDL_Rect destination = { 0, 0, logical_width, logical_height };
    SDL_RenderCopy(renderer, decay_texture, NULL, &destination);
}

int display_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }

    window = SDL_CreateWindow("NuVec",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               DISPLAY_WIDTH, DISPLAY_HEIGHT,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }

    if (!create_renderer(VSYNC_ON)) {
        fprintf(stderr, "SDL renderer setup failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        window = NULL;
        SDL_Quit();
        return 0;
    }

    return 1;
}

void display_clear(void) {
    if (decay_mode == DECAY_ON) {
        capture_frame = &history[frame_counter % DECAY_FRAMES];
        capture_frame->first = 0;
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

    render_line(scale_coordinate(x1, logical_width, DISPLAY_WIDTH),
                scale_coordinate(y1, logical_height, DISPLAY_HEIGHT),
                scale_coordinate(x2, logical_width, DISPLAY_WIDTH),
                scale_coordinate(y2, logical_height, DISPLAY_HEIGHT),
                brightness, 1);
}

void display_present(void) {
    if (decay_mode == DECAY_ON) {
        render_decay_history();
        capture_frame = NULL;
        frame_counter++;
        purge_expired_history();
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

void display_toggle_bloom(void) {
    bloom_mode = bloom_mode == BLOOM_ON ? BLOOM_OFF : BLOOM_ON;
}

DisplayVsyncMode display_toggle_vsync(void) {
    DisplayVsyncMode requested = vsync_mode == VSYNC_ON ? VSYNC_OFF : VSYNC_ON;
    if (recreate_renderer(requested)) {
        vsync_mode = requested;
    } else {
        recreate_renderer(vsync_mode);
    }
    return vsync_mode;
}

void display_resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    int render_width, render_height;
    fitted_render_size(width, height, &render_width, &render_height);
    if (SDL_RenderSetLogicalSize(renderer, render_width, render_height) != 0) {
        return;
    }

    logical_width = render_width;
    logical_height = render_height;
    recreate_decay_texture(render_width, render_height);
}

void display_shutdown(void) {
    free_history();
    destroy_renderer_resources();
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_Quit();
}
