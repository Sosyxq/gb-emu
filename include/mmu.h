#ifndef MMU_H
#define MMU_H

#include <stdint.h>

struct GameBoy;

void mmu_init(struct GameBoy *gb);
uint8_t mmu_read(struct GameBoy *gb, uint16_t addr);
void mmu_write(struct GameBoy *gb, uint16_t addr, uint8_t val);
void mmu_hblank_hdma(struct GameBoy *gb);

#endif
