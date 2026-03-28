# ESP32 2.8" Touchscreen Photo Frame

A compact embedded photo frame built around an ESP32, featuring a 2.8" touchscreen display, SD card image storage, and a custom UI system.

This project focuses on integrating display rendering, touch input, and file handling into a responsive embedded system.

---

## Features

* 2.8" TFT touchscreen interface
* SD card-based image storage (`/photos` directory)
* JPEG image decoding using TJpg_Decoder
* Custom UI overlay with:

  * Menu navigation
  * Settings control
  * Slideshow interval selection
  * Brightness adjustment
* Auto-hiding UI for full-screen viewing
* Separate SPI bus for SD card to prevent display conflicts

---

## Hardware

* ESP32 development board
* 2.8" TFT display (ILI9341)
* Capacitive touch controller (FT6206 / FT62xx)
* Micro SD card module

### Pin Configuration (example)

| Component     | Pins                          |
| ------------- | ----------------------------- |
| TFT Display   | CS, DC, RST, MOSI, SCLK, MISO |
| Touch (I2C)   | SDA, SCL, INT                 |
| SD Card (SPI) | CS, MOSI, MISO, SCK           |

> Note: SD card runs on a separate SPI bus from the display for stability.

---

## Software

### Libraries Used

* TFT_eSPI
* TJpg_Decoder
* Adafruit FT6206 (or compatible FT62xx library)
* SD / SPI

---

## File Structure

```
/photos
  ├── image1.jpg
  ├── image2.jpg
  └── ...
```

Images are loaded from the SD card at runtime.

---

## How It Works

* Images are read from the SD card and decoded using TJpg_Decoder
* Touch input controls a UI overlay system
* Overlay auto-hides after inactivity for a clean viewing experience
* System manages display updates and input without blocking slideshow performance

---

## Known Issues

* Minor UI quirk during boot (initial state timing issue)

  * Does not affect normal operation after startup

---

## Future Improvements

* Improved boot initialization handling
* Wireless image upload (WiFi interface)
* Higher resolution display support
* Enhanced UI animations

---

## License

Open for personal use and modification. Attribution appreciated if reused or extended.

---

## Author

**generousmotors**
**ArduinoIsPain**

Functional embedded systems combining electronics and mechanical design.
