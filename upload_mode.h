#pragma once
#include <Arduino.h>

namespace UploadMode {
  void init();                 // optional
  bool enter();                // start AP + server
  void exit();                 // stop server + WiFi off
  void loop();                 // server.handleClient()
  bool isActive();
  uint32_t uploadedCount();

  // Display helpers for UI
  String apSsid();
  String apPass();
  String ipString();
}
