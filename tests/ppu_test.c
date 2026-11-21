#include <stdio.h>
#include <string.h>

#include "gb.h"

static int failures = 0;

static void expect_px(GameBoy *gb, int x, int y, uint32_t want, const char *what)
{
    uint32_t got = gb->ppu.fb[y * SCREEN_W + x];
    if (got != want) {
        printf("FAIL %-38s (%d,%d) got %08X want %08X\n", what, x, y, got, want);
        failures++;
    } else {
        printf("ok   %s\n", what);
    }
}

int main(void)
{
    GameBoy gb;
    gb_init(&gb);

    gb.io[0x40] = 0xF3;
    gb.io[0x47] = 0xE4;
    gb.io[0x48] = 0x40;
    gb.io[0x4A] = 20;
    gb.io[0x4B] = 47;
    gb.io[0x45] = 100;
    gb.io[0x41] = 0x40;

    memset(&gb.vram[0][16], 0xFF, 16);
    gb.vram[0][0x1800 + 3 * 32 + 1] = 1;
    gb.vram[0][0x1800 + 3 * 32 + 3] = 1;
    memset(&gb.vram[0][0x1C00], 1, 32 * 18);

    gb.oam[0] = 46; gb.oam[1] = 18; gb.oam[2] = 1; gb.oam[3] = 0x00;
    gb.oam[4] = 46; gb.oam[5] = 32; gb.oam[6] = 1; gb.oam[7] = 0x80;

    int guard = 0;
    while (!gb.ppu.frame_ready && guard++ < 300000)
        ppu_step(&gb, 4);

    if (!gb.ppu.frame_ready) {
        printf("FAIL: frame never completed\n");
        return 1;
    }
    printf("ok   frame completed\n");

    expect_px(&gb, 10, 10, 0xFFE0F8D0, "BG background");
    expect_px(&gb, 45, 25, 0xFF081820, "window box");
    expect_px(&gb, 10, 30, 0xFF88C070, "sprite opaque on top");
    expect_px(&gb, 24, 30, 0xFF081820, "sprite behind BG hidden");
    expect_px(&gb, 24, 32, 0xFF88C070, "sprite visible over color 0");
    expect_px(&gb, 10, 38, 0xFFE0F8D0, "below sprite is BG");

    if (!(gb.io[0x0F] & 0x02)) {
        printf("FAIL: LYC/STAT interrupt not requested\n");
        failures++;
    } else {
        printf("ok   LYC/STAT interrupt\n");
    }

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
