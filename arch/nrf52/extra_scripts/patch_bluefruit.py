"""
Bluefruit BLE Patch Script

Patches Bluefruit library to fix semaphore leak bug that causes device lockup
when BLE central disconnects unexpectedly (e.g., going out of range, supervision timeout).

Patches applied:
1. BLEConnection.h: Add _hvn_qsize member to track semaphore queue size
2. BLEConnection.cpp: Store hvn_qsize and restore semaphore on disconnect

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
    
    if patch_failed:
        print("Bluefruit patch: CRITICAL - Patch failed! Build aborted.")
        env.Exit(1)


# Register the patch to run before build
bluefruit_action = env.VerboseAction(_apply_bluefruit_patches, "Applying Bluefruit BLE patches...")
env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", bluefruit_action)

# Also run immediately to patch before any compilation
_apply_bluefruit_patches(None, None, env)
