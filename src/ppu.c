#include <string.h>

#include "gb.h"
#include "mmu.h"
#include "ppu.h"

static const uint32_t DMG_PAL[4] = {
    0xFFE0F8D0,
    0xFF88C070,
    0xFF346856,
    0xFF081820,
};

static uint32_t cgb_color(const uint8_t *pal, int pal_num, int color)
{
    int off = ((pal_num & 7) * 4 + (color & 3)) * 2;
    uint16_t v = (uint16_t)(pal[off] | (pal[off + 1] << 8));
    uint32_t r = v & 0x1F;
    uint32_t g = (v >> 5) & 0x1F;
    uint32_t b = (v >> 10) & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void ppu_init(PPU *p)
{
    memset(p, 0, sizeof(*p));
    p->mode = 2;
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) p->fb[i] = DMG_PAL[0];
}

static void update_stat(struct GameBoy *gb)
{
    PPU *p = &gb->ppu;
    uint8_t old = gb->io[0x41];
    uint8_t stat = (uint8_t)(old & 0x78);
    stat |= (uint8_t)(p->mode & 3);

    int lyc = (p->ly == gb->io[0x45]);
    if (lyc) stat |= 0x04;

    int fire = 0;
    if (!(old & 0x04) && lyc && (stat & 0x40)) fire = 1;

    int old_mode = old & 3;
    if (old_mode != p->mode) {
        if (p->mode == 0 && (stat & 0x08)) fire = 1;
        if (p->mode == 1 && (stat & 0x10)) fire = 1;
        if (p->mode == 2 && (stat & 0x20)) fire = 1;
    }

    gb->io[0x41] = (uint8_t)(stat | 0x80);
    if (fire) gb->io[0x0F] |= 0x02;
}

static void render_sprites(struct GameBoy *gb, const uint8_t *bg_idx,
                           const uint8_t *bg_prio, int ly)
{
    uint8_t lcdc = gb->io[0x40];
    if (!(lcdc & 0x02)) return;

    int cgb = gb->cgb_mode;
    int height = (lcdc & 0x04) ? 16 : 8;
    uint32_t *line = &gb->ppu.fb[ly * SCREEN_W];
    uint32_t bg_line[SCREEN_W];
    if (cgb)
        memcpy(bg_line, line, sizeof(bg_line));

    int sel[10];
    int n = 0;
    for (int i = 0; i < 40 && n < 10; i++) {
        int sy = gb->oam[i * 4] - 16;
        if (ly >= sy && ly < sy + height) sel[n++] = i;
    }
    if (n == 0) return;

    if (cgb) {
        for (int a = 0; a < n / 2; a++) {
            int t = sel[a];
            sel[a] = sel[n - 1 - a];
            sel[n - 1 - a] = t;
        }
    } else {
        for (int a = 1; a < n; a++) {
            int key = sel[a];
            int kx = gb->oam[key * 4 + 1];
            int b = a - 1;
            while (b >= 0) {
                int ox = gb->oam[sel[b] * 4 + 1];
                if (kx > ox || (kx == ox && key > sel[b])) {
                    sel[b + 1] = sel[b];
                    b--;
                } else {
                    break;
                }
            }
            sel[b + 1] = key;
        }
    }

    for (int s = 0; s < n; s++) {
        int i = sel[s];
        int oy = gb->oam[i * 4];
        int ox = gb->oam[i * 4 + 1];
        int tile = gb->oam[i * 4 + 2];
        int attr = gb->oam[i * 4 + 3];
        int sy = oy - 16;
        int sx = ox - 8;
        int row = ly - sy;
        if (attr & 0x40) row = height - 1 - row;
        int t = tile;
        if (height == 16) t = (tile & 0xFE) | ((row >> 3) & 1);
        int fine = row & 7;
        int base = t * 16 + fine * 2;
        int bank = cgb ? ((attr >> 3) & 1) : 0;
        uint8_t lo = gb->vram[bank][base];
        uint8_t hi = gb->vram[bank][base + 1];
        uint8_t pal = (attr & 0x10) ? gb->io[0x49] : gb->io[0x48];
        int behind = attr & 0x80;

        for (int px = 0; px < 8; px++) {
            int x = sx + px;
            if (x < 0 || x >= SCREEN_W) continue;
            int bit = (attr & 0x20) ? px : 7 - px;
            int color = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
            if (color == 0) continue;

            if (cgb) {
                int bg_wins = 0;
                if (bg_idx[x] != 0 && (lcdc & 0x01)) {
                    if ((attr & 0x80) || (bg_prio[x] & 1))
                        bg_wins = 1;
                }
                if (bg_wins)
                    line[x] = bg_line[x];
                else
                    line[x] = cgb_color(gb->ob_pal, attr & 7, color);
            } else {
                if (behind && bg_idx[x] != 0) continue;
                int shade = (pal >> (color * 2)) & 3;
                line[x] = DMG_PAL[shade];
            }
        }
    }
}

