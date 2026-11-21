#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gb.h"

static void dump_bmp(GameBoy *gb, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;

    int w = SCREEN_W, h = SCREEN_H;
    int row_bytes = (w * 3 + 3) & ~3;
    int data_size = row_bytes * h;
    int file_size = 54 + data_size;

    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (unsigned char)file_size;
    hdr[3] = (unsigned char)(file_size >> 8);
    hdr[4] = (unsigned char)(file_size >> 16);
    hdr[5] = (unsigned char)(file_size >> 24);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (unsigned char)w;
    hdr[19] = (unsigned char)(w >> 8);
    hdr[20] = (unsigned char)(w >> 16);
    hdr[21] = (unsigned char)(w >> 24);
    hdr[22] = (unsigned char)h;
    hdr[23] = (unsigned char)(h >> 8);
    hdr[24] = (unsigned char)(h >> 16);
    hdr[25] = (unsigned char)(h >> 24);
    hdr[26] = 1;
    hdr[28] = 24;
    hdr[34] = (unsigned char)data_size;
    hdr[35] = (unsigned char)(data_size >> 8);
    hdr[36] = (unsigned char)(data_size >> 16);
    hdr[37] = (unsigned char)(data_size >> 24);
    fwrite(hdr, 1, 54, f);

    unsigned char row[640];
    memset(row, 0, sizeof(row));
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t c = gb->ppu.fb[y * w + x];
            row[x * 3 + 0] = (unsigned char)(c & 0xFF);
            row[x * 3 + 1] = (unsigned char)((c >> 8) & 0xFF);
            row[x * 3 + 2] = (unsigned char)((c >> 16) & 0xFF);
        }
        fwrite(row, 1, (size_t)row_bytes, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <rom.gb> [frames] [shot.bmp]\n", argv[0]);
        return 2;
    }
    const char *path = argv[1];
    int max_frames = argc > 2 ? atoi(argv[2]) : 4000;
    const char *shot = argc > 3 ? argv[3] : NULL;

    GameBoy *gb = calloc(1, sizeof(GameBoy));
    if (!gb) return 2;

    gb_init(gb);
    int rc = gb_load_rom(gb, path);
    if (rc != 0) {
        fprintf(stderr, "load failed (%d): %s\n", rc, path);
        free(gb);
        return 2;
    }

    for (int f = 0; f < max_frames; f++)
        gb_run_frame(gb);

    printf("=== %s ===\n", path);

    if (gb->serial_len == 0)
        printf("(no serial output after %d frames)\n", max_frames);
    else {
        fwrite(gb->serial, 1, (size_t)gb->serial_len, stdout);
        if (gb->serial[gb->serial_len - 1] != '\n') putchar('\n');
    }

    if (shot) {
        dump_bmp(gb, shot);
        printf("(screenshot: %s)\n", shot);
    }

    char screen[512];
    int slen = 0;
    for (int r = 0; r < 18 && slen < (int)sizeof(screen) - 21; r++) {
        for (int c = 0; c < 20 && slen < (int)sizeof(screen) - 1; c++) {
            uint8_t t = gb->vram[0][0x1800 + r * 32 + c];
            screen[slen++] = (t >= 32 && t < 127) ? (char)t : ' ';
        }
        screen[slen++] = '\n';
    }
    screen[slen] = '\0';

    const char *text = gb->serial_len ? gb->serial : screen;
    int pass = strstr(text, "Pass") != NULL || strstr(text, "pass") != NULL;
    int fail = strstr(text, "Fail") != NULL || strstr(text, "fail") != NULL
            || strstr(text, "FAILED") != NULL;

    int result;
    if (gb->serial_len == 0 && !pass && !fail) {
        printf("=> NO OUTPUT\n");
        result = 3;
    } else if (pass && !fail) {
        printf("=> PASS\n");
        result = 0;
    } else if (fail) {
        printf("=> FAIL\n");
        result = 1;
    } else {
        printf("=> INCONCLUSIVE\n");
        result = 4;
    }
    putchar('\n');

    cart_free(&gb->cart);
    free(gb);
    return result;
}
