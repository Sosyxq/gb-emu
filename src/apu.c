#include <string.h>

#include "apu.h"
#include "gb.h"

static const uint8_t duty_table[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 0},
};

static void ring_push(APU *a, int16_t l, int16_t r)
{
    unsigned next = a->ring_w + 1;
    if (next >= APU_RING) next = 0;
    if (next == a->ring_r) {
        a->ring_r++;
        if (a->ring_r >= APU_RING) a->ring_r = 0;
    }
    a->ring[a->ring_w * 2] = l;
    a->ring[a->ring_w * 2 + 1] = r;
    a->ring_w = next;
}

int apu_drain(APU *a, int16_t *dst, int max_frames)
{
    int n = 0;
    while (n < max_frames && a->ring_r != a->ring_w) {
        dst[n * 2] = a->ring[a->ring_r * 2];
        dst[n * 2 + 1] = a->ring[a->ring_r * 2 + 1];
        a->ring_r++;
        if (a->ring_r >= APU_RING) a->ring_r = 0;
        n++;
    }
    return n;
}

void apu_init(APU *a)
{
    a->sample_clock = 0;
    a->ring_r = 0;
    a->ring_w = 0;
    a->hp_l = 0;
    a->hp_r = 0;
    a->hp_prev_l = 0;
    a->hp_prev_r = 0;
    a->fs_step = 0;
    a->fs_timer = 0;
    a->wave_just_read = 0;
    a->wave_read_pos = 0;
    a->wave_last_sample = 0;
    a->wave_read_window = 0;
    a->ch1.sweep_negate_used = 0;
    a->ch2.sweep_negate_used = 0;
}

static void pulse_trigger(APU *a, Pulse *c)
{
    c->enabled = c->dac;
    if (c->length == 0) {
        c->length = 64;
        /* Trigger + length_en + next FS step won't clock length → 63 */
        if (c->length_en && (a->fs_step & 1))
            c->length = 63;
    }
    c->volume = c->volume_init;
    c->env_timer = c->env_period ? c->env_period : 8;
    c->timer = (2048 - (c->freq & 0x7FF)) * 4;
    if (c->timer <= 0) c->timer = 4;
    c->duty_pos = 0;
    c->shadow = c->freq & 0x7FF;
    c->sweep_timer = c->sweep_period ? c->sweep_period : 8;
    c->sweep_en = c->sweep_period || c->sweep_shift;
    c->sweep_negate_used = 0;
    if (c->sweep_shift) {
        int delta = c->shadow >> c->sweep_shift;
        int nf = c->sweep_negate ? c->shadow - delta : c->shadow + delta;
        if (c->sweep_negate) c->sweep_negate_used = 1;
        if (nf > 2047 || nf < 0) c->enabled = 0;
    }
}

static void wave_trigger(struct GameBoy *gb, Wave *w)
{
    int was_on = w->enabled && w->dac;
    int old_timer = w->timer;
    int old_pos = w->pos;

    w->enabled = w->dac;
    if (w->length == 0) {
        w->length = 256;
        if (w->length_en && (gb->apu.fs_step & 1))
            w->length = 255;
    }
    /* DMG bug: corrupt when retriggered on the APU tick that will read
     * (SameBoy sample_countdown==0). Our timer is T-cycles; we only ever
     * observe writes on even boundaries, so timer==2 => next 2MHz tick reads. */
    if (was_on && gb && old_timer == 2) {
        int offset = ((old_pos + 1) >> 1) & 0x0F;
        if (offset < 4) {
            gb->io[0x30] = gb->io[0x30 + offset];
        } else {
            int base = offset & ~3;
            gb->io[0x30] = gb->io[0x30 + base];
            gb->io[0x31] = gb->io[0x30 + base + 1];
            gb->io[0x32] = gb->io[0x30 + base + 2];
            gb->io[0x33] = gb->io[0x30 + base + 3];
        }
    }
    w->timer = (2048 - (w->freq & 0x7FF)) * 2;
    if (w->timer <= 0) w->timer = 2;
    /* DMG: +6 cycle delay on wave period after trigger (blargg 09/10) */
    w->timer += 6;
    w->pos = 0;
    w->wave_pos = 0;
    w->wave_read_window = 0;
}

