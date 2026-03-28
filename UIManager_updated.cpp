#include "UIManager_updated.h"
#include <Wire.h>
#include <TJpg_Decoder.h>
#include "Pins.h"

// FS options
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>        // SPI SD
#include "upload_mode.h"

// -------------------- Static members --------------------
TFT_eSPI* UIManager::tftPtr = nullptr;
Adafruit_FT6206* UIManager::touchPtr = nullptr;


UIManager::State UIManager::state = UIManager::State::SLIDESHOW;
UIManager::FitMode UIManager::fitMode = UIManager::FitMode::FILL;

uint32_t UIManager::lastSlideMs = 0;
uint32_t UIManager::slideIntervalMs = 20000;

uint32_t UIManager::lastTouchMs = 0;
uint16_t UIManager::debounceMs = 180;
int UIManager::currentPhotoIndex = 0;
int UIManager::photoCount = 0;
bool UIManager::wasTouched = false;
uint32_t UIManager::touchDownMs = 0;

volatile bool UIManager::touchInterruptFlag = false;
bool UIManager::overlayActive = false;
uint32_t UIManager::overlayUntilMs = 0;
static bool gButtonLatch = false;

uint8_t UIManager::brightness = 255;

static bool     s_sleeping = false;
static bool    s_sleepWaitRelease = false;
static uint8_t  s_prevBrightness = 255;


// Buttons (positions tuned for 320x240 landscape)
Button UIManager::btnMenu       = { 252, 16,  60, 34, "" };
Button UIManager::btnSleep      = { 12, 16, 60, 34, "" };  // top-left
Button UIManager::btnResume     = { 60,  60, 200, 44, "Resume" };
Button UIManager::btnSelect     = { 60, 112, 200, 44, "Upload Photos" };
Button UIManager::btnSettings   = { 60, 164, 200, 44, "Settings" };

Button UIManager::btnBack       = { 8,   12,  70, 34, "<<<" };

// Settings screen buttons
Button UIManager::btnBrightness = { 60,  70, 200, 44, "Brightness" };
Button UIManager::btnInterval   = { 60, 122, 200, 44, "Interval" };
Button UIManager::btnAbout      = { 60, 174, 200, 44, "About" };

// Brightness +/- buttons
Button UIManager::btnBrDown     = { 8, 196, 120, 40, "-" };
Button UIManager::btnBrUp       = { 192,196, 120, 40, "+" };

// Interval +/- buttons
Button UIManager::btnIntDown    = { 8, 196, 120, 40, "-" };
Button UIManager::btnIntUp      = { 192,196, 120, 40, "+" };

// Slider geometry (shared style)
static constexpr int BR_BAR_X = 40;
static constexpr int BR_BAR_Y = 90;
static constexpr int BR_BAR_W = 240;
static constexpr int BR_BAR_H = 18;


static constexpr int INT_BAR_X = 40;
static constexpr int INT_BAR_Y = 90;
static constexpr int INT_BAR_W = 240;
static constexpr int INT_BAR_H = 18;

static constexpr uint32_t INT_MIN_MS = 5000;
static constexpr uint32_t INT_MAX_MS = 180000;


UIManager::Theme UIManager::theme;



// -------------------- Photo list cache --------------------
static const char* kPhotosDir = "/photos"; // on SD or LittleFS
static fs::FS* gPhotoFS = nullptr;         // points at SD or LittleFS
static bool gPhotoFSIsSD = false;

// Cache of absolute paths ("/photos/xxx.jpg")
#include <vector>
static std::vector<String> gPhotoPaths;

// -------------------- Helpers --------------------
static bool isJpgName(const String& name) {
  String low = name; low.toLowerCase();
  return (low.endsWith(".jpg") || low.endsWith(".jpeg"));
}

