#include "hid.h"

#if CYD_BLE_HID

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEUtils.h>

namespace hid {
namespace {

// ------------------------------------------------------------------ GATT ids
constexpr uint16_t kUuidHidService = 0x1812;
constexpr uint16_t kUuidHidInfo = 0x2A4A;
constexpr uint16_t kUuidReportMap = 0x2A4B;
constexpr uint16_t kUuidControlPoint = 0x2A4C;
constexpr uint16_t kUuidReport = 0x2A4D;
constexpr uint16_t kUuidProtocolMode = 0x2A4E;
constexpr uint16_t kUuidReportRef = 0x2908;

// One 1-byte input report with the three usages a media key sends, plus five
// bits of padding to fill the byte.  Windows (and Linux and macOS) know all
// three by heart, so nothing else has to be described.
const uint8_t kReportMapData[] = {
    0x05, 0x0C,        // Usage Page (Consumer Devices)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x03,        //   Report Count (3)
    0x09, 0xE9,        //   Usage (Volume Increment)
    0x09, 0xEA,        //   Usage (Volume Decrement)
    0x09, 0xE2,        //   Usage (Mute)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x75, 0x05,        //   Report Size (5) - padding
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Constant)
    0xC0,              // End Collection
};

// The bits of that report, in the order the descriptor lists them.
constexpr uint8_t kBitVolumeUp = 0x01;
constexpr uint8_t kBitVolumeDown = 0x02;
constexpr uint8_t kBitMute = 0x04;

// ------------------------------------------------------------------- state
uint8_t gQueue[CFG_HID_QUEUE];
uint8_t gHead = 0;
uint8_t gTail = 0;
uint8_t gDown = 0;             // usage bits in the report currently held down
uint8_t gRetry = 0;
uint32_t gNextAt = 0;
bool gConnected = false;
bool gAdvPending = false;
uint32_t gAdvAt = 0;
BLECharacteristic* gReport = nullptr;
// The report's client-characteristic-configuration descriptor (CCCD).  The
// bundled BLE stack's notify() returns nothing, so this is how the loop learns
// whether the PC has actually subscribed - keys sent before that are lost.
BLE2902* gCccd = nullptr;

int gVolume = 50;              // estimate, percent
bool gMuted = false;
uint32_t gSent = 0;
uint32_t gDropped = 0;

void send(uint8_t value) {
  if (!gReport) return;
  gReport->setValue(&value, 1);
  gReport->notify();
}

// Queues one step.  With no link there is nothing to send and the step is
// dropped rather than replayed the moment the PC connects: a stale burst of
// volume changes is worse than a key that did nothing.
void push(uint8_t key) {
  if (!gConnected) {
    ++gDropped;
    return;
  }
  const uint8_t next = (uint8_t)((gTail + 1) % CFG_HID_QUEUE);
  if (next == gHead) {
    gHead = (uint8_t)((gHead + 1) % CFG_HID_QUEUE);   // full: oldest step goes
    ++gDropped;
  }
  gQueue[gTail] = key;
  gTail = next;
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    (void)server;
    gConnected = true;
    gHead = gTail = 0;              // presses from before the link are stale
    gDown = 0;
    gRetry = 0;
    Serial.println("[hid] PC linked - the volume keys are live");
  }

  void onDisconnect(BLEServer* server) override {
    (void)server;
    gConnected = false;
    gDown = 0;
    gRetry = 0;
    // Advertising cannot be started from inside this callback safely, so the
    // loop does it a moment later.
    gAdvPending = true;
    gAdvAt = millis() + 300;
    Serial.println("[hid] PC unlinked - advertising again");
  }
};

class SecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t passKey) override { (void)passKey; }
  bool onSecurityRequest() override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
    Serial.printf("[hid] pairing %s\n",
                  cmpl.success ? "complete" : "FAILED - remove the device on the "
                                               "PC and pair again");
  }
  bool onConfirmPIN(uint32_t pin) override {
    (void)pin;
    return true;
  }
};

}  // namespace