static void noise_trigger(APU *a, Noise *n)
{
    n->enabled = n->dac;
    if (n->length == 0) {
        n->length = 64;
        if (n->length_en && (a->fs_step & 1))
            n->length = 63;
    }
    n->volume = n->volume_init;
    n->env_timer = n->env_period ? n->env_period : 8;
    n->lfsr = 0x7FFF;
    {
        int div = n->div_code ? n->div_code * 16 : 8;
        n->timer = div << n->shift;
        if (n->timer <= 0) n->timer = 8;
    }
}

static void clock_length_channel(int *enabled, int *length, int length_en)
{
    if (!length_en || *length <= 0) return;
    (*length)--;
    if (*length == 0) *enabled = 0;
}

static void clock_length(APU *a)
{
    clock_length_channel(&a->ch1.enabled, &a->ch1.length, a->ch1.length_en);
    clock_length_channel(&a->ch2.enabled, &a->ch2.length, a->ch2.length_en);
    clock_length_channel(&a->ch3.enabled, &a->ch3.length, a->ch3.length_en);
    clock_length_channel(&a->ch4.enabled, &a->ch4.length, a->ch4.length_en);
}

static void extra_length_clock_pulse(Pulse *c)
{
    if (c->length <= 0) return;
    c->length--;
    if (c->length == 0) c->enabled = 0;
}

static void extra_length_clock_wave(Wave *w)
{
    if (w->length <= 0) return;
    w->length--;
    if (w->length == 0) w->enabled = 0;
}

static void extra_length_clock_noise(Noise *n)
{
    if (n->length <= 0) return;
    n->length--;
    if (n->length == 0) n->enabled = 0;
}

static void clock_envelope_pulse(Pulse *c)
{
    if (c->env_period == 0) return;
    if (--c->env_timer > 0) return;
    c->env_timer = c->env_period;
    if (c->env_dir) {
        if (c->volume < 15) c->volume++;
    } else {
        if (c->volume > 0) c->volume--;
    }
}

static void clock_envelope_noise(Noise *n)
{
    if (n->env_period == 0) return;
    if (--n->env_timer > 0) return;
    n->env_timer = n->env_period;
    if (n->env_dir) {
        if (n->volume < 15) n->volume++;
    } else {
        if (n->volume > 0) n->volume--;
    }
}

static void pulse_sweep_update(Pulse *c)
{
    int delta = c->shadow >> c->sweep_shift;
    int nf = c->sweep_negate ? c->shadow - delta : c->shadow + delta;
    if (c->sweep_negate) c->sweep_negate_used = 1;
    if (nf > 2047 || nf < 0) {
        c->enabled = 0;
        return;
    }
    if (c->sweep_shift == 0) return;
    c->shadow = nf;
    c->freq = nf;
    delta = c->shadow >> c->sweep_shift;
    nf = c->sweep_negate ? c->shadow - delta : c->shadow + delta;
    if (nf > 2047 || nf < 0) c->enabled = 0;
}

static void clock_sweep(Pulse *c)
{
    if (c->sweep_timer > 0) c->sweep_timer--;
    if (c->sweep_timer > 0) return;
    c->sweep_timer = c->sweep_period ? c->sweep_period : 8;
    if (!c->sweep_en || c->sweep_period == 0) return;
    pulse_sweep_update(c);
}

static void frame_sequencer(APU *a)
{
    int step = a->fs_step;
    a->fs_step = (step + 1) & 7;

    if ((step & 1) == 0)
        clock_length(a);
    if (step == 2 || step == 6)
        clock_sweep(&a->ch1);
    if (step == 7) {
        clock_envelope_pulse(&a->ch1);
        clock_envelope_pulse(&a->ch2);
        clock_envelope_noise(&a->ch4);
    }
}

