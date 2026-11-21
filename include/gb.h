#ifndef GB_H
#define GB_H

#include <stdint.h>

#include "apu.h"
#include "cart.h"
#include "cpu.h"
#include "joypad.h"
#include "ppu.h"
#include "timer.h"

#define SERIAL_CAP 8192

typedef struct GameBoy {
    CPU cpu;
    PPU ppu;
    Timer timer;
    Joypad joypad;
    APU apu;
    Cart cart;
    int cgb_mode;
    uint8_t vram[2][0x2000];
    uint8_t wram[8][0x1000];
    uint8_t oam[0xA0];
    uint8_t hram[0x7F];
    uint8_t io[0x80];
    uint8_t ie;
    uint8_t vbk;
    uint8_t svbk;
    uint8_t bg_pal[64];
    uint8_t ob_pal[64];
    uint8_t bcps;
    uint8_t ocps;
    int double_speed;
    int key1_armed;
    int hdma_active;
    int hdma_blocks;
    char serial[SERIAL_CAP];
    int serial_len;
} GameBoy;

void gb_init(GameBoy *gb);
int gb_load_rom(GameBoy *gb, const char *path);
void gb_step(GameBoy *gb);
void gb_run_frame(GameBoy *gb);

#endif
