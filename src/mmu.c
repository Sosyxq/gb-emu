#include <string.h>

#include "apu.h"
#include "cart.h"
#include "gb.h"
#include "joypad.h"
#include "mmu.h"
#include "ppu.h"
#include "timer.h"

static void hdma_copy_block(struct GameBoy *gb)
{
    uint16_t src = (uint16_t)((gb->io[0x51] << 8) | (gb->io[0x52] & 0xF0));
    uint16_t dst = (uint16_t)(0x8000 | ((gb->io[0x53] & 0x1F) << 8) | (gb->io[0x54] & 0xF0));
    int bank = gb->vbk & 1;

    for (int i = 0; i < 16; i++) {
        uint16_t d = (uint16_t)(dst + i);
        if (d >= 0xA000) break;
        gb->vram[bank][d - 0x8000] = mmu_read(gb, (uint16_t)(src + i));
    }

    src = (uint16_t)(src + 16);
    gb->io[0x51] = (uint8_t)(src >> 8);
    gb->io[0x52] = (uint8_t)(src & 0xF0);
    dst = (uint16_t)(dst + 16);
    gb->io[0x53] = (uint8_t)((dst >> 8) & 0x1F);
    gb->io[0x54] = (uint8_t)(dst & 0xF0);
}

static void hdma_start(struct GameBoy *gb, uint8_t val)
{
    int blocks = (val & 0x7F) + 1;

    if (!(val & 0x80)) {
        for (int i = 0; i < blocks; i++)
            hdma_copy_block(gb);
        gb->hdma_active = 0;
        gb->hdma_blocks = 0;
        gb->io[0x55] = 0xFF;
        return;
    }

    gb->hdma_active = 1;
    gb->hdma_blocks = blocks;
    gb->io[0x55] = (uint8_t)(val & 0x7F);
}

void mmu_hblank_hdma(struct GameBoy *gb)
{
    if (!gb->cgb_mode || !gb->hdma_active) return;
    if (gb->ppu.ly >= 144) return;

    hdma_copy_block(gb);
    gb->hdma_blocks--;
    if (gb->hdma_blocks <= 0) {
        gb->hdma_active = 0;
        gb->hdma_blocks = 0;
        gb->io[0x55] = 0xFF;
    } else {
        gb->io[0x55] = (uint8_t)((gb->hdma_blocks - 1) & 0x7F);
    }
}

static int wram_bank(struct GameBoy *gb)
{
    int bank = gb->svbk & 7;
    return bank == 0 ? 1 : bank;
}

void mmu_init(struct GameBoy *gb)
{
    for (int i = 0; i < 0x80; i++) gb->io[i] = 0;
    gb->ie = 0;
    gb->vbk = 0;
    gb->svbk = 0;
    gb->double_speed = 0;
    gb->key1_armed = 0;
    gb->hdma_active = 0;
    gb->hdma_blocks = 0;
    memset(gb->bg_pal, 0, sizeof(gb->bg_pal));
    memset(gb->ob_pal, 0, sizeof(gb->ob_pal));
    gb->bcps = 0;
    gb->ocps = 0;
}

uint8_t mmu_read(struct GameBoy *gb, uint16_t addr)
{
    if (addr < 0x8000) return cart_read_rom(&gb->cart, addr);
    if (addr < 0xA000) return gb->vram[gb->vbk & 1][addr - 0x8000];
    if (addr < 0xC000) return cart_read_ram(&gb->cart, addr);
    if (addr < 0xD000) return gb->wram[0][addr - 0xC000];
    if (addr < 0xE000) return gb->wram[wram_bank(gb)][addr - 0xD000];
    if (addr < 0xF000) {
        uint16_t a = (uint16_t)(addr - 0x2000);
        return gb->wram[0][a - 0xC000];
    }
    if (addr < 0xFE00) {
        uint16_t a = (uint16_t)(addr - 0x2000);
        return gb->wram[wram_bank(gb)][a - 0xD000];
    }
    if (addr < 0xFEA0) return gb->oam[addr - 0xFE00];
    if (addr < 0xFF00) return 0xFF;

    if (addr < 0xFF80) {
        if (addr == 0xFF00) return joypad_read(gb, addr);
        if (addr == 0xFF0F) return (uint8_t)((gb->io[0x0F] & 0x1F) | 0xE0);
        if (addr >= 0xFF04 && addr <= 0xFF07) return timer_read(gb, addr);
        if (addr >= 0xFF10 && addr <= 0xFF3F) return apu_read(gb, addr);
        if (addr >= 0xFF40 && addr <= 0xFF4B) return ppu_read(gb, addr);
        if (!gb->cgb_mode) {
            if (addr == 0xFF4D || addr == 0xFF4F ||
                (addr >= 0xFF51 && addr <= 0xFF55) ||
                (addr >= 0xFF68 && addr <= 0xFF6B) ||
                addr == 0xFF70)
                return 0xFF;
        } else {
            if (addr == 0xFF4D)
                return (uint8_t)((gb->double_speed ? 0x80 : 0) |
                                 (gb->key1_armed ? 0x01 : 0) | 0x7E);
            if (addr == 0xFF4F)
                return (uint8_t)((gb->vbk & 1) | 0xFE);
            if (addr == 0xFF55) {
                if (gb->hdma_active)
                    return (uint8_t)((gb->hdma_blocks - 1) & 0x7F);
                return gb->io[0x55];
            }
            if (addr >= 0xFF51 && addr <= 0xFF54)
                return 0xFF;
            if (addr == 0xFF68) return gb->bcps;
            if (addr == 0xFF69) return gb->bg_pal[gb->bcps & 0x3F];
            if (addr == 0xFF6A) return gb->ocps;
            if (addr == 0xFF6B) return gb->ob_pal[gb->ocps & 0x3F];
            if (addr == 0xFF70)
                return (uint8_t)((gb->svbk & 7) | 0xF8);
        }
        return gb->io[addr - 0xFF00];
    }
    if (addr < 0xFFFF) return gb->hram[addr - 0xFF80];
    return (uint8_t)((gb->ie & 0x1F) | 0xE0);
}