static void tick_pulse(Pulse *c)
{
    if (!c->enabled) return;
    c->timer--;
    if (c->timer > 0) return;
    {
        int p = 2048 - (c->freq & 0x7FF);
        if (p < 1) p = 1;
        c->timer = p * 4;
    }
    c->duty_pos = (c->duty_pos + 1) & 7;
}

static void tick_wave(struct GameBoy *gb, Wave *w)
{
    if (!w->enabled) return;
    w->timer--;
    if (w->timer > 0) return;
    {
        int p = 2048 - (w->freq & 0x7FF);
        if (p < 1) p = 1;
        w->timer = p * 2;
    }
    w->pos = (w->pos + 1) & 31;
    w->wave_pos = w->pos;
    w->wave_just_read = 1;
    w->wave_read_window = 2;
    (void)gb;
}

static void tick_noise(Noise *n)
{
    if (!n->enabled) return;
    n->timer--;
    if (n->timer > 0) return;
    {
        int div = n->div_code ? n->div_code * 16 : 8;
        n->timer = div << n->shift;
        if (n->timer <= 0) n->timer = 8;
    }
    {
        int x = (n->lfsr & 1) ^ ((n->lfsr >> 1) & 1);
        n->lfsr = (uint16_t)((n->lfsr >> 1) | (x << 14));
        if (n->width) {
            n->lfsr = (uint16_t)((n->lfsr & ~(1 << 6)) | (x << 6));
        }
    }
}

static int pulse_out(Pulse *c)
{
    if (!c->enabled || !c->dac) return 0;
    return duty_table[c->duty][c->duty_pos] ? c->volume : 0;
}

static int wave_out(struct GameBoy *gb, Wave *w)
{
    if (!w->enabled || !w->dac) return 0;
    {
        uint8_t b = gb->io[0x30 + (w->pos >> 1)];
        int s = (w->pos & 1) ? (b & 0x0F) : (b >> 4);
        if (w->vol_code == 0) return 0;
        return s >> (w->vol_code - 1);
    }
}

static int noise_out(Noise *n)
{
    if (!n->enabled || !n->dac) return 0;
    return (n->lfsr & 1) ? 0 : n->volume;
}

static void mix_sample(struct GameBoy *gb, APU *a)
{
    int left = 0, right = 0;
    int nr51 = gb->io[0x25];
    int nr50 = gb->io[0x24];
    int c1 = pulse_out(&a->ch1);
    int c2 = pulse_out(&a->ch2);
    int c3 = wave_out(gb, &a->ch3);
    int c4 = noise_out(&a->ch4);
    int ol, or_;
    double sl, sr;

    if (!a->powered) {
        c1 = c2 = c3 = c4 = 0;
    }

    if (nr51 & 0x10) left += c1;
    if (nr51 & 0x20) left += c2;
    if (nr51 & 0x40) left += c3;
    if (nr51 & 0x80) left += c4;
    if (nr51 & 0x01) right += c1;
    if (nr51 & 0x02) right += c2;
    if (nr51 & 0x04) right += c3;
    if (nr51 & 0x08) right += c4;

    left *= ((nr50 >> 4) & 7) + 1;
    right *= (nr50 & 7) + 1;

    ol = left * 32767 / 480;
    or_ = right * 32767 / 480;

    a->hp_l = ol - a->hp_prev_l + a->hp_l * 0.999958;
    a->hp_r = or_ - a->hp_prev_r + a->hp_r * 0.999958;
    a->hp_prev_l = ol;
    a->hp_prev_r = or_;

    sl = a->hp_l;
    sr = a->hp_r;
    if (sl > 32767.0) sl = 32767.0;
    if (sl < -32768.0) sl = -32768.0;
    if (sr > 32767.0) sr = 32767.0;
    if (sr < -32768.0) sr = -32768.0;

    ring_push(a, (int16_t)sl, (int16_t)sr);
}