static int rebuildPhotoList() {
  gPhotoPaths.clear();

  if (!gPhotoFS) {
    Serial.println("No photo FS selected.");
    return 0;
  }

  File root = gPhotoFS->open(kPhotosDir);
  if (!root || !root.isDirectory()) {
    Serial.printf("No %s directory on selected FS.\n", kPhotosDir);
    return 0;
  }

  File f = root.openNextFile();
  while (f) {
    String full = f.name();
    if (!f.isDirectory() && isJpgName(full)) {
      int lastSlash = full.lastIndexOf('/');
      String base = (lastSlash >= 0) ? full.substring(lastSlash + 1) : full;
      String absPath = String(kPhotosDir) + "/" + base;
      gPhotoPaths.push_back(absPath);
    }
    f = root.openNextFile();
  }

  int count = (int)gPhotoPaths.size();
  Serial.printf("Found %d photos in %s on %s\n",
                count, kPhotosDir, gPhotoFSIsSD ? "SD" : "LittleFS");
  return count;
}

static bool getJpegSizeFromFS(fs::FS& fs, const char* path, uint16_t* w, uint16_t* h) {
  File f = fs.open(path, FILE_READ);
  if (!f) return false;

  auto rd16 = [&](uint16_t &out)->bool {
    int a = f.read(), b = f.read();
    if (a < 0 || b < 0) return false;
    out = (uint16_t(a) << 8) | uint16_t(b);
    return true;
  };

  // SOI
  int a = f.read(), b = f.read();
  if (a != 0xFF || b != 0xD8) { f.close(); return false; }

  while (f.available()) {
    int c;
    do { c = f.read(); if (c < 0) { f.close(); return false; } } while (c != 0xFF);
    do { c = f.read(); if (c < 0) { f.close(); return false; } } while (c == 0xFF);

    uint8_t marker = (uint8_t)c;
    if (marker == 0xDA || marker == 0xD9) break;

    uint16_t len = 0;
    if (!rd16(len) || len < 2) { f.close(); return false; }

    bool isSOF = (marker >= 0xC0 && marker <= 0xCF) &&
                 marker != 0xC4 && marker != 0xC8 && marker != 0xCC;

    if (isSOF) {
      f.read(); // precision
      uint16_t hh = 0, ww = 0;
      if (!rd16(hh) || !rd16(ww)) { f.close(); return false; }
      *w = ww; *h = hh;
      f.close();
      return true;
    }

    f.seek(f.position() + (len - 2));
  }

  f.close();
  return false;
}

static int chooseJpgScaleFill(uint16_t jw, uint16_t jh, int sw, int sh) {
  int best = 1;
  for (int s : {1, 2, 4, 8}) {
    int dw = jw / s;
    int dh = jh / s;
    if (dw >= sw && dh >= sh) best = s;
  }
  return best;
}

static int chooseJpgScaleFit(uint16_t jw, uint16_t jh, int sw, int sh) {
  for (int s : {1, 2, 4, 8}) {
    int dw = jw / s;
    int dh = jh / s;
    if (dw <= sw && dh <= sh) return s;
  }
  return 8;
}

// -------------------- UIManager --------------------
void UIManager::begin(TFT_eSPI* tft, Adafruit_FT6206* touch) {
  tftPtr = tft;
  touchPtr = touch;

  // Let rails settle slightly on cold plug-in
  delay(200);

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  delay(20);

  tftPtr->init();
  delay(20);

  tftPtr->setRotation(1);
  themeInitDefault();
  tftPtr->fillScreen(theme.BG);

  UploadMode::init();

  // Backlight
  ledcAttach(TFT_BL, BL_PWM_FREQ, BL_PWM_RES);
  applyBacklight();

  // Give I2C device time before begin()
  delay(200);
  touchPtr->begin(40);

  // Mount LittleFS
  LittleFS.begin();
  delay(20);

  // Select filesystem (SD preferred)
  if (SD.cardType() != CARD_NONE) {
    gPhotoFS = &SD;
    gPhotoFSIsSD = true;
  } else {
    gPhotoFS = &LittleFS;
    gPhotoFSIsSD = false;
  }

  // JPEG decoder setup
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(UIManager::tftOutput);

  // Small delay before scanning directory (this matters on cold boot)
  delay(100);
  photoCount = rebuildPhotoList();
  currentPhotoIndex = 0;

  state = UIManager::State::SLIDESHOW;
  lastSlideMs = millis();
  showSlideshow(true);
}



