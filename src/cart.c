#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cart.h"

static int cart_has_battery(uint8_t type)
{
    switch (type) {
    case 0x03: /* MBC1+RAM+BATTERY */
    case 0x06: /* MBC2+BATTERY */
    case 0x09: /* ROM+RAM+BATTERY */
    case 0x0D: /* MMM01+RAM+BATTERY */
    case 0x0F: /* MBC3+TIMER+BATTERY */
    case 0x10: /* MBC3+TIMER+RAM+BATTERY */
    case 0x13: /* MBC3+RAM+BATTERY */
    case 0x1B: /* MBC5+RAM+BATTERY */
    case 0x1E: /* MBC5+RUMBLE+RAM+BATTERY */
    case 0xFF: /* HuC1+RAM+BATTERY */
        return 1;
    default:
        return 0;
    }
}

static int cart_has_rtc(uint8_t type)
{
    return type == 0x0F || type == 0x10;
}

static void cart_build_save_path(Cart *c, const char *rom_path)
{
    snprintf(c->save_path, sizeof(c->save_path), "%s", rom_path);
    char *dot = strrchr(c->save_path, '.');
    char *slash = strrchr(c->save_path, '/');
#ifdef _WIN32
    char *bslash = strrchr(c->save_path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    if (dot && (!slash || dot > slash))
        snprintf(dot, sizeof(c->save_path) - (size_t)(dot - c->save_path), ".sav");
    else
        snprintf(c->save_path + strlen(c->save_path),
                 sizeof(c->save_path) - strlen(c->save_path), ".sav");
}

static void cart_build_rtc_path(Cart *c, const char *rom_path)
{
    snprintf(c->rtc_path, sizeof(c->rtc_path), "%s", rom_path);
    char *dot = strrchr(c->rtc_path, '.');
    char *slash = strrchr(c->rtc_path, '/');
    if (dot && (!slash || dot > slash))
        snprintf(dot, sizeof(c->rtc_path) - (size_t)(dot - c->rtc_path), ".rtc");
    else
        snprintf(c->rtc_path + strlen(c->rtc_path),
                 sizeof(c->rtc_path) - strlen(c->rtc_path), ".rtc");
}

static void rtc_tick(Cart *c)
{
    if (!c->battery || !cart_has_rtc(c->cart_type)) return;
    if (c->rtc_regs[4] & 0x40) {
        c->rtc_updated = time(NULL);
        return;
    }
    if (c->rtc_updated == 0) {
        c->rtc_updated = time(NULL);
        return;
    }
    time_t now = time(NULL);
    long long d = (long long)(now - c->rtc_updated);
    if (d <= 0) return;
    c->rtc_updated = now;

    long long s = c->rtc_regs[0] + d;
    c->rtc_regs[0] = (uint8_t)(s % 60);
    long long m = c->rtc_regs[1] + s / 60;
    c->rtc_regs[1] = (uint8_t)(m % 60);
    long long h = c->rtc_regs[2] + m / 60;
    c->rtc_regs[2] = (uint8_t)(h % 24);
    long long day = (c->rtc_regs[3] | ((c->rtc_regs[4] & 1) << 8)) + h / 24;
    if (day >= 512) {
        day &= 511;
        c->rtc_regs[4] |= 0x80;
    }
    c->rtc_regs[3] = (uint8_t)(day & 0xFF);
    c->rtc_regs[4] = (uint8_t)((c->rtc_regs[4] & 0xFE) | ((day >> 8) & 1));
}

static void rtc_latch_now(Cart *c)
{
    rtc_tick(c);
    memcpy(c->rtc_latched, c->rtc_regs, 5);
}

static void rtc_write_reg(Cart *c, int reg, uint8_t val)
{
    switch (reg) {
    case 0: c->rtc_regs[0] = (uint8_t)(val % 60); break;
    case 1: c->rtc_regs[1] = (uint8_t)(val % 60); break;
    case 2: c->rtc_regs[2] = (uint8_t)(val % 24); break;
    case 3: c->rtc_regs[3] = val; break;
    case 4: {
        uint8_t keep = (uint8_t)(c->rtc_regs[4] & 0x3E);
        c->rtc_regs[4] = (uint8_t)((val & 0xC1) | keep | ((c->rtc_regs[4] & 1) & val));
        break;
    }
    default: break;
    }
    c->rtc_updated = time(NULL);
}

static void rtc_save(const Cart *c)
{
    if (!c->battery || !cart_has_rtc(c->cart_type) || !c->rtc_path[0]) return;
    Cart *mut = (Cart *)c;
    rtc_tick(mut);
    FILE *f = fopen(c->rtc_path, "wb");
    if (!f) return;
    fwrite(c->rtc_regs, 1, 5, f);
    int64_t t = (int64_t)c->rtc_updated;
    fwrite(&t, 1, sizeof(t), f);
    fwrite(c->rtc_latched, 1, 5, f);
    fclose(f);
}

static void rtc_load(Cart *c)
{
    if (!c->battery || !cart_has_rtc(c->cart_type) || !c->rtc_path[0]) return;
    FILE *f = fopen(c->rtc_path, "rb");
    if (!f) {
        c->rtc_updated = time(NULL);
        return;
    }
    if (fread(c->rtc_regs, 1, 5, f) != 5) {
        fclose(f);
        c->rtc_updated = time(NULL);
        return;
    }
    int64_t t = 0;
    if (fread(&t, 1, sizeof(t), f) != sizeof(t)) t = 0;
    fread(c->rtc_latched, 1, 5, f);
    fclose(f);
    c->rtc_updated = (time_t)t;
    rtc_tick(c);
    memcpy(c->rtc_latched, c->rtc_regs, 5);
}

static void cart_load_save(Cart *c)
{
    if (!c->battery || c->ram_size == 0 || !c->save_path[0]) return;
    FILE *f = fopen(c->save_path, "rb");
    if (!f) return;
    size_t n = fread(c->ram, 1, c->ram_size, f);
    fclose(f);
    if (n > 0)
        printf("Save loaded: %s (%zu bytes)\n", c->save_path, n);
}

void cart_save(const Cart *c)
{
    if (!c->battery) return;
    rtc_save(c);
    if (c->ram_size == 0 || !c->save_path[0]) return;
    FILE *f = fopen(c->save_path, "wb");
    if (!f) {
        fprintf(stderr, "warning: cannot write save %s\n", c->save_path);
        return;
    }
    size_t n = fwrite(c->ram, 1, c->ram_size, f);
    fclose(f);
    if (n == c->ram_size)
        printf("Save written: %s (%zu bytes)\n", c->save_path, n);
    else
        fprintf(stderr, "warning: short write to %s\n", c->save_path);
}

int cart_load(Cart *c, const char *path)
{
    memset(c, 0, sizeof(*c));
    c->rtc_sel = -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0x4000) {
        fclose(f);
        return -2;
    }

    c->rom = malloc((size_t)sz);
    if (!c->rom) {
        fclose(f);
        return -1;
    }
    if (fread(c->rom, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(c->rom);
        c->rom = NULL;
        return -1;
    }
    fclose(f);
    c->rom_size = (size_t)sz;

    c->cgb_flag = c->rom[0x143];
    memcpy(c->title, c->rom + 0x134, 16);
    c->title[16] = '\0';
    for (int i = 0; i < 16; i++) {
        char ch = c->title[i];
        if (ch == '\0') break;
        if (ch < 32 || ch > 126) {
            c->title[i] = '\0';
            break;
        }
    }

    c->cart_type = c->rom[0x147];
    switch (c->cart_type) {
    case 0x01: case 0x02: case 0x03:
        c->mbc = 1;
        break;
    case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13:
        c->mbc = 3;
        break;
    case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
        c->mbc = 5;
        break;
    default:
        c->mbc = 0;
        break;
    }

    c->rom_banks = (int)(c->rom_size / 0x4000);
    if (c->rom_banks < 1) c->rom_banks = 1;
    c->rom_bank = 1;
    c->ram_bank = 0;
    c->banking_mode = 0;
    c->ram_enabled = 0;
    c->ram_wr_ok = c->ram_wr_blocked = 0;
    c->ram_rd_ok = c->ram_rd_blocked = 0;

    switch (c->rom[0x149]) {
    case 1: c->ram_size = 0x800; break;
    case 2: c->ram_size = 0x2000; break;
    case 3: c->ram_size = 0x8000; break;
    case 5: c->ram_size = 0x1000; break;
    default: c->ram_size = 0; break;
    }

    c->battery = cart_has_battery(c->cart_type);
    if (c->mbc == 0 && c->ram_size > 0)
        c->ram_enabled = 1;

        if (c->battery && c->ram_size > 0) {
        cart_build_save_path(c, path);
        cart_load_save(c);
        c->ram_wr_ok = c->ram_wr_blocked = 0;
        c->ram_rd_ok = c->ram_rd_blocked = 0;
    }
    if (c->battery && cart_has_rtc(c->cart_type)) {
        cart_build_rtc_path(c, path);
        rtc_load(c);
    }

    return 0;
}

void cart_free(Cart *c)
{
    if (c->battery && c->ram_size > 0) {
        int nz = 0;
        for (size_t i = 0; i < c->ram_size; i++)
            if (c->ram[i]) nz++;
        printf("Cart RAM: nz=%d wr_ok=%u wr_blocked=%u rd_ok=%u rd_blocked=%u en=%d bank=%d\n",
               nz, c->ram_wr_ok, c->ram_wr_blocked, c->ram_rd_ok,
               c->ram_rd_blocked, c->ram_enabled, c->ram_bank);
    }
    cart_save(c);
    free(c->rom);
    c->rom = NULL;
    c->rom_size = 0;
}

void cart_print_header(const Cart *c)
{
    static const char *mbc_names[] = {"none", "MBC1", "?", "MBC3", "?", "MBC5"};
    const char *mbc = (c->mbc >= 0 && c->mbc <= 5) ? mbc_names[c->mbc] : "?";

    printf("Title:     %s\n", c->title[0] ? c->title : "(no title)");
    printf("Cart type: $%02X\n", c->cart_type);
    printf("MBC:       %s\n", mbc);
    if (c->cgb_flag & 0x80)
        printf("CGB:       %s\n", (c->cgb_flag & 0x40) ? "only" : "enhanced");
    else
        printf("CGB:       no (DMG)\n");
    if (cart_has_rtc(c->cart_type))
        printf("RTC:       yes\n");
    printf("ROM:       %d banks (%zu KiB)\n", c->rom_banks, c->rom_size / 1024);
    printf("RAM:       %zu bytes%s\n", c->ram_size, c->ram_enabled ? " (enabled)" : "");
    printf("Battery:   %s%s%s\n", c->battery ? "yes" : "no",
           c->battery && c->ram_size ? " -> " : "",
           c->battery && c->ram_size ? c->save_path : "");
}

static void mbc_write(Cart *c, uint16_t addr, uint8_t val)
{
    if (addr < 0x2000) {
        c->ram_enabled = ((val & 0x0F) == 0x0A);
        return;
    }
    if (addr < 0x4000) {
        switch (c->mbc) {
        case 1: {
            uint8_t low = val & 0x1F;
            if (low == 0) low = 1;
            c->rom_bank = (c->rom_bank & ~0x1F) | low;
            break;
        }
        case 3: {
            uint8_t bank = val & 0x7F;
            if (bank == 0) bank = 1;
            c->rom_bank = bank;
            break;
        }
        case 5:
            c->rom_bank = (c->rom_bank & 0x100) | val;
            break;
        default:
            break;
        }
        return;
    }
    if (addr < 0x6000) {
        switch (c->mbc) {
        case 1:
            c->ram_bank = val & 0x03;
            c->rom_bank = (c->rom_bank & 0x1F) | ((val & 0x03) << 5);
            break;
        case 3:
            if (val <= 7) {
                c->ram_bank = val & 0x07;
                c->rtc_sel = -1;
            } else if (val <= 0x0C) {
                c->rtc_sel = val - 8;
            }
            break;
        case 5:
            c->ram_bank = val & 0x0F;
            break;
        default:
            break;
        }
        return;
    }
    if (c->mbc == 1) {
        c->banking_mode = val & 0x01;
        return;
    }
    if (c->mbc == 3) {
        if (c->rtc_latch == 0 && val == 1)
            rtc_latch_now(c);
        c->rtc_latch = val;
    }
}

void cart_write(Cart *c, uint16_t addr, uint8_t val)
{
    if (c->mbc != 0) mbc_write(c, addr, val);
}

uint8_t cart_read_rom(Cart *c, uint16_t addr)
{
    if (!c->rom || c->rom_size == 0) return 0xFF;

    if (addr < 0x4000) {
        if (c->mbc == 1 && c->banking_mode == 1) {
            int bank = ((c->ram_bank & 3) << 5) % c->rom_banks;
            return c->rom[(size_t)bank * 0x4000 + addr];
        }
        return c->rom[addr];
    }

    int bank = c->rom_bank % c->rom_banks;
    if (bank < 0) bank = 0;
    return c->rom[(size_t)bank * 0x4000 + (addr - 0x4000)];
}

static size_t ram_offset(Cart *c, uint16_t addr)
{
    int bank = c->ram_bank;
    if (c->mbc == 1 && c->banking_mode == 0) bank = 0;
    if (c->mbc == 3) bank &= 0x03;
    size_t off = (size_t)(bank & 0x3) * 0x2000 + (addr - 0xA000);
    return off;
}

uint8_t cart_read_ram(Cart *c, uint16_t addr)
{
    if (!c->ram_enabled) {
        c->ram_rd_blocked++;
        return 0xFF;
    }
    if (c->mbc == 3 && c->rtc_sel >= 0)
        return c->rtc_latched[c->rtc_sel];
    if (c->ram_size == 0) return 0xFF;
    size_t off = ram_offset(c, addr);
    if (off >= c->ram_size) {
        c->ram_rd_blocked++;
        return 0xFF;
    }
    c->ram_rd_ok++;
    return c->ram[off];
}

void cart_write_ram(Cart *c, uint16_t addr, uint8_t val)
{
    if (!c->ram_enabled) {
        c->ram_wr_blocked++;
        return;
    }
    if (c->mbc == 3 && c->rtc_sel >= 0) {
        rtc_write_reg(c, c->rtc_sel, val);
        return;
    }
    if (c->ram_size == 0) return;
    size_t off = ram_offset(c, addr);
    if (off < c->ram_size) {
        c->ram[off] = val;
        c->ram_wr_ok++;
    } else {
        c->ram_wr_blocked++;
    }
}