static void apu_power_off(struct GameBoy *gb)
{
    APU *a = &gb->apu;
    int i;

    for (i = 0x10; i <= 0x25; i++)
        gb->io[i] = 0;
    for (i = 0x27; i <= 0x2F; i++)
        gb->io[i] = 0;

    a->powered = 0;
    a->fs_step = 0;
    a->fs_timer = 0;

    a->ch1.enabled = 0;
    a->ch1.dac = 0;
    a->ch1.freq = 0;
    a->ch1.length_en = 0;
    a->ch1.volume = 0;
    a->ch1.volume_init = 0;
    a->ch1.env_dir = 0;
    a->ch1.env_period = 0;
    a->ch1.sweep_period = 0;
    a->ch1.sweep_negate = 0;
    a->ch1.sweep_shift = 0;
    a->ch1.sweep_en = 0;
    a->ch1.sweep_negate_used = 0;
    a->ch1.duty = 0;
    a->ch1.length_data = 0;

    a->ch2.enabled = 0;
    a->ch2.dac = 0;
    a->ch2.freq = 0;
    a->ch2.length_en = 0;
    a->ch2.volume = 0;
    a->ch2.volume_init = 0;
    a->ch2.env_dir = 0;
    a->ch2.env_period = 0;
    a->ch2.duty = 0;
    a->ch2.length_data = 0;

    a->ch3.enabled = 0;
    a->ch3.dac = 0;
    a->ch3.freq = 0;
    a->ch3.length_en = 0;
    a->ch3.vol_code = 0;
    a->ch3.length_data = 0;
    a->ch3.wave_pos = 0;
    a->ch3.wave_just_read = 0;
    a->ch3.wave_read_window = 0;

    a->ch4.enabled = 0;
    a->ch4.dac = 0;
    a->ch4.length_en = 0;
    a->ch4.volume = 0;
    a->ch4.volume_init = 0;
    a->ch4.env_dir = 0;
    a->ch4.env_period = 0;
    a->ch4.shift = 0;
    a->ch4.width = 0;
    a->ch4.div_code = 0;
    a->ch4.length_data = 0;
}

static void apu_power_on(struct GameBoy *gb)
{
    APU *a = &gb->apu;
    a->powered = 1;
    a->fs_step = 0;
    a->fs_timer = 0;
    gb->io[0x26] = 0x80;
}

void apu_step(struct GameBoy *gb, int cycles)
{
    APU *a = &gb->apu;
    int i;

    if (cycles < 0) cycles = 0;

    if (a->powered) {
        a->fs_timer += cycles;
        while (a->fs_timer >= 8192) {
            a->fs_timer -= 8192;
            frame_sequencer(a);
        }

        for (i = 0; i < cycles; i++) {
            tick_pulse(&a->ch1);
            tick_pulse(&a->ch2);
            tick_wave(gb, &a->ch3);
            tick_noise(&a->ch4);
            if (a->ch3.wave_read_window > 0)
                a->ch3.wave_read_window--;
        }
    }

    a->sample_clock += (int64_t)cycles * APU_SAMPLE_RATE;
    while (a->sample_clock >= APU_CPU_HZ) {
        a->sample_clock -= APU_CPU_HZ;
        mix_sample(gb, a);
    }
}

static uint8_t read_masked(uint16_t addr, uint8_t v)
{
    switch (addr) {
    case 0xFF10: return (uint8_t)(v | 0x80);
    case 0xFF11: return (uint8_t)((v & 0xC0) | 0x3F);
    case 0xFF12: return v;
    case 0xFF13: return 0xFF;
    case 0xFF14: return (uint8_t)((v & 0x40) | 0xBF);
    case 0xFF16: return (uint8_t)((v & 0xC0) | 0x3F);
    case 0xFF17: return v;
    case 0xFF18: return 0xFF;
    case 0xFF19: return (uint8_t)((v & 0x40) | 0xBF);
    case 0xFF1A: return (uint8_t)((v & 0x80) | 0x7F);
    case 0xFF1B: return 0xFF;
    case 0xFF1C: return (uint8_t)((v & 0x60) | 0x9F);
    case 0xFF1D: return 0xFF;
    case 0xFF1E: return (uint8_t)((v & 0x40) | 0xBF);
    case 0xFF20: return 0xFF;
    case 0xFF21: return v;
    case 0xFF22: return v;
    case 0xFF23: return (uint8_t)((v & 0x40) | 0xBF);
    case 0xFF24: return v;
    case 0xFF25: return v;
    default: return v;
    }
}

