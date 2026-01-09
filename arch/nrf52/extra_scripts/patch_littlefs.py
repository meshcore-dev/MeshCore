"""
LittleFS Patch Script

This script updates the Adafruit nRF52 Arduino framework's LittleFS library
from version 1.6 (used in Adafruit 1.7.0) to version 1.7.0.

The patch uses the official LittleFS v1.7.0 source from:
https://github.com/littlefs-project/littlefs/releases/tag/v1.7.0

"""

from pathlib import Path
from typing import Tuple

Import("env")  # pylint: disable=undefined-variable

try:
  SCRIPT_DIR = Path(__file__).resolve().parent
except NameError:
  SCRIPT_DIR = Path(env.subst("$PROJECT_DIR")) / "arch" / "nrf52" / "extra_scripts"
PATCH_DIR = SCRIPT_DIR / "littlefs_patch"


def _copy_if_different(source: Path, target: Path) -> Tuple[bool, bool]:
  """
  Copy source to target if different.
  Returns (changed, success) tuple.
  """
  if not source.exists():
    return (False, False)

  try:
    src_data = source.read_bytes()

    if not target.exists():
      target.write_bytes(src_data)
      # Verify write
      verify_data = target.read_bytes()
      if verify_data == src_data:
        return (True, True)
      return (False, False)

    dst_data = target.read_bytes()
    if src_data != dst_data:
      target.write_bytes(src_data)
      # Verify write
      verify_data = target.read_bytes()
      if verify_data == src_data:
        return (True, True)
      return (False, False)

    return (False, True)  # Already up to date
  except Exception as e:
    print(f"LittleFS patch: ERROR copying {source.name}: {e}")
    return (False, False)


def _apply_littlefs_patch(target, source, env):  # pylint: disable=unused-argument
  framework_path = env.get("PLATFORMFW_DIR")
  if not framework_path:
    framework_path = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52")

  if not framework_path:
    print("LittleFS patch: ERROR - framework directory not found")
    env.Exit(1)
    return

  framework_dir = Path(framework_path)

  targets = {
      PATCH_DIR / "lfs.c": framework_dir / "libraries" / "Adafruit_LittleFS" / "src" / "littlefs" / "lfs.c",
      PATCH_DIR / "lfs.h": framework_dir / "libraries" / "Adafruit_LittleFS" / "src" / "littlefs" / "lfs.h",
  }

  updated = False
  patch_failed = False
  
  for source_file, target_file in targets.items():
    if not source_file.exists():
      print(f"LittleFS patch: ✗ ERROR - source missing {source_file.name}")
      patch_failed = True
      continue

    changed, success = _copy_if_different(source_file, target_file)
    
    if success:
      status = "updated" if changed else "unchanged"
      print(f"LittleFS patch: OK - {source_file.name} {status}")
      updated |= changed
    else:
      print(f"LittleFS patch: FAILED - Failed to patch {source_file.name}")
      patch_failed = True

  if patch_failed:
    print("LittleFS patch: CRITICAL - Patch verification failed! Build aborted.")
    env.Exit(1)
  elif updated:
    print("LittleFS patch: OK - Applied updates")
  else:
    print("LittleFS patch: OK - Already up to date")


littlefs_action = env.VerboseAction(_apply_littlefs_patch, "")
env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", littlefs_action)
_apply_littlefs_patch(None, None, env)