void UIManager::update() {

    if (s_sleeping) {
    // Keep CPU running, but do not run overlay/slideshow logic
    return;
  }

  if (overlayActive && millis() > overlayUntilMs) {
    overlayActive = false;
    if (state == UIManager::State::SLIDESHOW) {
      renderCurrentPhoto();
    }
  }

    if (state == UIManager::State::UPLOAD) {
    UploadMode::loop();
    return; // don't do slideshow timing/overlay stuff while uploading
  }

  if (state == UIManager::State::SLIDESHOW && photoCount > 0) {
    uint32_t now = millis();
    if (now - lastSlideMs >= slideIntervalMs) {
      lastSlideMs = now;
      currentPhotoIndex = (currentPhotoIndex + 1) % photoCount;
      showSlideshow(true);
    }
  }
}

void UIManager::themeInitDefault() {
  // Default: charcoal + pastel yellow/purple accents
  theme.BG          = tftPtr->color565(24, 24, 30);
  theme.fill        = tftPtr->color565(44, 44, 52);
  theme.outline     = tftPtr->color565(70, 70, 82);
  theme.highlight   = tftPtr->color565(55, 55, 60);
  theme.shadow      = tftPtr->color565(26, 26, 30);
  theme.textCol     = tftPtr->color565(235,235,240);
  theme.textMutedCol= tftPtr->color565(180,180,190);
  theme.ACCENT_YELLOW = tftPtr->color565(245, 214, 130);
  theme.ACCENT_PURPLE = tftPtr->color565(178, 160, 210);
  theme.r = 10;
}

void UIManager::setTheme(const Theme& t) {
  theme = t;
}

void UIManager::drawHeader(const char* title) {
  // slightly lighter than before
  uint16_t hdr = tftPtr->color565(40, 40, 48);
  tftPtr->fillRect(0, 0, 320, 50, hdr);

  tftPtr->setTextColor(theme.ACCENT_YELLOW, hdr);
  tftPtr->setTextDatum(TC_DATUM);
  useSmoothFont(SmoothFont::TITLE);
  tftPtr->drawString(title, 160, 21);
  unloadSmoothFont();
}


void UIManager::showSettings() {
  state = UIManager::State::SETTINGS;
  overlayActive = false;
  touchInterruptFlag = false;
  overlayUntilMs = 0;

  tftPtr->fillScreen(theme.BG);
  drawHeader("Settings");

  drawButton(btnBrightness);
  drawButton(btnInterval);
  drawButton(btnAbout);
  drawButton(btnBack);
}

void UIManager::drawIntervalBarAndValue() {
  if (!tftPtr) return;

  const int barX = 40, barY = 90, barW = 240, barH = 18;
  static constexpr uint32_t MIN_MS = 5000;
  static constexpr uint32_t MAX_MS = 180000;

  uint32_t ms = slideIntervalMs;
  if (ms < MIN_MS) ms = MIN_MS;
  if (ms > MAX_MS) ms = MAX_MS;

  // Clear slider region
  tftPtr->fillRect(barX - 2, barY - 2, barW + 4, barH + 4, theme.BG);

  // Outline
  tftPtr->drawRoundRect(barX, barY, barW, barH, 6, theme.outline);

  // Fill based on ms within MIN..MAX
  float t = float(ms - MIN_MS) / float(MAX_MS - MIN_MS);
  int fillW = (int)lroundf(t * float(barW - 2));
  fillW = constrain(fillW, 0, barW - 2);

  tftPtr->fillRoundRect(barX + 1, barY + 1, fillW, barH - 2, 5, theme.ACCENT_YELLOW);

  // Clear value region
  tftPtr->fillRect(80, 125, 160, 40, theme.BG);

  uint32_t secs = (slideIntervalMs + 500) / 1000;

  tftPtr->setTextDatum(MC_DATUM);
  tftPtr->setTextColor(theme.textCol, theme.BG);
  useSmoothFont(SmoothFont::BUTTON);
  tftPtr->drawString(String(secs) + " s", 160, 140);
  unloadSmoothFont();
}


