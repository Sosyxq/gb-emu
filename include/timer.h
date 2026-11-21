#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

struct GameBoy;

typedef struct {
    uint16_t div_counter;
} Timer;

void timer_init(Timer *t);
void timer_step(struct GameBoy *gb, int cycles);
uint8_t timer_read(struct GameBoy *gb, uint16_t addr);
void timer_write(struct GameBoy *gb, uint16_t addr, uint8_t val);

#endif