uint8_t apu_read(struct GameBoy *gb, uint16_t addr)
{
    APU *a = &gb->apu;
    uint8_t v = gb->io[addr - 0xFF00];

    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        if (a->powered && a->ch3.enabled && a->ch3.dac) {
            int cur = (a->ch3.wave_pos >> 1) & 0x0F;
            if (a->ch3.wave_read_window > 0)
                return gb->io[0x30 + cur];
            return 0xFF;
        }
        return v;
    }

    if (addr == 0xFF26) {
        uint8_t st = 0;
        if (a->ch1.enabled) st |= 0x01;
        if (a->ch2.enabled) st |= 0x02;
        if (a->ch3.enabled) st |= 0x04;
        if (a->ch4.enabled) st |= 0x08;
        return (uint8_t)(0x70 | (a->powered ? 0x80 : 0) | st);
    }

    if (addr == 0xFF15 || addr == 0xFF1F ||
        (addr >= 0xFF27 && addr <= 0xFF2F))
        return 0xFF;

    if (!a->powered) {
        if (addr >= 0xFF10 && addr <= 0xFF25)
            return read_masked(addr, 0);
        return 0xFF;
    }

    return read_masked(addr, v);
}

static void write_pulse(struct GameBoy *gb, int io, int kind, uint8_t val, Pulse *c, APU *a)
{
    switch (kind) {
    case 0:
        gb->io[io] = (uint8_t)(val & 0x7F);
        {
            int old_neg = c->sweep_negate;
            int new_neg = (val >> 3) & 1;
            c->sweep_period = (val >> 4) & 7;
            c->sweep_negate = new_neg;
            c->sweep_shift = val & 7;
            if (old_neg && !new_neg && c->sweep_negate_used)
                c->enabled = 0;
        }
        break;
    case 1:
        gb->io[io] = val;
        c->duty = val >> 6;
        c->length_data = val & 0x3F;
        c->length = 64 - c->length_data;
        break;
    case 2:
        gb->io[io] = val;
        c->volume_init = val >> 4;
        c->volume = c->volume_init;
        c->env_dir = (val >> 3) & 1;
        c->env_period = val & 7;
        c->dac = (val & 0xF8) != 0;
        if (!c->dac) c->enabled = 0;
        break;
    case 3:
        gb->io[io] = val;
        c->freq = (c->freq & 0x700) | val;
        break;
    case 4: {
        gb->io[io] = val;
        c->freq = (c->freq & 0x0FF) | ((val & 7) << 8);
        int old_en = c->length_en;
        int do_trig = (val & 0x80) != 0;
        c->length_en = (val >> 6) & 1;
        if (!old_en && c->length_en && (a->fs_step & 1))
            extra_length_clock_pulse(c);
        if (do_trig)
            pulse_trigger(a, c);
        break;
    }
    default:
        break;
    }
}

static void write_wave(struct GameBoy *gb, uint16_t addr, uint8_t val, Wave *w, APU *a)
{
    switch (addr) {
    case 0xFF1A:
        gb->io[0x1A] = (uint8_t)(val & 0x80);
        w->dac = (val & 0x80) != 0;
        if (!w->dac) w->enabled = 0;
        break;
    case 0xFF1B:
        gb->io[0x1B] = val;
        w->length_data = val;
        w->length = 256 - val;
        break;
    case 0xFF1C:
        gb->io[0x1C] = (uint8_t)(val & 0x60);
        w->vol_code = (val >> 5) & 3;
        break;
    case 0xFF1D:
        gb->io[0x1D] = val;
        w->freq = (w->freq & 0x700) | val;
        break;
    case 0xFF1E: {
        gb->io[0x1E] = val;
        w->freq = (w->freq & 0xFF) | ((val & 7) << 8);
        int old_en = w->length_en;
        int do_trig = (val & 0x80) != 0;
        w->length_en = (val >> 6) & 1;
        if (!old_en && w->length_en && (a->fs_step & 1))
            extra_length_clock_wave(w);
        if (do_trig)
            wave_trigger(gb, w);
        break;
    }
    default:
        break;
    }
}

