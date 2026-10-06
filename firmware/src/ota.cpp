// ota.cpp - ArduinoOTA implementation for the CYD amp panel.
#include "ota.h"

#if defined(OTA_ENABLE) && OTA_ENABLE

#include <ArduinoOTA.h>
#include <WiFi.h>

namespace {
bool gBusy = false;
char gStatus[32] = "OTA idle";
}

void otaBegin() {
  const char* ssid = OTA_WIFI_SSID;
  const char* pass = OTA_WIFI_PASS;
  if (!ssid || ssid[0] == '\0') {
    Serial.println("[ota] no SSID configured, skipping Wi-Fi");
    return;
  }
  Serial.printf("[ota] connecting to %s ...\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  // Wait up to 10 s for an IP – non-blocking alternative: just start OTA and
  // let it connect in the background, but ArduinoOTA needs an IP first.
  const uint32_t deadline = millis() + 10000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ota] Wi-Fi connect timed out");
    snprintf(gStatus, sizeof(gStatus), "OTA no wifi");
    return;
  }
  Serial.printf("[ota] IP %s\n", WiFi.localIP().toString().c_str());
  snprintf(gStatus, sizeof(gStatus), "OTA ready");

  ArduinoOTA.setHostname("amp-panel");
  ArduinoOTA.setPort(3232);

  ArduinoOTA.onStart([]() {
    gBusy = true;
    Serial.println("[ota] update start");
    snprintf(gStatus, sizeof(gStatus), "OTA updating");
  });
  ArduinoOTA.onEnd([]() {
    gBusy = false;
    Serial.println("[ota] update done, rebooting");
    snprintf(gStatus, sizeof(gStatus), "OTA done");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    const int pct = (int)(100UL * done / total);
    snprintf(gStatus, sizeof(gStatus), "OTA %d%%", pct);
  });
  ArduinoOTA.onError([](ota_error_t e) {
    gBusy = false;
    Serial.printf("[ota] error %u\n", e);
    snprintf(gStatus, sizeof(gStatus), "OTA err %u", e);
  });

  ArduinoOTA.begin();
  Serial.println("[ota] listening");
}

void otaLoop() {
  ArduinoOTA.handle();
}

bool otaBusy() { return gBusy; }
const char* otaStatus() { return gStatus; }

#endif  // OTA_ENABLE
