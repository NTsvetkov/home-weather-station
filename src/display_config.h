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

// UI palette (v2 design)
#define CLR_STATUS_BG  0x08C4  // #081820 status bar / highlighted column
#define CLR_DIVIDER    0x3A8E  // #3A5070 grid lines
#define CLR_TRACK      0x42CF  // #425878 pressure track, inactive WiFi bars
#define CLR_DIM        0x532F  // #56657D stale / past values
#define CLR_MUTED      0x9D58  // #9AA8C2 labels, units
#define CLR_DIV_STRONG 0x6C14  // #6A82A6 outside/inside divider on the main screen
#define CLR_PRESS      0xFEAD  // #FFD56A pressure value and scale fill
#define CLR_CLOUD      0xC67B  // #C5CEDE secondary text, pictograms, clouds
#define CLR_SKY        0x5E5F  // #5ACAFF humidity drop, rain, headers, < 10C
#define CLR_COOL       0x4C7F  // #4A8DFF 10..18C
#define CLR_OK         0x7ECA  // #7BDA52 18..25C, good WiFi
#define CLR_WARM       0xFD85  // #FFB229 25..30C, warnings
#define CLR_HOT        0xFAC9  // #FF594A > 30C, errors
#define CLR_SUN        0xFE87  // #FFD239 sun icon

// Dimmed icon colors (past blocks on the "today" screen)
#define CLR_SUN_DIM    0x72E3
#define CLR_CLOUD_DIM  0x5AEC
#define CLR_SKY_DIM    0x2ACE