static void write_noise(struct GameBoy *gb, int io, int kind, uint8_t val, Noise *n, APU *a)
{
    switch (kind) {
    case 0:
        gb->io[io] = (uint8_t)(val & 0x3F);
        n->length_data = val & 0x3F;
        n->length = 64 - n->length_data;
        break;
    case 1:
        gb->io[io] = val;
        n->volume_init = val >> 4;
        n->volume = n->volume_init;
        n->env_dir = (val >> 3) & 1;
        n->env_period = val & 7;
        n->dac = (val & 0xF8) != 0;
        if (!n->dac) n->enabled = 0;
        break;
    case 2:
        gb->io[io] = val;
        n->shift = (val >> 4) & 0x0F;
        n->width = (val >> 3) & 1;
        n->div_code = val & 7;
        break;
    case 3: {
        gb->io[io] = val;
        int old_en = n->length_en;
        int do_trig = (val & 0x80) != 0;
        n->length_en = (val >> 6) & 1;
        if (!old_en && n->length_en && (a->fs_step & 1))
            extra_length_clock_noise(n);
        if (do_trig)
            noise_trigger(a, n);
        break;
    }
    default:
        break;
    }
}

void apu_write(struct GameBoy *gb, uint16_t addr, uint8_t val)
{
    APU *a = &gb->apu;
    int idx = addr - 0xFF00;

    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        if (a->powered && a->ch3.enabled && a->ch3.dac) {
            if (a->ch3.wave_read_window > 0) {
                int cur = (a->ch3.wave_pos >> 1) & 0x0F;
                gb->io[0x30 + cur] = val;
            }
            return;
        }
        gb->io[idx] = val;
        return;
    }

    if (addr == 0xFF26) {
        int on = (val & 0x80) != 0;
        if (on && !a->powered)
            apu_power_on(gb);
        else if (!on && a->powered)
            apu_power_off(gb);
        else
            gb->io[0x26] = (uint8_t)(val & 0x80);
        return;
    }

    if (addr == 0xFF15 || addr == 0xFF1F ||
        (addr >= 0xFF27 && addr <= 0xFF2F))
        return;

    if (!a->powered) {
        /* DMG: length counters can be written while APU is off,
         * but readable registers stay cleared (01-registers #6). */
        if (addr == 0xFF11) {
            a->ch1.duty = val >> 6;
            a->ch1.length_data = val & 0x3F;
            a->ch1.length = 64 - a->ch1.length_data;
        } else if (addr == 0xFF16) {
            a->ch2.duty = val >> 6;
            a->ch2.length_data = val & 0x3F;
            a->ch2.length = 64 - a->ch2.length_data;
        } else if (addr == 0xFF1B) {
            a->ch3.length_data = val;
            a->ch3.length = 256 - val;
        } else if (addr == 0xFF20) {
            a->ch4.length_data = val & 0x3F;
            a->ch4.length = 64 - a->ch4.length_data;
        }
        return;
    }

    if (addr >= 0xFF10 && addr <= 0xFF14) {
        write_pulse(gb, (int)(addr - 0xFF00), (int)(addr - 0xFF10), val, &a->ch1, a);
        return;
    }
    if (addr >= 0xFF16 && addr <= 0xFF19) {
        write_pulse(gb, (int)(addr - 0xFF00), 1 + (int)(addr - 0xFF16), val, &a->ch2, a);
        return;
    }
    if (addr >= 0xFF1A && addr <= 0xFF1E) {
        write_wave(gb, addr, val, &a->ch3, a);
        return;
    }
    if (addr >= 0xFF20 && addr <= 0xFF23) {
        write_noise(gb, (int)(addr - 0xFF00), (int)(addr - 0xFF20), val, &a->ch4, a);
        return;
    }
    if (addr == 0xFF24 || addr == 0xFF25) {
        gb->io[idx] = val;
        return;
    }
}
