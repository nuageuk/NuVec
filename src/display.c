#include "display.h"
#include <SDL.h>
#include <stdio.h>

#define DISPLAY_WIDTH  600
#define DISPLAY_HEIGHT 400

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

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

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        window = NULL;
        SDL_Quit();
        return 0;
    }

    return 1;
}

void display_clear(void) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

void display_draw_line(int x1, int y1, int x2, int y2) {
    SDL_SetRenderDrawColor(renderer, 60, 255, 120, 255); // phosphor-ish green
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

void display_present(void) {
    SDL_RenderPresent(renderer);
}

void display_shutdown(void) {
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
