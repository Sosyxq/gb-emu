#include <stdio.h>

#include "cpu.h"
#include "gb.h"
#include "mmu.h"

static inline uint8_t rb(struct GameBoy *gb, uint16_t a) { return mmu_read(gb, a); }
static inline void wb(struct GameBoy *gb, uint16_t a, uint8_t v) { mmu_write(gb, a, v); }

static inline uint16_t get_hl(CPU *c) { return ((uint16_t)c->h << 8) | c->l; }
static inline void set_hl(CPU *c, uint16_t v)
{
    c->h = (uint8_t)(v >> 8);
    c->l = (uint8_t)(v & 0xFF);
}

static inline uint16_t fetch16(struct GameBoy *gb)
{
    uint8_t lo = rb(gb, gb->cpu.pc++);
    uint8_t hi = rb(gb, gb->cpu.pc++);
    return ((uint16_t)hi << 8) | lo;
}

static inline int get_flag(CPU *c, uint8_t flag) { return (c->f & flag) != 0; }

static uint8_t get_r(struct GameBoy *gb, int i)
{
    CPU *c = &gb->cpu;
    switch (i) {
    case 0: return c->b;
    case 1: return c->c;
    case 2: return c->d;
    case 3: return c->e;
    case 4: return c->h;
    case 5: return c->l;
    case 6: return rb(gb, get_hl(c));
    default: return c->a;
    }
}

static void set_r(struct GameBoy *gb, int i, uint8_t v)
{
    CPU *c = &gb->cpu;
    switch (i) {
    case 0: c->b = v; break;
    case 1: c->c = v; break;
    case 2: c->d = v; break;
    case 3: c->e = v; break;
    case 4: c->h = v; break;
    case 5: c->l = v; break;
    case 6: wb(gb, get_hl(c), v); break;
    default: c->a = v; break;
    }
}

static uint16_t get_rr(CPU *c, int i)
{
    switch (i) {
    case 0: return ((uint16_t)c->b << 8) | c->c;
    case 1: return ((uint16_t)c->d << 8) | c->e;
    case 2: return get_hl(c);
    default: return c->sp;
    }
}

static void set_rr(CPU *c, int i, uint16_t v)
{
    switch (i) {
    case 0: c->b = (uint8_t)(v >> 8); c->c = (uint8_t)v; break;
    case 1: c->d = (uint8_t)(v >> 8); c->e = (uint8_t)v; break;
    case 2: set_hl(c, v); break;
    default: c->sp = v; break;
    }
}

static void alu_add(CPU *c, uint8_t v, int carry)
{
    int half = (c->a & 0xF) + (v & 0xF) + carry;
    int total = c->a + v + carry;
    uint8_t f = 0;
    if ((total & 0xFF) == 0) f |= FLAG_Z;
    if (half > 0xF) f |= FLAG_H;
    if (total > 0xFF) f |= FLAG_C;
    c->f = f;
    c->a = (uint8_t)total;
}

static void alu_sub(CPU *c, uint8_t v, int carry)
{
    int half = (c->a & 0xF) - (v & 0xF) - carry;
    int total = c->a - v - carry;
    uint8_t f = FLAG_N;
    if ((total & 0xFF) == 0) f |= FLAG_Z;
    if (half < 0) f |= FLAG_H;
    if (total < 0) f |= FLAG_C;
    c->f = f;
    c->a = (uint8_t)total;
}

static void alu_cmp(CPU *c, uint8_t v)
{
    int half = (c->a & 0xF) - (v & 0xF);
    int total = c->a - v;
    uint8_t f = FLAG_N;
    if ((total & 0xFF) == 0) f |= FLAG_Z;
    if (half < 0) f |= FLAG_H;
    if (total < 0) f |= FLAG_C;
    c->f = f;
}

static uint8_t inc8(CPU *c, uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    c->f &= FLAG_C;
    if (r == 0) c->f |= FLAG_Z;
    if ((v & 0xF) == 0xF) c->f |= FLAG_H;
    return r;
}

