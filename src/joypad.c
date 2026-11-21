#include "gb.h"
#include "joypad.h"

void joypad_init(Joypad *j)
{
    j->pressed = 0;
    j->select = 0x30;
}

void joypad_set(struct GameBoy *gb, uint8_t mask, int down)
{
    uint8_t old = gb->joypad.pressed;
    if (down)
        gb->joypad.pressed |= mask;
    else
        gb->joypad.pressed &= (uint8_t)~mask;

    if (down && !(old & mask)) gb->io[0x0F] |= 0x10;
}

uint8_t joypad_read(struct GameBoy *gb, uint16_t addr)
{
    (void)addr;
    uint8_t select = gb->joypad.select;
    uint8_t low = 0x0F;

    if (!(select & 0x10)) low &= (uint8_t)~(gb->joypad.pressed & 0x0F);
    if (!(select & 0x20)) low &= (uint8_t)~((gb->joypad.pressed >> 4) & 0x0F);

    return (uint8_t)(0xC0 | select | low);
}

void joypad_write(struct GameBoy *gb, uint16_t addr, uint8_t val)
{
    (void)addr;
    gb->joypad.select = (uint8_t)(val & 0x30);
    gb->io[0x00] = (uint8_t)(0xC0 | gb->joypad.select | 0x0F);
}
