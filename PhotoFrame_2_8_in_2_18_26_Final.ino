#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Fonts/Custom/Roboto_Regular12pt7b.h>
#include <Fonts/Custom/Roboto_Bold16pt7b.h>
#include <Fonts/Custom/Roboto_Bold24pt7b.h>
#include <Adafruit_FT6206.h>
#include "UIManager_updated.h"
#include "Pins.h"
#include <SD.h>
#include <SPI.h>


TFT_eSPI tft;
Adafruit_FT6206 touch;
SPIClass sdSPI(FSPI);

void IRAM_ATTR onTouchInt() {
  UIManager::notifyTouchInterrupt();
}

void setup() {
  Serial.begin(115200);
  delay(1200);         // let 5V/3V3 settle on cold plug-in

  // SD init (10MHz ok)
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS_PIN);
  delay(150);
  SD.begin(SD_CS_PIN, sdSPI, 10000000);

  UIManager::begin(&tft, &touch);
  UIManager::setDebounceMs(180);
}



void loop() {
  UIManager::processTouch();
  UIManager::update();
  delay(5);
}