// ------------------------------------------------------------------ public
void begin() {
  BLEDevice::init(CFG_HID_NAME);
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks());

  // "Just works" pairing: no display and no keypad to agree on a PIN with, and
  // nothing secret is being typed - the device only sends three volume keys.
  BLESecurity* sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_BOND);
  sec->setCapability(ESP_IO_CAP_NONE);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(BLEUUID(kUuidHidService));

  // HID Information: bcdHID 1.11, no country code, "normally connectable".
  BLECharacteristic* info = service->createCharacteristic(
      BLEUUID(kUuidHidInfo), BLECharacteristic::PROPERTY_READ);
  uint8_t infoData[4] = {0x11, 0x01, 0x00, 0x02};
  info->setValue(infoData, sizeof(infoData));

  BLECharacteristic* map = service->createCharacteristic(
      BLEUUID(kUuidReportMap), BLECharacteristic::PROPERTY_READ);
  map->setValue((uint8_t*)kReportMapData, sizeof(kReportMapData));

  // Written when the host suspends or resumes the device; there is nothing to
  // suspend, so the value is only stored.
  BLECharacteristic* control = service->createCharacteristic(
      BLEUUID(kUuidControlPoint), BLECharacteristic::PROPERTY_WRITE_NR);
  uint8_t controlData = 0;
  control->setValue(&controlData, 1);

  BLECharacteristic* mode = service->createCharacteristic(
      BLEUUID(kUuidProtocolMode),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE_NR);
  uint8_t reportMode = 1;                  // 1 = report protocol
  mode->setValue(&reportMode, 1);

  gReport = service->createCharacteristic(
      BLEUUID(kUuidReport),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  uint8_t idle = 0;
  gReport->setValue(&idle, 1);
  gCccd = new BLE2902();                   // notify subscription (CCCD)
  gReport->addDescriptor(gCccd);
  uint8_t reportRef[2] = {0x01, 0x01};     // report id 1, input report
  BLEDescriptor* refDesc = new BLEDescriptor(BLEUUID(kUuidReportRef), 2);
  refDesc->setValue(reportRef, sizeof(reportRef));
  gReport->addDescriptor(refDesc);

  service->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLEUUID(kUuidHidService));
  adv->setAppearance(0x03C0);              // "HID generic"
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);              // 7.5..22.5 ms: Windows finds it fast
  adv->setMaxPreferred(0x12);
  adv->start();

  Serial.printf("[hid] advertising as \"%s\" - pair the PC with it once\n",
                CFG_HID_NAME);
}

void loop(uint32_t now) {
  if (gAdvPending && now >= gAdvAt) {
    gAdvPending = false;
    BLEDevice::getAdvertising()->start();
  }
  if (!gConnected || !gReport) return;
  if (now < gNextAt) return;

  if (gDown) {
    send(0);                        // key up
    gDown = 0;
    gNextAt = now + CFG_HID_GAP_MS;
    return;
  }

  if (gHead == gTail) return;       // nothing queued
  if (!gCccd || !gCccd->getNotifications()) {
    // The host links first and subscribes to the report a moment later; hold
    // the step for a few tries instead of throwing it away.
    if (++gRetry >= 10) {
      gHead = (uint8_t)((gHead + 1) % CFG_HID_QUEUE);
      gRetry = 0;
      ++gDropped;
    }
    gNextAt = now + 100;
    return;
  }

  uint8_t key = gQueue[gHead];
  send(key);

  gHead = (uint8_t)((gHead + 1) % CFG_HID_QUEUE);
  gRetry = 0;
  gDown = key;
  if (key & kBitVolumeUp) gVolume = min(100, gVolume + CFG_HID_STEP_PCT);
  if (key & kBitVolumeDown) gVolume = max(0, gVolume - CFG_HID_STEP_PCT);
  if (key & kBitMute) gMuted = !gMuted;
  ++gSent;
  gNextAt = now + CFG_HID_PRESS_MS;
}

bool ready() { return gReport != nullptr; }
bool connected() { return gConnected; }

const char* stateText() {
  if (!gReport) return "OFF";
  return gConnected ? "LINKED" : "PAIRING";
}

void volumeUp(uint8_t steps) {
  if (steps > CFG_HID_QUEUE) steps = CFG_HID_QUEUE;
  while (steps--) push(kBitVolumeUp);
}

void volumeDown(uint8_t steps) {
  if (steps > CFG_HID_QUEUE) steps = CFG_HID_QUEUE;
  while (steps--) push(kBitVolumeDown);
}

void muteToggle() { push(kBitMute); }

int volumeEstimate() { return gVolume; }
bool muteEstimate() { return gMuted; }
uint32_t sentSteps() { return gSent; }
uint32_t droppedSteps() { return gDropped; }

}  // namespace hid
#endif  // CYD_BLE_HID
