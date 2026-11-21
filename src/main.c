#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apu.h"
#include "font8x8.h"
#include "gb.h"
#include "joypad.h"
#include "mmu.h"

#define SCALE 4

static void fill_demo(uint32_t *fb, int t)
{
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            int v = (((x + t) ^ y) + (t / 2)) & 0xFF;
            fb[y * SCREEN_W + x] = 0xFF000000u | ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)v;
        }
    }
}

static int key_mask(SDL_Keycode k)
{
    switch (k) {
    case SDLK_d: return JOYP_RIGHT;
    case SDLK_a: return JOYP_LEFT;
    case SDLK_w: return JOYP_UP;
    case SDLK_s: return JOYP_DOWN;
    case SDLK_z: return JOYP_A;
    case SDLK_x: return JOYP_B;
    case SDLK_RSHIFT: return JOYP_SELECT;
    case SDLK_RETURN: return JOYP_START;
    default: return 0;
    }
}

static void draw_text(SDL_Renderer *ren, int x, int y, uint32_t color, const char *s)
{
    SDL_SetRenderDrawColor(ren, (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, 0xFF);
    for (; *s; s++, x += 8) {
        const uint8_t *g = font8x8_glyph((unsigned char)*s);
        for (int row = 0; row < 8; row++)
            for (int bit = 0; bit < 8; bit++)
                if ((g[row] >> bit) & 1)
                    SDL_RenderDrawPoint(ren, x + bit, y + row);
    }
}

int main(int argc, char **argv)
{
    GameBoy *gb = calloc(1, sizeof(GameBoy));
    if (!gb) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    gb_init(gb);

    int has_rom = 0;
    if (argc > 1) {
        int rc = gb_load_rom(gb, argv[1]);
        if (rc == 0) {
            has_rom = 1;
            printf("Loaded: %s\n", argv[1]);
            cart_print_header(&gb->cart);
        } else if (rc == -2) {
            fprintf(stderr, "error: %s is too small to be a Game Boy ROM\n", argv[1]);
        } else {
            fprintf(stderr, "error: cannot open %s\n", argv[1]);
        }
    } else {
        printf("No ROM given - showing demo pattern.\n");
        printf("Usage: %s <game.gb>\n", argv[0]);
    }

    printf("\nControls: WASD = d-pad, Z = A, X = B, Enter = Start, RShift = Select, Tab = turbo, M = menu, Esc = quit\n\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        free(gb);
        return 1;
    }

    SDL_AudioDeviceID audio = 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want;
        memset(&want, 0, sizeof(want));
        want.freq = APU_SAMPLE_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (audio)
            SDL_PauseAudioDevice(audio, 0);
    }

    SDL_Window *win = SDL_CreateWindow(
        has_rom ? gb->cart.title : "gb-emu (demo)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W * SCALE, SCREEN_H * SCALE, 0);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        free(gb);
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);

    SDL_Texture *tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_W, SCREEN_H);

    int running = 1;
    int frame = 0;
    int turbo = 0;
    int menu = 0;
    static const int speeds[] = { 2, 4, 8, 16, 32 };
    int speed_idx = 2;
    int turbo_speed = speeds[speed_idx];
    uint32_t next_tick = SDL_GetTicks();

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                SDL_Keycode k = e.key.keysym.sym;
                int down = (e.type == SDL_KEYDOWN);

                if (down && !e.key.repeat && k == SDLK_m) {
                    menu = !menu;
                    if (menu) {
                        turbo = 0;
                        joypad_set(gb, 0xFF, 0); /* release all buttons */
                    }
                    continue;
                }

                if (menu) {
                    /* swallow game input; only handle menu keys */
                    if (!down && k == SDLK_TAB)
                        turbo = 0;
                    if (down && !e.key.repeat) {
                        if (k == SDLK_ESCAPE)
                            menu = 0;
                        else if (k == SDLK_a || k == SDLK_LEFT) {
                            if (speed_idx > 0) speed_idx--;
                        } else if (k == SDLK_d || k == SDLK_RIGHT) {
                            if (speed_idx < (int)(sizeof speeds / sizeof speeds[0]) - 1)
                                speed_idx++;
                        }
                        turbo_speed = speeds[speed_idx];
                    }
                    continue;
                }

                if (down && k == SDLK_ESCAPE)
                    running = 0;
                if (k == SDLK_TAB)
                    turbo = down;
                int m = key_mask(k);
                if (m)
                    joypad_set(gb, (uint8_t)m, down);
            }
        }

        int frames = turbo ? turbo_speed : 1;
        for (int i = 0; i < frames && running; i++) {
            if (has_rom) {
                gb_run_frame(gb);
            } else {
                fill_demo(gb->ppu.fb, frame);
                frame += 2;
            }
        }

        if (audio) {
            static int16_t abuf[2048 * 2];
            int n;
            if (turbo) {
                while (apu_drain(&gb->apu, abuf, 2048) > 0) {}
            } else {
                while ((n = apu_drain(&gb->apu, abuf, 2048)) > 0) {
                    if (SDL_GetQueuedAudioSize(audio) < (Uint32)(APU_SAMPLE_RATE * 2 * sizeof(int16_t) / 4))
                        SDL_QueueAudio(audio, abuf, (Uint32)(n * 2 * sizeof(int16_t)));
                }
            }
        }

        SDL_UpdateTexture(tex, NULL, gb->ppu.fb, SCREEN_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);

        if (menu) {
            char line[32];
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 190);
            SDL_Rect panel = { 8, 40, 144, 52 };
            SDL_RenderFillRect(ren, &panel);
            SDL_SetRenderDrawColor(ren, 80, 80, 80, 255);
            SDL_RenderDrawRect(ren, &panel);
            draw_text(ren, 16, 46, 0xFFFFFF00, "GB-EMU MENU");
            snprintf(line, sizeof line, "> SPEED [%dX]", turbo_speed);
            draw_text(ren, 16, 60, 0xFFFFFFFF, line);
            draw_text(ren, 16, 76, 0xFFAAAAAA, "A/D SET  M CLOSE");
        }

        SDL_RenderPresent(ren);

        if (!turbo) {
            next_tick += 16;
            uint32_t now = SDL_GetTicks();
            if (next_tick > now)
                SDL_Delay(next_tick - now);
            else
                next_tick = now;
        } else {
            next_tick = SDL_GetTicks();
        }
    }

    if (audio)
        SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    cart_free(&gb->cart); /* writes .sav if battery cart */
    free(gb);
    return 0;
}
