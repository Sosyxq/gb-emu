#ifndef FONT8X8_H
#define FONT8X8_H

#include <stdint.h>

/* 8x8 bitmap font, ASCII 32..126.
 * Each glyph is 8 rows top->bottom; bit0 = leftmost pixel.
 * Undefined characters render as blank. */
const uint8_t *font8x8_glyph(int c);

#endif
