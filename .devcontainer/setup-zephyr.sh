#!/usr/bin/env bash
#
# Provisions everything the Zephyr MeshCore companion
# (zephyr-port/07_companion) needs, *alongside* the existing PlatformIO setup,
# so the ESP32/nRF52/RP2040/STM32 builds are left untouched.
#
# The companion is built on **mainline Zephyr** (NOT the Nordic nRF Connect SDK):
# see zephyr-port/07_companion/README.md. This script therefore installs:
#   1. a west/Zephyr workspace from zephyrproject-rtos/zephyr  -> $ZEPHYR_WORKSPACE
#   2. the matching Zephyr SDK (compilers)                      via `west sdk install`
#   3. pyocd (CMSIS-DAP flash/RTT for the XIAO's on-board debugger)
#   4. arduino-cli + the three Arduino libraries the companion actually
#      compiles ($HOME/Arduino/libraries/{RadioLib,Crypto,base64}); the other
#      libs its CMakeLists.txt names (CayenneLPP/RTClib/ArduinoJson) are
#      header-shimmed in compat/ and not installed
#
# It is intentionally idempotent: re-running skips work that's already done.
# It is HEAVY (multi-GB clone + Zephyr SDK). Expect the first run to take a while.
#
# Usage:
#   bash .devcontainer/setup-zephyr.sh
#
# Override defaults via env, track the moving mainline tip instead of the
# pinned commit:
#   ZEPHYR_REV=main bash .devcontainer/setup-zephyr.sh
#
set -euo pipefail

# --- Tunables ----------------------------------------------------------------
# Mainline Zephyr revision. Pinned to a known-good commit on `main` that reports
# VERSION 4.4.99 and ships the Seeed XIAO nRF54L15 board
# (boards/seeed/xiao_nrf54l15). Override with ZEPHYR_REV=main to track the tip.
#   https://docs.zephyrproject.org/latest/boards/seeed/xiao_nrf54l15/doc/index.html
ZEPHYR_REV="${ZEPHYR_REV:-c9a40340e8e3b94d6660beb98f4d9a858cfd937e}"
ZEPHYR_WORKSPACE="${ZEPHYR_WORKSPACE:-$HOME/zephyrproject}"
VENV_DIR="${VENV_DIR:-$HOME/.zephyr-venv}"

# The board target the companion builds for (README + CMakeLists).
BOARD="${BOARD:-xiao_nrf54l15/nrf54l15/cpuapp}"

# Arduino libraries the companion's CMakeLists.txt pulls in by absolute path
# ($HOME/Arduino/libraries/...). Versions match a known-good build; RadioLib
# 7.7.1 already carries the LR2021 driver *and* the multi-SF side-detector API
# (setSideDetector / setLoRaSideDetCad) upstream, so no fork/patch is needed.
ARDUINO_DIR="${ARDUINO_DIR:-$HOME/Arduino}"
RADIOLIB_VER="${RADIOLIB_VER:-7.7.1}"
CRYPTO_VER="${CRYPTO_VER:-0.4.0}"
BASE64_VER="${BASE64_VER:-1.3.0}"
# -----------------------------------------------------------------------------

echo ">> Zephyr revision : ${ZEPHYR_REV}"
echo ">> Workspace       : ${ZEPHYR_WORKSPACE}"
echo ">> Python venv     : ${VENV_DIR}"
echo ">> Board           : ${BOARD}"

# 1) Isolated Python venv for west + pyocd (don't pollute the PlatformIO/pipx env).
if [ ! -d "${VENV_DIR}" ]; then
  echo ">> Creating Python venv for west..."
  python3 -m venv "${VENV_DIR}"
fi
# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"
pip install --upgrade pip wheel >/dev/null
# west (build system) + pyocd (CMSIS-DAP flash & RTT console for the XIAO).
pip install --upgrade west pyocd >/dev/null

# 2) Initialise the mainline Zephyr workspace (skip if already initialised).
if [ ! -d "${ZEPHYR_WORKSPACE}/.west" ]; then
  echo ">> west init mainline Zephyr workspace (${ZEPHYR_REV})..."
  west init -m https://github.com/zephyrproject-rtos/zephyr --mr "${ZEPHYR_REV}" "${ZEPHYR_WORKSPACE}"
