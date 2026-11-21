#ifndef PPU_H
#define PPU_H

#include <stdint.h>

#define SCREEN_W 160
#define SCREEN_H 144

struct GameBoy;

typedef struct {
    uint32_t fb[SCREEN_W * SCREEN_H];
    uint8_t ly;
    uint8_t mode;
    int mode_clock;
    int frame_ready;
    int lcd_off_clock;
    int win_line;
} PPU;

void ppu_init(PPU *ppu);
void ppu_step(struct GameBoy *gb, int cycles);
uint8_t ppu_read(struct GameBoy *gb, uint16_t addr);
void ppu_write(struct GameBoy *gb, uint16_t addr, uint8_t val);

#endif
