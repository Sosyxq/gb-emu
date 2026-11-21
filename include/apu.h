#ifndef APU_H
#define APU_H

#include <stdint.h>

struct GameBoy;

#define APU_SAMPLE_RATE 44100
#define APU_CPU_HZ      4194304
#define APU_RING        2048

typedef struct {
    int enabled;
    int dac;
    int timer;
    int duty;
    int duty_pos;
    int freq;
    int volume;
    int volume_init;
    int env_dir;
    int env_period;
    int env_timer;
    int length;
    int length_data;
    int length_en;
    int sweep_period;
    int sweep_negate;
    int sweep_shift;
    int sweep_timer;
    int sweep_en;
    int shadow;
    int sweep_negate_used;
} Pulse;

typedef struct {
    int enabled;
    int dac;
    int timer;
    int pos;
    int freq;
    int vol_code;
    int length;
    int length_data;
    int length_en;
    int wave_pos;
    int wave_just_read;
    int wave_read_window;
} Wave;

typedef struct {
    int enabled;
    int dac;
    int timer;
    uint16_t lfsr;
    int shift;
    int width;
    int div_code;
    int volume;
    int volume_init;
    int env_dir;
    int env_period;
    int env_timer;
    int length;
    int length_data;
    int length_en;
} Noise;

typedef struct {
    Pulse ch1;
    Pulse ch2;
    Wave ch3;
    Noise ch4;
    int powered;
    int fs_step;
    int fs_timer;
    int64_t sample_clock;
    int16_t ring[APU_RING * 2];
    unsigned ring_r;
    unsigned ring_w;
    double hp_l;
    double hp_r;
    int hp_prev_l;
    int hp_prev_r;
    int wave_just_read;
    int wave_read_pos;
    int wave_last_sample;
    int wave_read_window;
} APU;

void apu_init(APU *a);
void apu_step(struct GameBoy *gb, int cycles);
uint8_t apu_read(struct GameBoy *gb, uint16_t addr);
void apu_write(struct GameBoy *gb, uint16_t addr, uint8_t val);
int apu_drain(APU *a, int16_t *dst, int max_frames);

#endif
