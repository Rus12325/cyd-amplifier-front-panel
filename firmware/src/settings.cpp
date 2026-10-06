#include "settings.h"

#include <Preferences.h>

Settings gSettings;

namespace {

Preferences prefs;

const char* kNamespace = CFG_NVS_NAMESPACE;

// The stored blob carries its own version so a layout change does not load
// nonsense into the new fields.
struct Stored {
  uint16_t version;
  SettingsData data;
};

constexpr uint16_t kVersion = 4;

}  // namespace

void Settings::begin() {
  prefs.begin(kNamespace, false);
  Stored stored;
  const size_t got = prefs.getBytes("state", &stored, sizeof(stored));
  if (got == sizeof(stored) && stored.version == kVersion) {
    data_ = stored.data;
    loaded_ = true;
  } else {
    data_ = SettingsData();
    loaded_ = false;
  }
}

void Settings::save() {
  Stored stored;
  stored.version = kVersion;
  stored.data = data_;
  prefs.putBytes("state", &stored, sizeof(stored));
}
