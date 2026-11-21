#ifndef CPU_H
#define CPU_H

#include <stdint.h>

struct GameBoy;

#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;
    uint16_t sp;
    uint16_t pc;
    int halted;
    int ime;
    int ei_delay;
    int halt_bug;
} CPU;

void cpu_init(CPU *cpu);
int cpu_step(struct GameBoy *gb);

#endif