void UIManager::showInterval() {
  state = UIManager::State::INTERVAL;
  overlayActive = false;

  tftPtr->fillScreen(theme.BG);
  drawHeader("Slide Interval");

  // Clamp displayed interval into slider range
  uint32_t ms = slideIntervalMs;
  if (ms < INT_MIN_MS) ms = INT_MIN_MS;
  if (ms > INT_MAX_MS) ms = INT_MAX_MS;

  // Slider outline
  tftPtr->drawRoundRect(INT_BAR_X, INT_BAR_Y, INT_BAR_W, INT_BAR_H, 6, theme.outline);

  float t = (ms - INT_MIN_MS) / float(INT_MAX_MS - INT_MIN_MS);
  int fillW = (int)lroundf(t * (INT_BAR_W - 2));
  fillW = constrain(fillW, 0, INT_BAR_W - 2);

  tftPtr->fillRoundRect(INT_BAR_X + 1, INT_BAR_Y + 1, fillW, INT_BAR_H - 2, 5, theme.ACCENT_YELLOW);

  // Show value in seconds
  uint32_t secs = (slideIntervalMs + 500) / 1000;

  tftPtr->setTextDatum(MC_DATUM);
  tftPtr->setTextColor(theme.textCol, theme.BG);
  useSmoothFont(SmoothFont::BUTTON);
  tftPtr->drawString(String(secs) + " s", 160, 140);
  unloadSmoothFont();

  drawButton(btnBack);
  drawButton(btnIntDown);
  drawButton(btnIntUp);
}


void UIManager::showUpload() {
  state = UIManager::State::UPLOAD;
  overlayActive = false;

  tftPtr->fillScreen(theme.BG);
  drawHeader("Upload Photos");
  drawButton(btnBack);

  // Start AP + server
  bool ok = UploadMode::enter();

  tftPtr->setTextDatum(TL_DATUM);
  tftPtr->setTextColor(theme.textCol, theme.BG);
  useSmoothFont(SmoothFont::SMALL);

  int16_t x = 14;
  int16_t y = 54;

  if (!ok) {
    tftPtr->drawString("Wi-Fi AP failed.", x, y); y += 18;
    tftPtr->drawString("Restart frame and try again.", x, y);
    unloadSmoothFont();
    return;
  }

  // Instructions
  tftPtr->drawString("1) Connect phone to:", x, y); y += 18;
  tftPtr->drawString("   " + UploadMode::apSsid(), x, y); y += 18;

  tftPtr->drawString("2) Password:", x, y); y += 18;
  tftPtr->drawString("   " + UploadMode::apPass(), x, y); y += 18;

  tftPtr->drawString("3) Open browser to:", x, y); y += 18;
  tftPtr->drawString("   http://" + UploadMode::ipString(), x, y); y += 22;

  tftPtr->drawString("Upload JPGs -> /photos", x, y); y += 18;
  tftPtr->drawString("Tap Back when done.", x, y);

  unloadSmoothFont();
}

bool UIManager::isSleeping() { return s_sleeping; }

void UIManager::sleepNow() {
  if (s_sleeping) return;
  // Store current brightness
  s_prevBrightness = brightness;

  // Enter sleep and require finger-up before we allow wake
  s_sleeping = true;
  s_sleepWaitRelease = true;

  // Disarm transient UI stuff
  overlayActive = false;
  overlayUntilMs = 0;
  touchInterruptFlag = false;
  lastSlideMs = millis();

  setBrightness(0);   // or 5 if you want “almost off”
}