static int tile_addr_of(uint8_t tile, int signed_mode)
{
    if (signed_mode)
        return 0x1000 + (int)((int8_t)tile * 16);
    return (int)tile * 16;
}

static void render_scanline(struct GameBoy *gb)
{
    PPU *p = &gb->ppu;
    int ly = p->ly;
    if (ly >= SCREEN_H) return;

    uint8_t lcdc = gb->io[0x40];
    uint32_t *line = &p->fb[ly * SCREEN_W];
    uint8_t bg_idx[SCREEN_W];
    uint8_t bg_prio[SCREEN_W];
    int cgb = gb->cgb_mode;

    memset(bg_prio, 0, sizeof(bg_prio));

    int bg_off = !(lcdc & 0x01);
    if (bg_off && !cgb) {
        for (int x = 0; x < SCREEN_W; x++) {
            line[x] = DMG_PAL[0];
            bg_idx[x] = 0;
        }
        render_sprites(gb, bg_idx, bg_prio, ly);
        return;
    }

    int scy = gb->io[0x42];
    int scx = gb->io[0x43];
    uint8_t bgp = gb->io[0x47];
    uint16_t map_base = (lcdc & 0x08) ? 0x1C00 : 0x1800;
    int signed_tiles = !(lcdc & 0x10);

    int y = (ly + scy) & 0xFF;
    int map_row = (y / 8) * 32;
    int fine_y = y % 8;

    for (int x = 0; x < SCREEN_W; x++) {
        int xx = (x + scx) & 0xFF;
        int cell = map_base + map_row + (xx / 8);
        uint8_t tile = gb->vram[0][cell];
        int attr = cgb ? gb->vram[1][cell] : 0;
        int fine_x = xx % 8;
        if (attr & 0x20) fine_x = 7 - fine_x;
        int fy = fine_y;
        if (attr & 0x40) fy = 7 - fy;
        int bank = cgb ? ((attr >> 3) & 1) : 0;
        int tile_addr = tile_addr_of(tile, signed_tiles);
        uint8_t lo = gb->vram[bank][tile_addr + fy * 2];
        uint8_t hi = gb->vram[bank][tile_addr + fy * 2 + 1];
        int bit = 7 - fine_x;
        int color = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
        bg_idx[x] = (uint8_t)color;
        bg_prio[x] = (uint8_t)((attr >> 7) & 1);
        if (cgb)
            line[x] = cgb_color(gb->bg_pal, attr & 7, color);
        else {
            int shade = (bgp >> (color * 2)) & 3;
            line[x] = DMG_PAL[shade];
        }
    }

    int win_enable = (lcdc & 0x20) && (cgb || (lcdc & 0x01));
    if (win_enable && gb->io[0x4B] <= 166 && ly >= gb->io[0x4A]) {
        int wx = gb->io[0x4B] - 7;
        int start = wx < 0 ? 0 : wx;
        uint16_t wmap = (lcdc & 0x40) ? 0x1C00 : 0x1800;
        int wline = p->win_line;
        int wmap_row = (wline / 8) * 32;
        int wfine = wline % 8;

        for (int x = start; x < SCREEN_W; x++) {
            int wx_in = x - wx;
            int cell = wmap + wmap_row + (wx_in / 8);
            uint8_t tile = gb->vram[0][cell];
            int attr = cgb ? gb->vram[1][cell] : 0;
            int fine_x = wx_in % 8;
            if (attr & 0x20) fine_x = 7 - fine_x;
            int fy = wfine;
            if (attr & 0x40) fy = 7 - fy;
            int bank = cgb ? ((attr >> 3) & 1) : 0;
            int tile_addr = tile_addr_of(tile, signed_tiles);
            uint8_t lo = gb->vram[bank][tile_addr + fy * 2];
            uint8_t hi = gb->vram[bank][tile_addr + fy * 2 + 1];
            int bit = 7 - fine_x;
            int color = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
            bg_idx[x] = (uint8_t)color;
            bg_prio[x] = (uint8_t)((attr >> 7) & 1);
            if (cgb)
                line[x] = cgb_color(gb->bg_pal, attr & 7, color);
            else {
                int shade = (bgp >> (color * 2)) & 3;
                line[x] = DMG_PAL[shade];
            }
        }
        p->win_line++;
    }

    render_sprites(gb, bg_idx, bg_prio, ly);
}

