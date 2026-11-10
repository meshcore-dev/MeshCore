#pragma once

#include <stdint.h>
#include <stddef.h>
#include "TrackerPrefs.h"

class ProvisioningManager {
public:
    static const uint32_t PROVISION_WINDOW_MS = 60000;  // 60 seconds
    static const uint32_t REBOOT_HOLD_MS      = 5000;   // 5s button hold to re-provision

    void begin(unsigned long now_ms);
    void loop(unsigned long now_ms);
    bool isWindowOpen() const { return _window_open; }
    bool isProvisioned() const { return _provisioned; }
    NodeMode getMode() const { return _pending_mode; }

    // Called by NFC handler when valid NDEF payload received
    void onNFCProvision(const uint8_t* payload, size_t len);

    // Called by BLE handler when valid provision packet received
    void onBLEProvision(const uint8_t* payload, size_t len);

    // Called by main loop when button held 5s
    void requestReopen();

    // Parsed result (valid after isProvisioned() == true)
    TrackerPrefs result;

private:
    bool          _window_open;
    bool          _provisioned;
    unsigned long _window_start_ms;
    NodeMode      _pending_mode;

    // LED state
    enum LEDPattern {
        LED_PAT_NONE = 0,
        LED_PAT_SLOW_PULSE,      // provisioning window open: 500/500
        LED_PAT_TRIPLE_BLINK,    // data received, parsing
        LED_PAT_TRACKER_OK,      // 3 long blinks (tracker)
        LED_PAT_RELAY_TIMEOUT,   // 1 long blink (relay)
        LED_PAT_REPROVISION,     // 5 rapid blinks
    };

    LEDPattern    _led_pattern;
    unsigned long _led_next_ms;
    int           _led_blink_count;
    bool          _led_state;

    bool parseProvisionPayload(const uint8_t* payload, size_t len);
    void closeWindow(NodeMode mode);
    void indicateLED(unsigned long now_ms);

    // Internal LED helpers
    void setLEDPattern(LEDPattern pat, unsigned long now_ms);
    void ledOn();
    void ledOff();

    // Minimal base64 decoder — no dynamic allocation
    static int base64Decode(const char* src, uint8_t* dst, size_t dst_max);
};