void UIManager::processTouch() {
  if (!tftPtr || !touchPtr) return;

  const bool touchedNow = touchPtr->touched();

  if (!touchedNow) {
    wasTouched = false;
    gButtonLatch = false;

    if (s_sleeping) s_sleepWaitRelease = false;   
    return;
  }

if (s_sleeping) {
  if (s_sleepWaitRelease) return;

  // Wake
  s_sleeping = false;

  uint8_t restore = (s_prevBrightness == 0) ? 120 : s_prevBrightness;
  setBrightness(restore);

  // Reset transient flags
  overlayActive = false;
  overlayUntilMs = 0;
  touchInterruptFlag = false;
  lastSlideMs = millis();

  // IMPORTANT: treat this wake touch as the slideshow tap that shows overlay
  if (state == UIManager::State::SLIDESHOW) {
    uint32_t nowMs = millis();
    overlayActive = true;
    overlayUntilMs = nowMs + 4000;

    drawButton(btnMenu);
    drawButton(btnSleep);
  }

  // Eat this touch so it doesn't also click Menu/Sleep immediately
  wasTouched = true;
  gButtonLatch = true;      // optional extra safety
  lastTouchMs = millis();   // optional: prevents immediate re-trigger
  return;
}



  // If we're on a slider screen, we WANT continuous updates while held down.
  const bool allowHoldRepeat =
      (state == UIManager::State::BRIGHTNESS) ||
      (state == UIManager::State::INTERVAL);

  if (!allowHoldRepeat) {
    if (wasTouched) return;   // normal screens: rising-edge only
    wasTouched = true;
  } else {
    // slider screens: do NOT latch out repeats
    wasTouched = true; // still mark as down so release logic works
  }

  TS_Point p = touchPtr->getPoint();
  if (p.z == 0) return;

  int16_t tx, ty;
  mapTouchToScreen(p.x, p.y, tx, ty);

  // Debounce: for slider screens use a much smaller debounce
  static uint32_t lastSliderMs = 0;
  uint32_t nowMs = millis();

  if (allowHoldRepeat) {
    if (nowMs - lastSliderMs < 30) return; // ~33 Hz update while holding
    lastSliderMs = nowMs;
  } else {
    if (nowMs - lastTouchMs < debounceMs) return;
    lastTouchMs = nowMs;
  }

  if (state == UIManager::State::SLIDESHOW) {

  if (overlayActive){
    if (hit(btnSleep, tx, ty)) { sleepNow(); return; }
    if (hit(btnMenu, tx, ty)) { showMenu(); return; }
  }

    // Any touch in slideshow shows overlay for 4s
  overlayActive = true;
  overlayUntilMs = nowMs + 4000;

  drawButton(btnMenu);
  drawButton(btnSleep);
  return;
}


  if (state == UIManager::State::MENU) {
    if (hit(btnResume, tx, ty))   { showSlideshow(true); return; }
    if (hit(btnSelect, tx, ty))   { showUpload(); return; }
    if (hit(btnSettings, tx, ty)) { showSettings();      return; }
    if (hit(btnBack, tx, ty))     { showSlideshow(true); return; }
    return;
  }

  if (state == UIManager::State::SETTINGS) {
    if (hit(btnBack, tx, ty))       { showMenu();      return; }
    if (hit(btnBrightness, tx, ty)) { showBrightness();return; }
    if (hit(btnInterval, tx, ty))   { showInterval();  return; }
    if (hit(btnAbout, tx, ty))      { showAbout();     return; }
    return;
  }

  if (state == UIManager::State::INTERVAL) {
  if (hit(btnBack, tx, ty)) { showSettings(); return; }

  const int barX = 40, barY = 90, barW = 240, barH = 18;
  static constexpr uint32_t MIN_MS = 5000;
  static constexpr uint32_t MAX_MS = 180000;

  if (tx >= barX && tx <= (barX + barW) && ty >= (barY - 12) && ty <= (barY + barH + 12)) {
    int rel = tx - (barX + 1);
    rel = constrain(rel, 0, barW - 2);

    uint32_t ms = map(rel, 0, barW - 2, MIN_MS, MAX_MS);
    ms = ((ms + 500) / 1000) * 1000; // snap to 1s

    setSlideIntervalMs(ms);
    drawIntervalBarAndValue();
    return;
  }

  // Fine adjust buttons (e.g., ±5s)
  if (!gButtonLatch && hit(btnIntDown, tx, ty)) {
    gButtonLatch = true;
    uint32_t ms = (slideIntervalMs >= 5000) ? (slideIntervalMs - 5000) : MIN_MS;
    if (ms < MIN_MS) ms = MIN_MS;
    setSlideIntervalMs(ms);
    drawIntervalBarAndValue();
    return;
  }
  if (!gButtonLatch && hit(btnIntUp, tx, ty)) {
    gButtonLatch = true;
    uint32_t ms = slideIntervalMs + 5000;
    if (ms > MAX_MS) ms = MAX_MS;
    setSlideIntervalMs(ms);
    drawIntervalBarAndValue();
    return;
  }

  return;
}


  if (state == UIManager::State::UPLOAD) {
  if (hit(btnBack, tx, ty)) {
    UploadMode::exit();

    // Refresh SD photo list (so new uploads show up immediately)
    if (SD.cardType() != CARD_NONE) {
      gPhotoFS = &SD;
      gPhotoFSIsSD = true;
    }
    photoCount = rebuildPhotoList();
    currentPhotoIndex = 0;

    showMenu();
    return;
  }
  return;
}

  if (state == UIManager::State::BRIGHTNESS) {
  if (hit(btnBack, tx, ty)) { showSettings(); return; }

  const int barX = 40, barY = 90, barW = 240, barH = 18;

  // Slider hitbox (extra padding vertically)
  if (tx >= barX && tx <= (barX + barW) && ty >= (barY - 12) && ty <= (barY + barH + 12)) {
    int rel = tx - (barX + 1);
    rel = constrain(rel, 0, barW - 2);

    int v = map(rel, 0, barW - 2, 5, 255);
    setBrightness((uint8_t)v);
    drawBrightnessBarAndValue();
    return;
  }

  // Fine adjust buttons
  if (!gButtonLatch && hit(btnBrDown, tx, ty)) {
    gButtonLatch = true;
    int v = (int)brightness - 10;
    if (v < 5) v = 5;
    setBrightness((uint8_t)v);
    drawBrightnessBarAndValue();
    return;
  }
  if (!gButtonLatch && hit(btnBrUp, tx, ty)) {
    gButtonLatch = true;
    int v = (int)brightness + 10;
    if (v > 255) v = 255;
    setBrightness((uint8_t)v);
    drawBrightnessBarAndValue();
    return;
  }

  return;
}




  if (state == UIManager::State::ABOUT) {
    if (hit(btnBack, tx, ty)) { showSettings(); return; }
    return;
  }
}