static void next_line(struct GameBoy *gb)
{
    PPU *p = &gb->ppu;
    p->ly++;
    gb->io[0x44] = p->ly;

    if (p->ly == SCREEN_H) {
        p->mode = 1;
        p->frame_ready = 1;
        gb->io[0x0F] |= 0x01;
    } else {
        p->mode = 2;
    }
}

void ppu_step(struct GameBoy *gb, int cycles)
{
    PPU *p = &gb->ppu;
    uint8_t lcdc = gb->io[0x40];

    if (!(lcdc & 0x80)) {
        if (p->mode != 0 || p->ly != 0) {
            p->mode = 0;
            p->ly = 0;
            p->win_line = 0;
            gb->io[0x44] = 0;
            for (int i = 0; i < SCREEN_W * SCREEN_H; i++) p->fb[i] = DMG_PAL[0];
        }
        p->lcd_off_clock += cycles;
        if (p->lcd_off_clock >= 70224) {
            p->lcd_off_clock -= 70224;
            p->frame_ready = 1;
        }
        uint8_t stat = (uint8_t)((gb->io[0x41] & 0x78) | 0x80);
        if (gb->io[0x44] == gb->io[0x45]) stat |= 0x04;
        gb->io[0x41] = stat;
        return;
    }

    p->lcd_off_clock = 0;
    p->mode_clock += cycles;

    switch (p->mode) {
    case 2:
        if (p->mode_clock >= 80) {
            p->mode_clock -= 80;
            p->mode = 3;
        }
        break;
    case 3:
        if (p->mode_clock >= 172) {
            p->mode_clock -= 172;
            p->mode = 0;
            render_scanline(gb);
            mmu_hblank_hdma(gb);
        }
        break;
    case 0:
        if (p->mode_clock >= 204) {
            p->mode_clock -= 204;
            next_line(gb);
        }
        break;
    case 1:
        if (p->mode_clock >= 456) {
            p->mode_clock -= 456;
            if (p->ly >= 153) {
                p->ly = 0;
                p->win_line = 0;
                gb->io[0x44] = 0;
                p->mode = 2;
            } else {
                p->ly++;
                gb->io[0x44] = p->ly;
            }
        }
        break;
    default:
        p->mode = 2;
        break;
    }

    update_stat(gb);
}

uint8_t ppu_read(struct GameBoy *gb, uint16_t addr)
{
    uint8_t reg = (uint8_t)(addr - 0xFF00);
    if (reg == 0x44) return gb->ppu.ly;
    if (reg == 0x41) return gb->io[0x41];
    return gb->io[reg];
}

void ppu_write(struct GameBoy *gb, uint16_t addr, uint8_t val)
{
    uint8_t reg = (uint8_t)(addr - 0xFF00);
    if (reg == 0x44) return;
    if (reg == 0x41) {
        gb->io[0x41] = (uint8_t)((val & 0x78) | (gb->io[0x41] & 0x07));
        return;
    }
    gb->io[reg] = val;
}
