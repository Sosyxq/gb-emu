#include "gb.h"
#include "timer.h"

static void tima_inc(struct GameBoy *gb)
{
    uint8_t tima = (uint8_t)(gb->io[0x05] + 1);
    if (tima == 0) {
        gb->io[0x05] = gb->io[0x06];
        gb->io[0x0F] |= 0x04;
    } else {
        gb->io[0x05] = tima;
    }
}

void timer_init(Timer *t)
{
    t->div_counter = 0xAB00;
}

void timer_step(struct GameBoy *gb, int cycles)
{
    Timer *t = &gb->timer;
    uint8_t tac = gb->io[0x07];
    static const int sel[4] = {9, 3, 5, 7};
    int bit = sel[tac & 3];

    for (int i = 0; i < cycles; i++) {
        uint16_t prev = t->div_counter;
        t->div_counter = (uint16_t)(prev + 1);
        if (!(tac & 0x04)) continue;
        if ((prev & (1u << bit)) && !(t->div_counter & (1u << bit)))
            tima_inc(gb);
    }
}

uint8_t timer_read(struct GameBoy *gb, uint16_t addr)
{
    switch (addr) {
    case 0xFF04: return (uint8_t)(gb->timer.div_counter >> 8);
    default: return gb->io[addr - 0xFF00];
    }
}

void timer_write(struct GameBoy *gb, uint16_t addr, uint8_t val)
{
    switch (addr) {
    case 0xFF04: {
        uint8_t tac = gb->io[0x07];
        if (tac & 0x04) {
            static const int sel[4] = {9, 3, 5, 7};
            int bit = sel[tac & 3];
            if (gb->timer.div_counter & (1u << bit)) tima_inc(gb);
        }
        gb->timer.div_counter = 0;
        break;
    }
    case 0xFF05:
        gb->io[0x05] = val;
        break;
    case 0xFF06:
        gb->io[0x06] = val;
        break;
    case 0xFF07:
        gb->io[0x07] = val & 0x07;
        break;
    default:
        gb->io[addr - 0xFF00] = val;
        break;
    }
}
