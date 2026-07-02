#!/usr/bin/env bash
#
# Installs the Nordic nRF Connect SDK (NCS) + Zephyr toolchain *alongside* the
# existing PlatformIO setup, so MeshCore can be ported to the Zephyr-based
# Seeed XIAO nRF54L15 without disturbing the ESP32/nRF52/RP2040/STM32 builds.
#
# This is intentionally idempotent: re-running it skips work that's already done.
# It is HEAVY (multi-GB download + Zephyr SDK). Expect the first run to take a while.
#
# Usage:
#   bash .devcontainer/setup-zephyr.sh
#
# Override defaults via env, e.g.:
#   NCS_REV=v3.0.0 NCS_WORKSPACE=$HOME/ncs bash .devcontainer/setup-zephyr.sh
#
set -euo pipefail

# --- Tunables ----------------------------------------------------------------
# Pick the latest NCS release that ships the Seeed XIAO nRF54L15 board
NCS_REV="${NCS_REV:-v2.9.0}"
NCS_WORKSPACE="${NCS_WORKSPACE:-$HOME/ncs}"
VENV_DIR="${VENV_DIR:-$HOME/.zephyr-venv}"
# -----------------------------------------------------------------------------

echo ">> NCS revision : ${NCS_REV}"
echo ">> Workspace    : ${NCS_WORKSPACE}"
echo ">> Python venv  : ${VENV_DIR}"

# 1) Isolated Python venv for west (don't pollute the PlatformIO/pipx env).
if [ ! -d "${VENV_DIR}" ]; then
  echo ">> Creating Python venv for west..."
  python3 -m venv "${VENV_DIR}"
fi
# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"
pip install --upgrade pip wheel >/dev/null
pip install --upgrade west >/dev/null

# 2) Initialise the NCS workspace (skip if already initialised).
if [ ! -d "${NCS_WORKSPACE}/.west" ]; then
  echo ">> west init NCS workspace (${NCS_REV})..."
  west init -m https://github.com/nrfconnect/sdk-nrf --mr "${NCS_REV}" "${NCS_WORKSPACE}"
else
  echo ">> NCS workspace already initialised, skipping west init."
fi

cd "${NCS_WORKSPACE}"
echo ">> west update (this is the slow part)..."
west update
west zephyr-export

# 3) Zephyr's Python requirements.
echo ">> Installing Zephyr Python requirements..."
pip install -r zephyr/scripts/requirements.txt >/dev/null

# 4) Zephyr SDK (toolchain). `west sdk install` auto-selects the version that
#    matches this Zephyr tree, so we don't hardcode (and mis-pin) an SDK version.
echo ">> Installing the matching Zephyr SDK (compilers)..."
west sdk install || {
  echo "!! 'west sdk install' failed. On older west you may need to install the"
  echo "!! Zephyr SDK manually: https://docs.zephyrproject.org/latest/develop/getting_started/index.html"
}

# 5) Sanity build: confirm the XIAO nRF54L15 target actually compiles here.
echo ">> Smoke-building Zephyr 'hello_world' for seeed/xiao_nrf54l15..."
if west build -p always -b seeed/xiao_nrf54l15 zephyr/samples/hello_world -d /tmp/xiao_hello; then
  echo ">> OK: native Zephyr toolchain can build for the XIAO nRF54L15."
else
  echo "!! Zephyr build for seeed/xiao_nrf54l15 FAILED."
  echo "!! Likely cause: NCS_REV=${NCS_REV} predates XIAO nRF54L15 board support."
  echo "!! Re-run with a newer release, e.g.  NCS_REV=v3.0.0 bash .devcontainer/setup-zephyr.sh"
fi

cat <<EOF

============================================================================
Zephyr / NCS toolchain ready (workspace: ${NCS_WORKSPACE}).

To use it in a shell:
    source ${VENV_DIR}/bin/activate
    source ${NCS_WORKSPACE}/zephyr/zephyr-env.sh

PlatformIO is untouched and still drives the existing ESP32/nRF52/etc. variants.

--- Arduino-Zephyr core (the recommended on-ramp; see tools/lr2021_nrf54l15_smoke) ---
The nRF54L15 Arduino core is itself Zephyr-based and reuses this NCS install.
Install arduino-cli + the core, then build the smoke test:

    # arduino-cli (user space)
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

    # TODO: add the nRF54L15 Arduino core's Boards Manager URL, then:
    #   arduino-cli config add board_manager.additional_urls <CORE_INDEX_JSON_URL>
    #   arduino-cli core update-index
    #   arduino-cli core install <core:nrf54l>
    # Ref: https://blog.adafruit.com/2026/02/17/an-arduino-core-for-nrf54l15-zephyr-based/
============================================================================
EOF
