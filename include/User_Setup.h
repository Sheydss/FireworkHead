// TFT_eSPI configuration for the head board (ESP32-S3).
// Placed in include/ so it is tracked by git and takes precedence over the
// library's own User_Setup.h when USER_SETUP_LOADED is defined in platformio.ini.

#define USER_SETUP_INFO "Head ESP32-S3 ILI9488"

// ── Display driver ────────────────────────────────────────────────────────────
#define ILI9488_DRIVER

// ── SPI pins ─────────────────────────────────────────────────────────────────
#define TFT_MOSI  47
#define TFT_SCLK  48
#define TFT_MISO   3
#define TFT_CS     6
#define TFT_DC     7
#define TFT_RST   40

// ── Touch controller (XPT2046) ────────────────────────────────────────────────
#define TOUCH_CS   4
//#define TOUCH_IRQ  2

// ── Fonts ─────────────────────────────────────────────────────────────────────
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// ── SPI speed ─────────────────────────────────────────────────────────────────
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