void UIManager::notifyTouchInterrupt() { touchInterruptFlag = true; }

uint8_t UIManager::getBrightness() { return brightness; }
void UIManager::setBrightness(uint8_t val) { brightness = val; applyBacklight(); }

uint32_t UIManager::getSlideIntervalMs() { return slideIntervalMs; }
void UIManager::setSlideIntervalMs(uint32_t ms) {
  // clamp 2s .. 300s
  if (ms < 2000) ms = 2000;
  if (ms > 300000) ms = 300000;
  slideIntervalMs = ms;
}

void UIManager::setDebounceMs(uint16_t ms) { debounceMs = ms; }

void UIManager::applyBacklight() { ledcWrite(TFT_BL, brightness); }

void UIManager::useFont(const GFXfont* f) {
  if (!tftPtr) return;
  if (f) tftPtr->setFreeFont(f);
  else tftPtr->setFreeFont(nullptr);
}

void UIManager::drawButton(const Button& b) {
  if (!tftPtr) return;

  const uint16_t BG            = theme.BG;
  const uint16_t fill          = theme.fill;
  const uint16_t outline       = theme.outline;
  const uint16_t highlight     = theme.highlight;
  const uint16_t shadow        = theme.shadow;
  const uint16_t textCol       = theme.textCol;
  const uint16_t textMutedCol  = theme.textMutedCol;
  const uint16_t ACCENT_YELLOW = theme.ACCENT_YELLOW;
  const uint16_t ACCENT_PURPLE = theme.ACCENT_PURPLE;
  const int r                  = theme.r;

  // Button body
  tftPtr->fillRoundRect(b.x, b.y, b.w, b.h, r, fill);
  tftPtr->drawRoundRect(b.x, b.y, b.w, b.h, r, outline);

  // Subtle "bevel" effect: top highlight + bottom shadow
  int inset = r + 3;  // tighter than radius
  tftPtr->drawFastHLine(b.x + inset, b.y + 2, b.w - 2*inset, highlight);
  tftPtr->drawFastHLine(b.x + inset, b.y + b.h - 3, b.w - 2*inset, shadow);
  tftPtr->drawRoundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, r - 1, theme.ACCENT_PURPLE);


  // Special case: hamburger menu icon (guaranteed to render, independent of fonts)
  if (&b == &btnMenu) {
    int16_t x0 = b.x + 10;
    int16_t w  = b.w - 20;
    int16_t y  = b.y + (b.h / 2) - 6;
    tftPtr->drawFastHLine(x0, y,     w, textCol);
    tftPtr->drawFastHLine(x0, y + 6, w, textCol);
    tftPtr->drawFastHLine(x0, y +12, w, textCol);
    return;
  }

  if (&b == &btnSleep) {
  // simple moon icon
  int cx = b.x + b.w/2;
  int cy = b.y + b.h/2;

  // outer circle
  tftPtr->fillCircle(cx, cy, 8, theme.textCol);
  // cutout to make crescent
  tftPtr->fillCircle(cx+4, cy-2, 8, theme.fill);
  return;
  }

  // Label
  useSmoothFont(SmoothFont::BUTTON);
  tftPtr->setTextDatum(MC_DATUM);
  tftPtr->setTextColor(textCol, fill);

  // FreeFonts can look slightly low even with MC_DATUM; tiny nudge helps
  int16_t cx = b.x + b.w/2;
  int16_t cy = b.y + b.h/2;
  cy -= (tftPtr->fontHeight() / 6);

  tftPtr->drawString(b.label, cx, cy);
  unloadSmoothFont();
}


