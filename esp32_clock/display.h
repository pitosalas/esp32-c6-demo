#pragma once
// Shared drawing helpers for the clock and ROS pages. Defined in clock.cpp.

#include <Arduino_GFX_Library.h>

#define COLOR_DIM 0x7BEF      // mid grey
#define COLOR_FAINT 0x39E7    // dark grey, for dividers
#define COLOR_CLOUD 0xC618    // light grey
#define COLOR_SUN 0xFE60      // warm yellow
#define COLOR_RAIN 0x3D9F     // sky blue
#define COLOR_DATE 0x5F1F     // soft cyan
#define COLOR_OK 0x87F0       // green
#define COLOR_WARN 0xFEA0     // amber
#define COLOR_BAD 0xF8A3      // red

enum Align { LEFT, CENTER, RIGHT };

extern Arduino_Canvas *gfx;

// Draws text with its baseline at y, anchored at x. Returns the x just past the last glyph.
int drawText(const char *text, int x, int y, const GFXfont *font, uint16_t color, Align align = LEFT);

// Draws text in the built-in 6x8 font with its top-left corner at (x, y).
void drawSmallText(const char *text, int x, int y, uint16_t color);

// The FreeSans fonts are 7-bit, so the degree sign is drawn as a small ring.
void drawDegree(int x, int top, int r, uint16_t color);

// Text colour for a temperature in Fahrenheit, from icy blue to hot red.
uint16_t tempColor(int f);
