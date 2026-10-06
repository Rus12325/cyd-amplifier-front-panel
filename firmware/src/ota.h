// ota.h - ArduinoOTA over Wi-Fi for the CYD amp panel.
//
// Usage:
//   otaBegin("MySSID", "password");   // call once in setup(), non-blocking
//   otaLoop();                        // call every loop() tick
//
// The AP credentials come from the build flags OTA_WIFI_SSID / OTA_WIFI_PASS.
// If neither is defined the module compiles to empty stubs so other builds are
// unaffected.  When an OTA transfer is in progress otaBusy() returns true and
// the main loop should avoid touching the display or relays.
#pragma once

#include <Arduino.h>

#ifndef OTA_WIFI_SSID
#define OTA_WIFI_SSID ""
#endif
#ifndef OTA_WIFI_PASS
#define OTA_WIFI_PASS ""
#endif

#if defined(OTA_ENABLE) && OTA_ENABLE

void otaBegin();
void otaLoop();
bool otaBusy();
const char* otaStatus();  // short human-readable state for the status bar

#else

inline void otaBegin() {}
inline void otaLoop()  {}
inline bool otaBusy()  { return false; }
inline const char* otaStatus() { return ""; }

#endif  // OTA_ENABLE
