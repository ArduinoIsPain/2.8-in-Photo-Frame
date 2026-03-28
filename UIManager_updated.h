#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Adafruit_FT6206.h>

// Simple button struct
struct Button {
  int16_t x, y, w, h;
  const char* label;
};

class UIManager {
public:
  enum class State : uint8_t {
    SLIDESHOW,
    MENU,
    UPLOAD,
    SETTINGS,
    BRIGHTNESS,
    INTERVAL,
    ABOUT
  };

  enum class SmoothFont {
  SMALL,
  BUTTON,
  TITLE
};

struct Theme {
  uint16_t BG;
  uint16_t fill;
  uint16_t outline;
  uint16_t highlight;
  uint16_t shadow;
  uint16_t textCol;
  uint16_t textMutedCol;
  uint16_t ACCENT_YELLOW;
  uint16_t ACCENT_PURPLE;
  int r;
};

static Theme theme;
static void themeInitDefault();   // sets your current palette
static void setTheme(const Theme& t);



enum class FitMode : uint8_t { FIT, FILL };
static FitMode fitMode;


  static void begin(TFT_eSPI* tft, Adafruit_FT6206* touch);
  static void update();
  static void processTouch();

  // Optional: call from an ISR or from loop when INT triggers
  static void notifyTouchInterrupt();

  // Settings you can persist later
  static uint8_t getBrightness();
  static void setBrightness(uint8_t val);


  // Slideshow interval (ms)
  static uint32_t getSlideIntervalMs();
  static void setSlideIntervalMs(uint32_t ms);

  // Touch debounce
  static void setDebounceMs(uint16_t ms);

  //fonts
  static void useSmoothFont(SmoothFont f);  
  static void unloadSmoothFont();

  static void drawBrightnessBarAndValue();
  static void drawIntervalBarAndValue();

  static void drawHeader(const char* title);

  static void sleepNow();
  static bool isSleeping();

private:
  static TFT_eSPI* tftPtr;
  static Adafruit_FT6206* touchPtr;

  static State state;

  // --- slideshow ---
  static uint32_t lastSlideMs;
  static uint32_t slideIntervalMs;
  static int currentPhotoIndex;
  static int photoCount;

  // --- overlay/menu trigger ---
  static volatile bool touchInterruptFlag;
  static bool overlayActive;
  static uint32_t overlayUntilMs;

  // --- brightness ---
  static uint8_t brightness;
  static const int BL_PWM_CHANNEL = 0;
  static const int BL_PWM_FREQ    = 5000;
  static const int BL_PWM_RES     = 8;


  // --- fonts (optional)
  static const GFXfont* fontTitle;
  static const GFXfont* fontMenu;
  static const GFXfont* fontSmall;

  // --- touch debounce ---
  static uint32_t lastTouchMs;
  static uint16_t debounceMs;
  static bool wasTouched;
  static uint32_t touchDownMs;


  // --- buttons ---
  static Button btnMenu;      // appears as overlay in slideshow
  static Button btnSleep;
  static Button btnResume;
  static Button btnSelect;
  static Button btnSettings;
  static Button btnBack;

  // Settings screen
  static Button btnBrightness;
  static Button btnInterval;
  static Button btnAbout;

  // Photo select
  static Button btnPrevPhoto;
  static Button btnNextPhoto;

  // Brightness screen
  static Button btnBrUp;
  static Button btnBrDown;

  // Interval screen
  static Button btnIntUp;
  static Button btnIntDown;

  // --- helpers ---
  static void drawButton(const Button& b);
  static void useFont(const GFXfont* f);
  static bool hit(const Button& b, int16_t x, int16_t y);

  static void showSlideshow(bool forceRedraw);
  static void showMenu();
  static void showUpload();
  static void showSettings();
  static void showBrightness();
  static void showInterval();
  static void showAbout();

  static void renderCurrentPhoto();
  static bool loadPhotoListCount();
  static String photoPathByIndex(int idx);

  // TJpg decoder callback
  static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

  // touch mapping (you said your mapping is already correct — keep this identical across builds)
  static void mapTouchToScreen(int16_t rawX, int16_t rawY, int16_t& sx, int16_t& sy);

  static void applyBacklight();
};
