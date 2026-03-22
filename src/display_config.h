#pragma once
#include <Adafruit_GFX.h>

#if defined(DISPLAY_ST7789)
  #include <Adafruit_ST7789.h>
  typedef Adafruit_ST7789 TftDriver;
#elif defined(DISPLAY_ILI9341)
  #include <Adafruit_ILI9341.h>
  typedef Adafruit_ILI9341 TftDriver;
#else
  #error "Define DISPLAY_ST7789 or DISPLAY_ILI9341 in platformio.ini build_flags"
#endif

// Unified color palette (RGB565 — same values for all Adafruit drivers)
#define CLR_BLACK      0x0000
#define CLR_WHITE      0xFFFF
#define CLR_RED        0xF800
#define CLR_GREEN      0x07E0
#define CLR_BLUE       0x001F
#define CLR_CYAN       0x07FF
#define CLR_YELLOW     0xFFE0
#define CLR_ORANGE     0xFD20
#define CLR_MAGENTA    0xF81F
#define CLR_DARKGREY   0x7BEF
#define CLR_LIGHTGREY  0xC618
