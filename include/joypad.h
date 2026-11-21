#ifndef JOYPAD_H
#define JOYPAD_H

#include <stdint.h>

struct GameBoy;

enum {
    JOYP_RIGHT  = 1 << 0,
    JOYP_LEFT   = 1 << 1,
    JOYP_UP     = 1 << 2,
    JOYP_DOWN   = 1 << 3,
    JOYP_A      = 1 << 4,
    JOYP_B      = 1 << 5,
    JOYP_SELECT = 1 << 6,
    JOYP_START  = 1 << 7
};

typedef struct {
    uint8_t pressed;
    uint8_t select;
} Joypad;

void joypad_init(Joypad *j);
void joypad_set(struct GameBoy *gb, uint8_t mask, int down);
uint8_t joypad_read(struct GameBoy *gb, uint16_t addr);
void joypad_write(struct GameBoy *gb, uint16_t addr, uint8_t val);

#endif
