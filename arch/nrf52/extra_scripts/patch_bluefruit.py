"""
Bluefruit BLE Patch Script

Patches Bluefruit library to fix semaphore leak bug that causes device lockup
when BLE central disconnects unexpectedly (e.g., going out of range, supervision timeout).

Patches applied:
1. BLEConnection.h: Add _hvn_qsize member to track semaphore queue size
2. BLEConnection.cpp: Store hvn_qsize and restore semaphore on disconnect
3. bluefruit.h: Add bluefruit_blinky_cb as friend so it can access _led_conn
4. bluefruit.cpp: Guard LED toggle in blinky callback with _led_conn check

Bug description:
- When a BLE central disconnects unexpectedly (reason=8 supervision timeout),
  the BLE_GATTS_EVT_HVN_TX_COMPLETE event may never fire
- This leaves the _hvn_sem counting semaphore in a decremented state
- Since BLEConnection objects are reused (destructor never called), the
  semaphore count is never restored
- Eventually all semaphore counts are exhausted and notify() blocks/fails

"""

from pathlib import Path

Import("env")  # pylint: disable=undefined-variable


def _patch_ble_connection_header(source: Path) -> bool:
    """
    Add _hvn_qsize member variable to BLEConnection class.
    
    This is needed to restore the semaphore to its correct count on disconnect.
    
    Returns True if patch was applied or already applied, False on error.
    """
    try:
        content = source.read_text()
        
        # Check if already patched
        if "_hvn_qsize" in content:
            return True  # Already patched
        
        # Find the location to insert - after _phy declaration
        original_pattern = '''    uint8_t  _phy;

    uint8_t  _role;'''
        
        patched_pattern = '''    uint8_t  _phy;
    uint8_t  _hvn_qsize;

    uint8_t  _role;'''
        
        if original_pattern not in content:
            print("Bluefruit patch: WARNING - BLEConnection.h pattern not found")
            return False
        
        content = content.replace(original_pattern, patched_pattern)
        source.write_text(content)
        
        # Verify
        if "_hvn_qsize" not in source.read_text():
            return False
        
        return True
    except Exception as e:
        print(f"Bluefruit patch: ERROR patching BLEConnection.h: {e}")
        return False


def _patch_ble_connection_source(source: Path) -> bool:
    """
    Patch BLEConnection.cpp to:
    1. Store hvn_qsize in constructor
    2. Restore _hvn_sem semaphore to full count on disconnect
    
    Returns True if patch was applied or already applied, False on error.
    """
    try:
        content = source.read_text()
        
        # Check if already patched (look for the restore loop)
        if "uxSemaphoreGetCount(_hvn_sem)" in content:
            return True  # Already patched
        
        # Patch 1: Store queue size in constructor
        constructor_original = '''  _hvn_sem   = xSemaphoreCreateCounting(hvn_qsize, hvn_qsize);'''
        
        constructor_patched = '''  _hvn_qsize = hvn_qsize;
  _hvn_sem   = xSemaphoreCreateCounting(hvn_qsize, hvn_qsize);'''
        
        if constructor_original not in content:
            print("Bluefruit patch: WARNING - BLEConnection.cpp constructor pattern not found")
            return False
        
        content = content.replace(constructor_original, constructor_patched)
        
        # Patch 2: Restore semaphore on disconnect
        disconnect_original = '''    case BLE_GAP_EVT_DISCONNECTED:
      // mark as disconnected
      _connected = false;
    break;'''
        
        disconnect_patched = '''    case BLE_GAP_EVT_DISCONNECTED:
      // Restore notification semaphore to full count
      // This fixes lockup when disconnect occurs with notifications in flight
      while (uxSemaphoreGetCount(_hvn_sem) < _hvn_qsize) {
        xSemaphoreGive(_hvn_sem);
      }
      // Release indication semaphore if waiting
      if (_hvc_sem) {
        _hvc_received = false;
        xSemaphoreGive(_hvc_sem);
      }
      // mark as disconnected
      _connected = false;
    break;'''
        
        if disconnect_original not in content:
            print("Bluefruit patch: WARNING - BLEConnection.cpp disconnect pattern not found")
            return False
        
        content = content.replace(disconnect_original, disconnect_patched)
        source.write_text(content)
        
        # Verify
        verify_content = source.read_text()
        if "uxSemaphoreGetCount(_hvn_sem)" not in verify_content:
            return False
        if "_hvn_qsize = hvn_qsize" not in verify_content:
            return False
        
        return True
    except Exception as e:
        print(f"Bluefruit patch: ERROR patching BLEConnection.cpp: {e}")
        return False


def _patch_bluefruit_header_led(source: Path) -> bool:
    """
    Add bluefruit_blinky_cb as a friend function so it can access _led_conn.

    Without this, the blink timer callback toggles LED_BLUE unconditionally,
    ignoring autoConnLed(false).

    Returns True if patch was applied or already applied, False on error.
    """
    try:
        content = source.read_text()

        # Check if already patched
        if "bluefruit_blinky_cb" in content:
            return True  # Already patched

        original_pattern = '''    friend void adafruit_soc_task(void* arg);
    friend class BLECentral;'''

        patched_pattern = '''    friend void adafruit_soc_task(void* arg);
    friend void bluefruit_blinky_cb(TimerHandle_t xTimer);
    friend class BLECentral;'''

        if original_pattern not in content:
            print("Bluefruit patch: WARNING - bluefruit.h friend pattern not found")
            return False

        content = content.replace(original_pattern, patched_pattern)
        source.write_text(content)

        if "bluefruit_blinky_cb" not in source.read_text():
            return False

        return True
    except Exception as e:
        print(f"Bluefruit patch: ERROR patching bluefruit.h: {e}")
        return False