bool UIManager::hit(const Button& b, int16_t x, int16_t y) {
  return (x >= b.x && x <= (b.x + b.w) && y >= b.y && y <= (b.y + b.h));
}

void UIManager::showSlideshow(bool forceRedraw) {
  state = UIManager::State::SLIDESHOW;

  // HARD reset transient UI state
  touchInterruptFlag = false;
  overlayActive = false;
  overlayUntilMs = 0;

  // Reset slide timer so it doesn't do weird timing after UI transitions
  lastSlideMs = millis();

  if (forceRedraw && !s_sleeping) renderCurrentPhoto();
}


void UIManager::showMenu() {
  state = UIManager::State::MENU;
  overlayActive = false;
  touchInterruptFlag = false;
  overlayUntilMs = 0;

  tftPtr->fillScreen(theme.BG);
  drawHeader("Menu");

  drawButton(btnResume);
  drawButton(btnSelect);
  drawButton(btnSettings);
  drawButton(btnBack);
}

void UIManager::drawBrightnessBarAndValue() {
  if (!tftPtr) return;

  const int barX = 40, barY = 90, barW = 240, barH = 18;

  // Clear slider region
  tftPtr->fillRect(barX - 2, barY - 2, barW + 4, barH + 4, theme.BG);

  // Outline
  tftPtr->drawRoundRect(barX, barY, barW, barH, 6, theme.outline);

  // Fill
  int fillW = (int)((brightness / 255.0f) * (barW - 2));
  fillW = constrain(fillW, 0, barW - 2);
  tftPtr->fillRoundRect(barX + 1, barY + 1, fillW, barH - 2, 5, theme.ACCENT_YELLOW);

  // Clear value region
  tftPtr->fillRect(90, 125, 140, 40, theme.BG);

  // Percent
  int pct = (int)lroundf((brightness / 255.0f) * 100.0f);
  pct = constrain(pct, 0, 100);

  tftPtr->setTextDatum(MC_DATUM);
  tftPtr->setTextColor(theme.textCol, theme.BG);
  useSmoothFont(SmoothFont::BUTTON);
  tftPtr->drawString(String(pct) + "%", 160, 140);
  unloadSmoothFont();
}



void UIManager::showBrightness() {
  state = UIManager::State::BRIGHTNESS;
  overlayActive = false;

  tftPtr->fillScreen(theme.BG);
  drawHeader("Brightness");

  // Slider outline
  tftPtr->drawRoundRect(BR_BAR_X, BR_BAR_Y, BR_BAR_W, BR_BAR_H, 6, theme.outline);

  // Fill based on brightness
  int fillW = (int)((brightness / 255.0f) * (BR_BAR_W - 2));
  if (fillW < 0) fillW = 0;
  if (fillW > (BR_BAR_W - 2)) fillW = (BR_BAR_W - 2);
  tftPtr->fillRoundRect(BR_BAR_X + 1, BR_BAR_Y + 1, fillW, BR_BAR_H - 2, 5, theme.ACCENT_YELLOW);

  // Show percent
  int pct = (int)lroundf((brightness / 255.0f) * 100.0f);
  pct = constrain(pct, 0, 100);

  tftPtr->setTextDatum(MC_DATUM);
  tftPtr->setTextColor(theme.textCol, theme.BG);
  useSmoothFont(SmoothFont::BUTTON);
  tftPtr->drawString(String(pct) + "%", 160, 140);
  unloadSmoothFont();

  drawButton(btnBack);
  drawButton(btnBrDown);
  drawButton(btnBrUp);
}