static uint8_t dec8(CPU *c, uint8_t v)
{
    uint8_t r = (uint8_t)(v - 1);
    c->f = (c->f & FLAG_C) | FLAG_N;
    if (r == 0) c->f |= FLAG_Z;
    if ((v & 0xF) == 0) c->f |= FLAG_H;
    return r;
}

static void add_hl(CPU *c, uint16_t v)
{
    uint16_t hl = get_hl(c);
    uint32_t r = (uint32_t)hl + v;
    c->f &= FLAG_Z;
    if (((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF) c->f |= FLAG_H;
    if (r > 0xFFFF) c->f |= FLAG_C;
    set_hl(c, (uint16_t)r);
}

static void daa(CPU *c)
{
    if (!get_flag(c, FLAG_N)) {
        if (get_flag(c, FLAG_C) || c->a > 0x99) {
            c->a = (uint8_t)(c->a + 0x60);
            c->f |= FLAG_C;
        }
        if (get_flag(c, FLAG_H) || (c->a & 0xF) > 0x09)
            c->a = (uint8_t)(c->a + 0x06);
    } else {
        if (get_flag(c, FLAG_C)) c->a = (uint8_t)(c->a - 0x60);
        if (get_flag(c, FLAG_H)) c->a = (uint8_t)(c->a - 0x06);
    }
    c->f &= ~(FLAG_Z | FLAG_H);
    if (c->a == 0) c->f |= FLAG_Z;
}

static int cond(CPU *c, int cc)
{
    switch (cc) {
    case 0: return !get_flag(c, FLAG_Z);
    case 1: return get_flag(c, FLAG_Z);
    case 2: return !get_flag(c, FLAG_C);
    default: return get_flag(c, FLAG_C);
    }
}

static void push(struct GameBoy *gb, uint16_t v)
{
    gb->cpu.sp -= 2;
    wb(gb, gb->cpu.sp, (uint8_t)(v & 0xFF));
    wb(gb, (uint16_t)(gb->cpu.sp + 1), (uint8_t)(v >> 8));
}

static uint16_t pop(struct GameBoy *gb)
{
    uint16_t v = rb(gb, gb->cpu.sp);
    v |= (uint16_t)rb(gb, (uint16_t)(gb->cpu.sp + 1)) << 8;
    gb->cpu.sp += 2;
    return v;
}

static int handle_interrupts(struct GameBoy *gb)
{
    CPU *c = &gb->cpu;
    uint8_t pending = (uint8_t)(gb->ie & gb->io[0x0F] & 0x1F);
    if (!pending) return 0;

    if (c->halted) c->halted = 0;
    if (!c->ime) return 0;

    static const uint16_t vectors[5] = {0x40, 0x48, 0x50, 0x58, 0x60};
    for (int i = 0; i < 5; i++) {
        if (pending & (1 << i)) {
            c->ime = 0;
            gb->io[0x0F] &= (uint8_t)~(1 << i);
            push(gb, c->pc);
            c->pc = vectors[i];
            return 20;
        }
    }
    return 0;
}

static int exec_cb(struct GameBoy *gb)
{
    CPU *c = &gb->cpu;
    uint8_t op = rb(gb, c->pc++);
    int reg = op & 7;
    int is_hl = (reg == 6);
    uint8_t v = get_r(gb, reg);
    int bit = (op >> 3) & 7;
    int old_c = get_flag(c, FLAG_C);

    switch (op >> 6) {
    case 0: {
        int kind = bit;
        uint8_t result = 0;
        int carry_out = 0;
        switch (kind) {
        case 0: carry_out = v >> 7; result = (uint8_t)((v << 1) | carry_out); break;
        case 1: carry_out = v & 1; result = (uint8_t)((v >> 1) | (carry_out << 7)); break;
        case 2: carry_out = v >> 7; result = (uint8_t)((v << 1) | old_c); break;
        case 3: carry_out = v & 1; result = (uint8_t)((v >> 1) | (old_c << 7)); break;
        case 4: carry_out = v >> 7; result = (uint8_t)(v << 1); break;
        case 5: carry_out = v & 1; result = (uint8_t)((v >> 1) | (v & 0x80)); break;
        case 6: result = (uint8_t)((v << 4) | (v >> 4)); break;
        case 7: carry_out = v & 1; result = (uint8_t)(v >> 1); break;
        }
        c->f = carry_out ? FLAG_C : 0;
        if (result == 0) c->f |= FLAG_Z;
        set_r(gb, reg, result);
        break;
    }
    case 1: {
        uint8_t f = (uint8_t)((c->f & FLAG_C) | FLAG_H);
        if (!(v & (1 << bit))) f |= FLAG_Z;
        c->f = f;
        break;
    }
    case 2:
        set_r(gb, reg, (uint8_t)(v & ~(1 << bit)));
        break;
    default:
        set_r(gb, reg, (uint8_t)(v | (1 << bit)));
        break;
    }

    if (is_hl) return (op >> 6) == 1 ? 12 : 16;
    return 8;
}

void cpu_init(CPU *c)
{
    c->a = 0x01;
    c->f = 0xB0;
    c->b = 0x00;
    c->c = 0x13;
    c->d = 0x00;
    c->e = 0xD8;
    c->h = 0x01;
    c->l = 0x4D;
    c->sp = 0xFFFE;
    c->pc = 0x0100;
    c->halted = 0;
    c->ime = 0;
    c->ei_delay = 0;
    c->halt_bug = 0;
}

static int do_step(struct GameBoy *gb)
{
    CPU *c = &gb->cpu;

    int cycles = handle_interrupts(gb);
    if (cycles) return cycles;

    if (c->halted) return 4;

    uint16_t pc0 = c->pc;
    uint8_t op = rb(gb, c->pc);
    if (c->halt_bug)
        c->halt_bug = 0;
    else
        c->pc++;

    if (op >= 0x40 && op <= 0x7F) {
        if (op == 0x76) {
            if (!c->ime && ((gb->ie & gb->io[0x0F]) & 0x1F))
                c->halt_bug = 1;
            else
                c->halted = 1;
            return 4;
        }
        int dst = (op >> 3) & 7;
        int src = op & 7;
        set_r(gb, dst, get_r(gb, src));
        return (dst == 6 || src == 6) ? 8 : 4;
    }

    if (op >= 0x80 && op <= 0xBF) {
        int src = op & 7;
        uint8_t v = get_r(gb, src);
        switch ((op >> 3) & 7) {
        case 0: alu_add(c, v, 0); break;
        case 1: alu_add(c, v, get_flag(c, FLAG_C)); break;
        case 2: alu_sub(c, v, 0); break;
        case 3: alu_sub(c, v, get_flag(c, FLAG_C)); break;
        case 4: c->f = FLAG_H; c->a &= v; if (c->a == 0) c->f |= FLAG_Z; break;
        case 5: c->f = 0; c->a ^= v; if (c->a == 0) c->f |= FLAG_Z; break;
        case 6: c->f = 0; c->a |= v; if (c->a == 0) c->f |= FLAG_Z; break;
        case 7: alu_cmp(c, v); break;
        }
        return src == 6 ? 8 : 4;
    }

    if ((op & 0xC7) == 0x04) {
        int r = (op >> 3) & 7;
        set_r(gb, r, inc8(c, get_r(gb, r)));
        return r == 6 ? 12 : 4;
    }
    if ((op & 0xC7) == 0x05) {
        int r = (op >> 3) & 7;
        set_r(gb, r, dec8(c, get_r(gb, r)));
        return r == 6 ? 12 : 4;
    }
    if ((op & 0xC7) == 0x06) {
        int r = (op >> 3) & 7;
        uint8_t n = rb(gb, c->pc++);
        set_r(gb, r, n);
        return r == 6 ? 12 : 8;
    }
    if ((op & 0xCF) == 0x01) {
        int rr = (op >> 4) & 3;
        set_rr(c, rr, fetch16(gb));
        return 12;
    }
    if ((op & 0xCF) == 0x03) {
        int rr = (op >> 4) & 3;
        set_rr(c, rr, (uint16_t)(get_rr(c, rr) + 1));
        return 8;
    }
    if ((op & 0xCF) == 0x0B) {
        int rr = (op >> 4) & 3;
        set_rr(c, rr, (uint16_t)(get_rr(c, rr) - 1));
        return 8;
    }
    if ((op & 0xCF) == 0x09) {
        int rr = (op >> 4) & 3;
        add_hl(c, get_rr(c, rr));
        return 8;
    }

    switch (op) {
    case 0x00: return 4;
    case 0x02: wb(gb, ((uint16_t)c->b << 8) | c->c, c->a); return 8;
    case 0x08: {
        uint16_t nn = fetch16(gb);
        wb(gb, nn, (uint8_t)(c->sp & 0xFF));
        wb(gb, (uint16_t)(nn + 1), (uint8_t)(c->sp >> 8));
        return 20;
    }
    case 0x07: {
        int cy = c->a >> 7;
        c->a = (uint8_t)((c->a << 1) | cy);
        c->f = cy ? FLAG_C : 0;
        return 4;
    }
    case 0x0A: c->a = rb(gb, ((uint16_t)c->b << 8) | c->c); return 8;
    case 0x0F: {
        int cy = c->a & 1;
        c->a = (uint8_t)((c->a >> 1) | (cy << 7));
        c->f = cy ? FLAG_C : 0;
        return 4;
    }
    case 0x10: {
        c->pc++;
        if (gb->cgb_mode && gb->key1_armed) {
            gb->key1_armed = 0;
            gb->double_speed ^= 1;
        }
        return 8;
    }
    case 0x12: wb(gb, ((uint16_t)c->d << 8) | c->e, c->a); return 8;
    case 0x17: {
        int hi = c->a >> 7;
        int old = get_flag(c, FLAG_C);
        c->a = (uint8_t)((c->a << 1) | old);
        c->f = hi ? FLAG_C : 0;
        return 4;
    }
    case 0x18: {
        int8_t e = (int8_t)rb(gb, c->pc++);
        c->pc = (uint16_t)(c->pc + e);
        return 12;
    }
    case 0x1A: c->a = rb(gb, ((uint16_t)c->d << 8) | c->e); return 8;
    case 0x1F: {
        int lo = c->a & 1;
        int old = get_flag(c, FLAG_C);
        c->a = (uint8_t)((c->a >> 1) | (old << 7));
        c->f = lo ? FLAG_C : 0;
        return 4;
    }
    case 0x20: {
        int8_t e = (int8_t)rb(gb, c->pc++);
        if (!get_flag(c, FLAG_Z)) { c->pc = (uint16_t)(c->pc + e); return 12; }
        return 8;
    }
    case 0x22: wb(gb, get_hl(c), c->a); set_hl(c, (uint16_t)(get_hl(c) + 1)); return 8;
    case 0x27: daa(c); return 4;
    case 0x28: {
        int8_t e = (int8_t)rb(gb, c->pc++);
        if (get_flag(c, FLAG_Z)) { c->pc = (uint16_t)(c->pc + e); return 12; }
        return 8;
    }
    case 0x2A: c->a = rb(gb, get_hl(c)); set_hl(c, (uint16_t)(get_hl(c) + 1)); return 8;
    case 0x2F: c->a = (uint8_t)~c->a; c->f |= FLAG_N | FLAG_H; return 4;
    case 0x30: {
        int8_t e = (int8_t)rb(gb, c->pc++);
        if (!get_flag(c, FLAG_C)) { c->pc = (uint16_t)(c->pc + e); return 12; }
        return 8;
    }
    case 0x32: wb(gb, get_hl(c), c->a); set_hl(c, (uint16_t)(get_hl(c) - 1)); return 8;
    case 0x37: c->f = (uint8_t)((c->f & FLAG_Z) | FLAG_C); return 4;
    case 0x38: {
        int8_t e = (int8_t)rb(gb, c->pc++);
        if (get_flag(c, FLAG_C)) { c->pc = (uint16_t)(c->pc + e); return 12; }
        return 8;
    }
    case 0x3A: c->a = rb(gb, get_hl(c)); set_hl(c, (uint16_t)(get_hl(c) - 1)); return 8;
    case 0x3F: {
        uint8_t f = (uint8_t)(c->f & FLAG_Z);
        if (!(c->f & FLAG_C)) f |= FLAG_C;
        c->f = f;
        return 4;
    }

    case 0xC0: if (!cond(c, 0)) return 8; c->pc = pop(gb); return 20;
    case 0xC1: set_rr(c, 0, pop(gb)); return 12;
    case 0xC2: {
        uint16_t nn = fetch16(gb);
        if (!cond(c, 0)) return 12;
        c->pc = nn;
        return 16;
    }
    case 0xC3: c->pc = fetch16(gb); return 16;
    case 0xC4: {
        uint16_t nn = fetch16(gb);
        if (!cond(c, 0)) return 12;
        push(gb, c->pc);
        c->pc = nn;
        return 24;
    }
    case 0xC5: push(gb, ((uint16_t)c->b << 8) | c->c); return 16;
    case 0xC6: alu_add(c, rb(gb, c->pc++), 0); return 8;
    case 0xC7: push(gb, c->pc); c->pc = 0x00; return 16;
    case 0xC8: if (!cond(c, 1)) return 8; c->pc = pop(gb); return 20;
    case 0xC9: c->pc = pop(gb); return 16;
    case 0xCA: {
        uint16_t nn = fetch16(gb);
        if (!cond(c, 1)) return 12;
        c->pc = nn;
        return 16;
    }
    case 0xCB: return exec_cb(gb);
    case 0xCC: {
        uint16_t nn = fetch16(gb);
        if (!cond(c, 1)) return 12;
        push(gb, c->pc);
        c->pc = nn;
        return 24;
    }
    case 0xCD: {
        uint16_t nn = fetch16(gb);
        push(gb, c->pc);
        c->pc = nn;
        return 24;
    }
    case 0xCE: alu_add(c, rb(gb, c->pc++), get_flag(c, FLAG_C)); return 8;
    case 0xCF: push(gb, c->pc); c->pc = 0x08; return 16;
    case 0xD0: if (!cond(c, 2)) return 8; c->pc = pop(gb); return 20;
    case 0xD1: set_rr(c, 1, pop(gb)); return 12;
    case 0xD2: {
        uint16_t nn = fetch16(gb);
        if (!cond(c, 2)) return 12;
        c->pc = nn;
        return 16;
    }
    case 0xD4: {
        uint16_t nn = fetch16(gb);
        if (!cond(c, 2)) return 12;
        push(gb, c->pc);
        c->pc = nn;
        return 24;
    }
    case 0xD5: push(gb, ((uint16_t)c->d << 8) | c->e); return 16;
    case 0xD6: alu_sub(c, rb(gb, c->pc++), 0); return 8;
    case 0xD7: push(gb, c->pc); c->pc = 0x10; return 16;
    case 0xD8: if (!cond(c, 3)) return 8; c->pc = pop(gb); return 20;
    case 0xD9: c->pc = pop(gb); c->ime = 1; return 16;
    case 0xDA: {
        uint16_t nn = fetch16(gb);
        if (!cond(c, 3)) return 12;
        c->pc = nn;
        return 16;
    }
    case 0xDC: {
        uint16_t nn = fetch16(gb);
        if (!cond(c, 3)) return 12;
        push(gb, c->pc);
        c->pc = nn;
        return 24;
    }
    case 0xDE: alu_sub(c, rb(gb, c->pc++), get_flag(c, FLAG_C)); return 8;
    case 0xDF: push(gb, c->pc); c->pc = 0x18; return 16;
    case 0xE0: wb(gb, (uint16_t)(0xFF00 + rb(gb, c->pc++)), c->a); return 12;
    case 0xE1: set_rr(c, 2, pop(gb)); return 12;
    case 0xE2: wb(gb, (uint16_t)(0xFF00 + c->c), c->a); return 8;
    case 0xE5: push(gb, get_hl(c)); return 16;
    case 0xE6: {
        uint8_t n = rb(gb, c->pc++);
        c->f = FLAG_H;
        c->a &= n;
        if (c->a == 0) c->f |= FLAG_Z;
        return 8;
    }
    case 0xE7: push(gb, c->pc); c->pc = 0x20; return 16;
    case 0xE8: {
        uint8_t imm = rb(gb, c->pc++);
        uint8_t f = 0;
        if (((c->sp & 0xF) + (imm & 0xF)) > 0xF) f |= FLAG_H;
        if (((c->sp & 0xFF) + imm) > 0xFF) f |= FLAG_C;
        c->f = f;
        c->sp = (uint16_t)(c->sp + (int8_t)imm);
        return 16;
    }
    case 0xE9: c->pc = get_hl(c); return 4;
    case 0xEA: {
        uint16_t nn = fetch16(gb);
        wb(gb, nn, c->a);
        return 16;
    }
    case 0xEE: {
        uint8_t n = rb(gb, c->pc++);
        c->f = 0;
        c->a ^= n;
        if (c->a == 0) c->f |= FLAG_Z;
        return 8;
    }
    case 0xEF: push(gb, c->pc); c->pc = 0x28; return 16;
    case 0xF0: c->a = rb(gb, (uint16_t)(0xFF00 + rb(gb, c->pc++))); return 12;
    case 0xF1: {
        uint16_t v = pop(gb);
        c->a = (uint8_t)(v >> 8);
        c->f = (uint8_t)(v & 0xF0);
        return 12;
    }
    case 0xF2: c->a = rb(gb, (uint16_t)(0xFF00 + c->c)); return 8;
    case 0xF3: c->ime = 0; c->ei_delay = 0; return 4;
    case 0xF5: push(gb, ((uint16_t)c->a << 8) | (c->f & 0xF0)); return 16;
    case 0xF6: {
        uint8_t n = rb(gb, c->pc++);
        c->f = 0;
        c->a |= n;
        if (c->a == 0) c->f |= FLAG_Z;
        return 8;
    }
    case 0xF7: push(gb, c->pc); c->pc = 0x30; return 16;
    case 0xF8: {
        uint8_t imm = rb(gb, c->pc++);
        uint16_t r = (uint16_t)(c->sp + (int8_t)imm);
        uint8_t f = 0;
        if (((c->sp & 0xF) + (imm & 0xF)) > 0xF) f |= FLAG_H;
        if (((c->sp & 0xFF) + imm) > 0xFF) f |= FLAG_C;
        c->f = f;
        set_hl(c, r);
        return 12;
    }
    case 0xF9: c->sp = get_hl(c); return 8;
    case 0xFA: {
        uint16_t nn = fetch16(gb);
        c->a = rb(gb, nn);
        return 16;
    }
    case 0xFB: c->ei_delay = 2; return 4;
    case 0xFE: alu_cmp(c, rb(gb, c->pc++)); return 8;
    case 0xFF: push(gb, c->pc); c->pc = 0x38; return 16;

    default: {
        static int illegal_count = 0;
        if (illegal_count < 8) {
            fprintf(stderr, "CPU: illegal opcode $%02X at $%04X\n", op, pc0);
            if (++illegal_count == 8)
                fprintf(stderr, "CPU: suppressing further illegal opcode messages\n");
        }
        return 4;
    }
    }
}

int cpu_step(struct GameBoy *gb)
{
    int cycles = do_step(gb);
    CPU *c = &gb->cpu;
    if (c->ei_delay > 0) {
        c->ei_delay--;
        if (!c->ei_delay) c->ime = 1;
    }
    return cycles;
}