def _patch_bluefruit_source_led(source: Path) -> bool:
    """
    Guard the LED toggle in bluefruit_blinky_cb with a _led_conn check.

    The blink timer can be inadvertently started by Advertising.start() ->
    setConnLedInterval() -> xTimerChangePeriod() (FreeRTOS starts dormant
    timers as a side effect). Without this guard, the LED toggles even when
    autoConnLed(false) has been called.

    Returns True if patch was applied or already applied, False on error.
    """
    try:
        content = source.read_text()

        # Check if already patched
        if "Bluefruit._led_conn" in content:
            return True  # Already patched

        original_pattern = '''static void bluefruit_blinky_cb( TimerHandle_t xTimer )
{
  (void) xTimer;
  digitalToggle(LED_BLUE);'''

        patched_pattern = '''void bluefruit_blinky_cb( TimerHandle_t xTimer )
{
  (void) xTimer;
  if ( Bluefruit._led_conn ) digitalToggle(LED_BLUE);'''

        if original_pattern not in content:
            print("Bluefruit patch: WARNING - bluefruit.cpp blinky_cb pattern not found")
            return False

        content = content.replace(original_pattern, patched_pattern)
        source.write_text(content)

        if "Bluefruit._led_conn" not in source.read_text():
            return False

        return True
    except Exception as e:
        print(f"Bluefruit patch: ERROR patching bluefruit.cpp: {e}")
        return False


def _apply_bluefruit_patches(target, source, env):  # pylint: disable=unused-argument
    framework_path = env.get("PLATFORMFW_DIR")
    if not framework_path:
        framework_path = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52")

    if not framework_path:
        print("Bluefruit patch: ERROR - framework directory not found")
        env.Exit(1)
        return

    framework_dir = Path(framework_path)
    bluefruit_lib = framework_dir / "libraries" / "Bluefruit52Lib" / "src"
    patch_failed = False
    
    # Patch BLEConnection.h
    conn_header = bluefruit_lib / "BLEConnection.h"
    if conn_header.exists():
        before = conn_header.read_text()
        success = _patch_ble_connection_header(conn_header)
        after = conn_header.read_text()
        
        if success:
            if before != after:
                print("Bluefruit patch: OK - Applied BLEConnection.h fix (added _hvn_qsize member)")
            else:
                print("Bluefruit patch: OK - BLEConnection.h already patched")
        else:
            print("Bluefruit patch: FAILED - BLEConnection.h")
            patch_failed = True
    else:
        print(f"Bluefruit patch: ERROR - BLEConnection.h not found at {conn_header}")
        patch_failed = True
    
    # Patch BLEConnection.cpp
    conn_source = bluefruit_lib / "BLEConnection.cpp"
    if conn_source.exists():
        before = conn_source.read_text()
        success = _patch_ble_connection_source(conn_source)
        after = conn_source.read_text()
        
        if success:
            if before != after:
                print("Bluefruit patch: OK - Applied BLEConnection.cpp fix (restore semaphore on disconnect)")
            else:
                print("Bluefruit patch: OK - BLEConnection.cpp already patched")
        else:
            print("Bluefruit patch: FAILED - BLEConnection.cpp")
            patch_failed = True
    else:
        print(f"Bluefruit patch: ERROR - BLEConnection.cpp not found at {conn_source}")
        patch_failed = True
    
    # Patch bluefruit.h - add friend declaration for blinky callback
    bf_header = bluefruit_lib / "bluefruit.h"
    if bf_header.exists():
        before = bf_header.read_text()
        success = _patch_bluefruit_header_led(bf_header)
        after = bf_header.read_text()

        if success:
            if before != after:
                print("Bluefruit patch: OK - Applied bluefruit.h fix (added blinky_cb friend)")
            else:
                print("Bluefruit patch: OK - bluefruit.h already patched")
        else:
            print("Bluefruit patch: FAILED - bluefruit.h")
            patch_failed = True
    else:
        print(f"Bluefruit patch: ERROR - bluefruit.h not found at {bf_header}")
        patch_failed = True

    # Patch bluefruit.cpp - guard LED toggle with _led_conn check
    bf_source = bluefruit_lib / "bluefruit.cpp"
    if bf_source.exists():
        before = bf_source.read_text()
        success = _patch_bluefruit_source_led(bf_source)
        after = bf_source.read_text()

        if success:
            if before != after:
                print("Bluefruit patch: OK - Applied bluefruit.cpp fix (guard blinky_cb with _led_conn)")
            else:
                print("Bluefruit patch: OK - bluefruit.cpp already patched")
        else:
            print("Bluefruit patch: FAILED - bluefruit.cpp")
            patch_failed = True
    else:
        print(f"Bluefruit patch: ERROR - bluefruit.cpp not found at {bf_source}")
        patch_failed = True

    if patch_failed:
        print("Bluefruit patch: CRITICAL - Patch failed! Build aborted.")
        env.Exit(1)


# Register the patch to run before build
bluefruit_action = env.VerboseAction(_apply_bluefruit_patches, "Applying Bluefruit BLE patches...")
env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", bluefruit_action)

# Also run immediately to patch before any compilation
_apply_bluefruit_patches(None, None, env)