else
  echo ">> Zephyr workspace already initialised, skipping west init."
fi

cd "${ZEPHYR_WORKSPACE}"
echo ">> west update (this is the slow part)..."
west update
west zephyr-export

# 3) Zephyr's Python requirements.
echo ">> Installing Zephyr Python requirements..."
pip install -r zephyr/scripts/requirements.txt >/dev/null

# 4) Zephyr SDK (toolchain). `west sdk install` auto-selects the version that
#    matches this Zephyr tree, so we don't hardcode (and mis-pin) an SDK version.
#    Only the Arm toolchain is needed (nRF54L15 = Cortex-M33); without -t it
#    would download every architecture's toolchain.
echo ">> Installing the matching Zephyr SDK (compilers)..."
west sdk install -t arm-zephyr-eabi || {
  echo "!! 'west sdk install' failed. On older west you may need to install the"
  echo "!! Zephyr SDK manually: https://docs.zephyrproject.org/latest/develop/getting_started/index.html"
}

# 5) arduino-cli + the Arduino libraries the companion build compiles.
#    arduino-cli's default sketchbook is $HOME/Arduino, so the libraries land
#    in $HOME/Arduino/libraries/{RadioLib,Crypto,base64} exactly where
#    CMakeLists.txt looks.
if ! command -v arduino-cli >/dev/null 2>&1; then
  echo ">> Installing arduino-cli (user space -> ~/.local/bin)..."
  mkdir -p "$HOME/.local/bin"
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
    | BINDIR="$HOME/.local/bin" sh
  export PATH="$HOME/.local/bin:$PATH"
fi
# Keep libraries in $HOME/Arduino so they match the CMake absolute paths.
# No --overwrite: an existing config is kept; directories.user is pinned below.
arduino-cli config init >/dev/null 2>&1 || true
arduino-cli config set directories.user "${ARDUINO_DIR}" >/dev/null 2>&1 || true
echo ">> Installing Arduino libraries (RadioLib/Crypto/base64) into ${ARDUINO_DIR}/libraries..."
arduino-cli lib update-index >/dev/null
arduino-cli lib install \
  "RadioLib@${RADIOLIB_VER}" \
  "Crypto@${CRYPTO_VER}" \
  "base64@${BASE64_VER}"

# 6) Sanity build: confirm the XIAO nRF54L15 target actually compiles here.
echo ">> Smoke-building Zephyr 'hello_world' for ${BOARD}..."
if west build -p always -b "${BOARD}" zephyr/samples/hello_world -d /tmp/xiao_hello; then
  echo ">> OK: native Zephyr toolchain can build for the XIAO nRF54L15."
else
  echo "!! Zephyr build for ${BOARD} FAILED."
  echo "!! Likely cause: ZEPHYR_REV=${ZEPHYR_REV} predates XIAO nRF54L15 board support,"
  echo "!! or the board target string changed. Confirm the board at:"
  echo "!!   https://docs.zephyrproject.org/latest/boards/seeed/xiao_nrf54l15/doc/index.html"
fi

cat <<EOF

============================================================================
Zephyr toolchain + companion deps ready.

  Zephyr workspace : ${ZEPHYR_WORKSPACE}   (mainline @ ${ZEPHYR_REV})
  Zephyr base      : ${ZEPHYR_WORKSPACE}/zephyr
  Python venv      : ${VENV_DIR}
  Arduino libs     : ${ARDUINO_DIR}/libraries/{RadioLib,Crypto,base64}
  Flash/RTT tool   : pyocd (target: nrf54l)

To build & flash the companion:
    source ${VENV_DIR}/bin/activate
    export ZEPHYR_BASE=${ZEPHYR_WORKSPACE}/zephyr

    cd zephyr-port/07_companion
    west build -b ${BOARD} -d build . --pristine
    pyocd flash -t nrf54l -e chip build/zephyr/zephyr.hex
    pyocd reset -t nrf54l

PlatformIO is untouched and still drives the existing ESP32/nRF52/etc. variants.
============================================================================
EOF