static void pal_auto_inc(uint8_t *spec)
{
    if (*spec & 0x80)
        *spec = (uint8_t)(0x80 | (((*spec & 0x3F) + 1) & 0x3F));
}

void mmu_write(struct GameBoy *gb, uint16_t addr, uint8_t val)
{
    if (addr < 0x8000) {
        cart_write(&gb->cart, addr, val);
        return;
    }
    if (addr < 0xA000) { gb->vram[gb->vbk & 1][addr - 0x8000] = val; return; }
    if (addr < 0xC000) { cart_write_ram(&gb->cart, addr, val); return; }
    if (addr < 0xD000) { gb->wram[0][addr - 0xC000] = val; return; }
    if (addr < 0xE000) { gb->wram[wram_bank(gb)][addr - 0xD000] = val; return; }
    if (addr < 0xF000) {
        uint16_t a = (uint16_t)(addr - 0x2000);
        gb->wram[0][a - 0xC000] = val;
        return;
    }
    if (addr < 0xFE00) {
        uint16_t a = (uint16_t)(addr - 0x2000);
        gb->wram[wram_bank(gb)][a - 0xD000] = val;
        return;
    }
    if (addr < 0xFEA0) { gb->oam[addr - 0xFE00] = val; return; }
    if (addr < 0xFF00) return;

    if (addr < 0xFF80) {
        if (addr == 0xFF00) { joypad_write(gb, addr, val); return; }
        if (addr == 0xFF01) { gb->io[0x01] = val; return; }
        if (addr == 0xFF0F) { gb->io[0x0F] = (uint8_t)(val & 0x1F); return; }
        if (addr == 0xFF02) {
            gb->io[0x02] = val;
            if (val & 0x80) {
                if (gb->serial_len < SERIAL_CAP - 1)
                    gb->serial[gb->serial_len++] = (char)gb->io[0x01];
                gb->serial[gb->serial_len] = '\0';
                gb->io[0x02] &= 0x7F;
                gb->io[0x0F] |= 0x08;
            }
            return;
        }
        if (addr >= 0xFF04 && addr <= 0xFF07) { timer_write(gb, addr, val); return; }
        if (addr >= 0xFF10 && addr <= 0xFF3F) { apu_write(gb, addr, val); return; }
        if (addr == 0xFF46) {
            gb->io[0x46] = val;
            for (int i = 0; i < 0xA0; i++)
                gb->oam[i] = mmu_read(gb, (uint16_t)((val << 8) + i));
            return;
        }
        if (addr >= 0xFF40 && addr <= 0xFF4B) { ppu_write(gb, addr, val); return; }
        if (gb->cgb_mode) {
            if (addr == 0xFF4D) {
                gb->key1_armed = val & 1;
                return;
            }
            if (addr == 0xFF4F) {
                gb->vbk = val & 1;
                return;
            }
            if (addr >= 0xFF51 && addr <= 0xFF54) {
                gb->io[addr - 0xFF00] = val;
                return;
            }
            if (addr == 0xFF55) {
                if (gb->hdma_active && !(val & 0x80)) {
                    gb->io[0x55] = (uint8_t)(0x80 | ((gb->hdma_blocks - 1) & 0x7F));
                    gb->hdma_active = 0;
                    gb->hdma_blocks = 0;
                    return;
                }
                if (!gb->hdma_active)
                    hdma_start(gb, val);
                return;
            }
            if (addr == 0xFF68) { gb->bcps = val; return; }
            if (addr == 0xFF69) {
                gb->bg_pal[gb->bcps & 0x3F] = val;
                pal_auto_inc(&gb->bcps);
                return;
            }
            if (addr == 0xFF6A) { gb->ocps = val; return; }
            if (addr == 0xFF6B) {
                gb->ob_pal[gb->ocps & 0x3F] = val;
                pal_auto_inc(&gb->ocps);
                return;
            }
            if (addr == 0xFF70) {
                gb->svbk = val & 7;
                return;
            }
        }
        gb->io[addr - 0xFF00] = val;
        return;
    }
    if (addr < 0xFFFF) { gb->hram[addr - 0xFF80] = val; return; }
    gb->ie = (uint8_t)(val & 0x1F);
}
