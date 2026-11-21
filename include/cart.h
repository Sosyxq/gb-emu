#ifndef CART_H
#define CART_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    uint8_t *rom;
    size_t rom_size;
    uint8_t ram[0x8000];
    size_t ram_size;
    char title[17];
    char save_path[512];
    char rtc_path[512];
    uint8_t cart_type;
    uint8_t cgb_flag;
    int mbc;
    int battery;
    int rom_banks;
    int rom_bank;
    int ram_bank;
    int ram_enabled;
    int banking_mode;
    uint8_t rtc_regs[5];
    uint8_t rtc_latched[5];
    time_t rtc_updated;
    int rtc_sel;
    int rtc_latch;
    unsigned ram_wr_ok;
    unsigned ram_wr_blocked;
    unsigned ram_rd_ok;
    unsigned ram_rd_blocked;
} Cart;

int cart_load(Cart *c, const char *path);
void cart_free(Cart *c);
void cart_save(const Cart *c);
void cart_print_header(const Cart *c);
void cart_write(Cart *c, uint16_t addr, uint8_t val);
uint8_t cart_read_rom(Cart *c, uint16_t addr);
uint8_t cart_read_ram(Cart *c, uint16_t addr);
void cart_write_ram(Cart *c, uint16_t addr, uint8_t val);

#endif
