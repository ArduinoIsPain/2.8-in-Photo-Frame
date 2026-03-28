 #pragma once

// ===== TFT =====
#define TFT_CS   5
#define TFT_DC   21
#define TFT_RST  15
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_MISO -1      // <-- change from -1 if you want to share the bus (recommended)
#define TFT_BL   14

// ===== Touch (FT62xx) =====
#define TOUCH_SDA 4
#define TOUCH_SCL 41
#define TOUCH_INT 35

// ===== SD (SPI) =====
#define SD_CS_PIN 10     // pick a free GPIO; 10 is fine if unused
#define SD_MOSI   39
#define SD_SCK    40
#define SD_MISO   38

// ===== Optional =====
#define USE_TOUCH_INTERRUPT 1
