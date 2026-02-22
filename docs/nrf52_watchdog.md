# Overview

The nRF52 hardware watchdog timer (WDT) provides automatic recovery from firmware hangs or crashes. When enabled, if the firmware fails to "feed" the watchdog within the timeout period, the device will automatically reset.

## Parameters

| Parameter | Value |
|-----------|-------|
| Timeout | 30 seconds |
| Sleep behavior | Pauses during sleep mode |
| Halt behavior | Pauses during halt state |
| Control | Compile-time via `NRF52_WATCHDOG` build flag + runtime pref `wdt_enabled` (CLI `set wdt on/off`) |

The implementation uses the nRF52840 WDT peripheral with the following configuration:

| Register | Value | Description |
|----------|-------|-------------|
| `CONFIG.SLEEP` | Pause | WDT pauses when CPU enters sleep |
| `CONFIG.HALT` | Pause | WDT pauses during debug halt |
| `CRV` | 983040 | Counter reload value (30s × 32768 Hz) |
| `RREN` | RR0 enabled | Uses reload request register 0 |
| `RR[0]` | 0x6E524635 | Magic value to feed watchdog |

## Usage

1. Watchdog is **disabled by default** - enable via `set wdt on` (reboot required to turn on/off)
2. Application code checks `prefs.wdt_enabled` after loading prefs and calls `board.initWatchdog()` if enabled
3. The main loop must call `board.tick()` regularly to feed the watchdog
4. If `board.tick()` is not called within 30 seconds, the device resets
5. The watchdog pauses during low-power sleep modes (won't reset during intended sleep)

**Important**: Once the watchdog is started, it cannot be stopped during runtime. A power cycle is required to apply changes made via `set wdt on/off`.

## Enabling Watchdog for a Board Variant

Watchdog is not compiled by default. To enable it for a board variant, add the `NRF52_WATCHDOG` flag to the variant's `platformio.ini`. The `wdt_enabled` preference (set via `set wdt on/off`) controls whether it starts on boot (disabled by default):

```ini
    [env:your_variant]
extends = nrf52_base
build_flags = ${nrf52_base.build_flags}
    -D NRF52_WATCHDOG
    # ... other flags
```

## Firmware Integration

Currently, Watchdog is only implemented in the Repeater firmware. To add to other firmware types, perform the below steps. 

After loading prefs in the `begin()` method, start the watchdog if enabled:

```cpp
_cli.loadPrefs(_fs);

#ifdef NRF52_WATCHDOG
  if (_prefs.wdt_enabled) {
    board.initWatchdog();
  }
#endif
```

Ensure the main `loop()` function calls `board.tick()` to feed the watchdog:

```cpp
void loop() {
    the_mesh.loop();
    sensors.loop();
#ifdef DISPLAY_CLASS
    ui_task.loop();
#endif
    rtc_clock.tick();
    board.tick();  // Feed the watchdog
}
```

## CLI Commands

When `NRF52_WATCHDOG` is defined, the following CLI commands are available:

| Command | Description |
|---------|-------------|
| `get wdt` | Returns `Enabled/Disabled, running/not running` |
| `set wdt on/off` | Enable/disable watchdog on next reboot |
