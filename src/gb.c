#include <string.h>

#include "gb.h"
#include "mmu.h"

static void gb_apply_cgb_boot(GameBoy *gb)
{
    gb->cpu.a = 0x11;
    gb->cpu.f = 0x80;
    gb->cpu.b = 0x00;
    gb->cpu.c = 0x00;
    gb->cpu.d = 0xFF;
    gb->cpu.e = 0x56;
    gb->cpu.h = 0x00;
    gb->cpu.l = 0x0D;

    gb->io[0x02] = 0x7F;
    gb->io[0x40] = 0x91;
    gb->io[0x41] = 0x85;
    gb->io[0x46] = 0x00;
    gb->io[0x4D] = 0x7E;
    gb->io[0x4F] = 0xFE;
    gb->io[0x51] = 0xFF;
    gb->io[0x52] = 0xFF;
    gb->io[0x53] = 0xFF;
    gb->io[0x54] = 0xFF;
    gb->io[0x55] = 0xFF;
    gb->io[0x56] = 0x3E;
    gb->io[0x70] = 0xF8;
    gb->vbk = 0;
    gb->svbk = 0;
    gb->double_speed = 0;
    gb->key1_armed = 0;
}

void gb_init(GameBoy *gb)
{
    memset(gb, 0, sizeof(*gb));
    cpu_init(&gb->cpu);
    ppu_init(&gb->ppu);
    timer_init(&gb->timer);
    joypad_init(&gb->joypad);
    apu_init(&gb->apu);
    mmu_init(gb);

    gb->timer.div_counter = 0xAB00;
    gb->io[0x00] = 0xCF;
    gb->io[0x40] = 0x91;
    gb->io[0x47] = 0xFC;
    gb->io[0x24] = 0x77;
    gb->io[0x25] = 0xF3;
    gb->apu.powered = 1;
}

int gb_load_rom(GameBoy *gb, const char *path)
{
    int rc = cart_load(&gb->cart, path);
    if (rc == 0) {
        gb->serial_len = 0;
        gb->serial[0] = '\0';
        gb->cgb_mode = (gb->cart.cgb_flag & 0x80) ? 1 : 0;
        if (gb->cgb_mode)
            gb_apply_cgb_boot(gb);
    }
    return rc;
}

void gb_step(GameBoy *gb)
{
    int cycles = cpu_step(gb);
    if (cycles <= 0) cycles = 4;
    timer_step(gb, cycles);
    int ppu_cycles = gb->double_speed ? cycles / 2 : cycles;
    if (ppu_cycles < 1) ppu_cycles = 1;
    ppu_step(gb, ppu_cycles);
    apu_step(gb, ppu_cycles);
}

void gb_run_frame(GameBoy *gb)
{
    gb->ppu.frame_ready = 0;
    int guard = 0;
    while (!gb->ppu.frame_ready && guard < 10000000) {
        gb_step(gb);
        guard++;
    }
}