void UIManager::showAbout() {
  state = UIManager::State::ABOUT;
  tftPtr->fillScreen(theme.BG);

  drawHeader("About");

  tftPtr->setTextDatum(TL_DATUM);
  useSmoothFont(SmoothFont::SMALL);
  int16_t x = 14;
  int16_t y = 54;
  tftPtr->drawString("Made by ", x, y); y += 22;
  tftPtr->drawString("For ", x, y); y += 22;
  unloadSmoothFont();

  drawButton(btnBack);
}

void UIManager::renderCurrentPhoto() {
  if (s_sleeping) return;
  if (!tftPtr) return;

  const int16_t sw = tftPtr->width();
  const int16_t sh = tftPtr->height();

  tftPtr->fillScreen(theme.BG);

  if (photoCount <= 0 || gPhotoPaths.empty()) {
    tftPtr->setTextColor(theme.ACCENT_YELLOW, theme.BG);
    tftPtr->setTextDatum(MC_DATUM);
    tftPtr->drawString("No photos", sw / 2, sh / 2);
    return;
  }

  if (currentPhotoIndex < 0) currentPhotoIndex = 0;
  if (currentPhotoIndex >= photoCount) currentPhotoIndex = photoCount - 1;

  const String& path = gPhotoPaths[currentPhotoIndex];

  uint16_t jw = 0, jh = 0;
  if (!getJpegSizeFromFS(*gPhotoFS, path.c_str(), &jw, &jh)) {
    Serial.printf("JPEG header size read failed: %s\n", path.c_str());
    tftPtr->setTextColor(theme.ACCENT_YELLOW, theme.BG);
    tftPtr->setTextDatum(MC_DATUM);
    tftPtr->drawString("JPEG size read fail", sw / 2, sh / 2);
    return;
  }

  int jpgScale = (fitMode == FitMode::FILL)
      ? chooseJpgScaleFill(jw, jh, sw, sh)
      : chooseJpgScaleFit(jw, jh, sw, sh);

  TJpgDec.setJpgScale(jpgScale);

  int16_t dw = (int16_t)(jw / jpgScale);
  int16_t dh = (int16_t)(jh / jpgScale);

  int16_t x = (sw - dw) / 2;
  int16_t y = (sh - dh) / 2;

  Serial.printf("Draw %s jw=%u jh=%u scale=%d x=%d y=%d (FS=%s)\n",
                path.c_str(), jw, jh, jpgScale, x, y, gPhotoFSIsSD ? "SD" : "LittleFS");

  // Direct draw (stable)
  TJpgDec.drawFsJpg(x, y, path.c_str(), *gPhotoFS);
}

bool UIManager::tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (tftPtr) tftPtr->pushImage(x, y, w, h, bitmap);
  return true;
}

void UIManager::mapTouchToScreen(int16_t rawX, int16_t rawY, int16_t& sx, int16_t& sy) {
  sx = map(rawY, 0, 320, 0, tftPtr->width());
  sy = map(rawX, 0, 240, tftPtr->height(), 0);
}

void UIManager::useSmoothFont(SmoothFont f) {
  if (!tftPtr) return;

  switch (f) {
    case SmoothFont::SMALL:  tftPtr->loadFont("/fonts/Inter10", LittleFS); break;
    case SmoothFont::BUTTON: tftPtr->loadFont("/fonts/Inter14", LittleFS); break;
    case SmoothFont::TITLE:  tftPtr->loadFont("/fonts/Inter16", LittleFS); break;
  }
}

void UIManager::unloadSmoothFont() {
  if (tftPtr) tftPtr->unloadFont();
}



