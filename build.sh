#!/usr/bin/env bash

ALL_PIO_ENVS=()
PIO_CONFIG_JSON=""
MENU_CHOICE=""
SELECTED_TARGET=""

ENV_VARIANT_SUFFIX_PATTERN='companion_radio_serial|companion_radio_wifi|companion_radio_usb|comp_radio_usb|companion_usb|companion_radio_ble|companion_ble|repeater_bridge_rs232_serial1|repeater_bridge_rs232_serial2|repeater_bridge_rs232|repeater_bridge_espnow|terminal_chat|room_server|room_svr|kiss_modem|sensor|repeatr|repeater'
BOARD_MODIFIER_WITHOUT_DISPLAY="_without_display"
BOARD_MODIFIER_LOGGING="_logging"
BOARD_MODIFIER_TFT="_tft"
BOARD_MODIFIER_EINK="_eink"
BOARD_MODIFIER_EINK_SUFFIX="Eink"
BOARD_LABEL_WITHOUT_DISPLAY="without_display"
BOARD_LABEL_LOGGING="logging"
BOARD_LABEL_TFT="tft"
BOARD_LABEL_EINK="eink"
DEFAULT_VARIANT_LABEL="default"
TAG_PREFIX_ROOM_SERVER="room-server"
TAG_PREFIX_COMPANION="companion"
TAG_PREFIX_REPEATER="repeater"
BULK_BUILD_SUFFIX_REPEATER="_repeater"
BULK_BUILD_SUFFIX_COMPANION_USB="_companion_radio_usb"
BULK_BUILD_SUFFIX_COMPANION_BLE="_companion_radio_ble"
BULK_BUILD_SUFFIX_ROOM_SERVER="_room_server"
SUPPORTED_PLATFORM_PATTERN='ESP32_PLATFORM|NRF52_PLATFORM|STM32_PLATFORM|RP2040_PLATFORM'
OUTPUT_DIR="out"
FALLBACK_VERSION_PREFIX="dev"
FALLBACK_VERSION_DATE_FORMAT='+%Y-%m-%d-%H-%M'

# External programs invoked by this script:
#   bash, cat, cp, date, git, grep, head, mkdir, pio, python3, rm, sed, sort
# Keep this list in sync when adding or removing non-builtin command usage.

global_usage() {
  cat - <<EOF
Usage:
bash build.sh <command> [target]

Commands:
  help|usage|-h|--help: Shows this message.
  list|-l: List firmwares available to build.
  build-firmware <target>: Build the firmware for the given build target.
  build-firmwares: Build all firmwares for all targets.
  build-matching-firmwares <build-match-spec>: Build all firmwares for build targets containing the string given for <build-match-spec>.
  build-companion-firmwares: Build all companion firmwares for all build targets.
  build-repeater-firmwares: Build all repeater firmwares for all build targets.
  build-room-server-firmwares: Build all chat room server firmwares for all build targets.

Examples:
Build firmware for the "RAK_4631_repeater" device target
$ bash build.sh build-firmware RAK_4631_repeater

Run without arguments to choose a target from an interactive menu
$ bash build.sh

Build all firmwares for device targets containing the string "RAK_4631"
$ bash build.sh build-matching-firmwares <build-match-spec>

Build all companion firmwares
$ bash build.sh build-companion-firmwares

Build all repeater firmwares
$ bash build.sh build-repeater-firmwares

Build all chat room server firmwares
$ bash build.sh build-room-server-firmwares

Environment Variables:
  FIRMWARE_VERSION=vX.Y.Z: Firmware version to embed in the build output.
                           If not set, build.sh derives a default from the latest matching git tag and appends "-dev".
  DISABLE_DEBUG=1: Disables all debug logging flags (MESH_DEBUG, MESH_PACKET_LOGGING, etc.)
                   If not set, debug flags from variant platformio.ini files are used.

Examples:
Build without debug logging:
$ export FIRMWARE_VERSION=v1.0.0
$ export DISABLE_DEBUG=1
$ bash build.sh build-firmware RAK_4631_repeater

Build with debug logging (default, uses flags from variant files):
$ export FIRMWARE_VERSION=v1.0.0
$ bash build.sh build-firmware RAK_4631_repeater

Build with the derived default version from git tags:
$ unset FIRMWARE_VERSION
$ bash build.sh
EOF
}

init_project_context() {
  if [ ${#ALL_PIO_ENVS[@]} -eq 0 ]; then
    mapfile -t ALL_PIO_ENVS < <(pio project config | grep 'env:' | sed 's/env://')
  fi

  if [ -z "$PIO_CONFIG_JSON" ]; then
    PIO_CONFIG_JSON=$(pio project config --json-output)
  fi
}

get_pio_envs() {
  if [ ${#ALL_PIO_ENVS[@]} -gt 0 ]; then
    printf '%s\n' "${ALL_PIO_ENVS[@]}"
  else
    pio project config | grep 'env:' | sed 's/env://'
  fi
}

canonicalize_variant_suffix() {
  local variant_suffix=$1

  case "${variant_suffix,,}" in
    comp_radio_usb|companion_usb|companion_radio_usb)
      echo "companion_radio_usb"
      ;;
    companion_ble|companion_radio_ble)
      echo "companion_radio_ble"
      ;;
    room_svr|room_server)
      echo "room_server"
      ;;
    repeatr|repeater)
      echo "repeater"
      ;;
    *)
      echo "${variant_suffix,,}"
      ;;
  esac
}

trim_trailing_underscores() {
  local value=$1

  while [[ "$value" == *_ ]]; do
    value=${value%_}
  done

  echo "$value"
}

sort_lines_case_insensitive() {
  sort -f
}

print_numbered_menu() {
  local items=("$@")
  local i

  for i in "${!items[@]}"; do
    printf '%d) %s\n' "$((i + 1))" "${items[$i]}"
  done
}

prompt_menu_choice() {
  local prompt_label=$1
  local max_choice=$2
  local allow_back=${3:-0}
  local choice

  while true; do
    if [ "$allow_back" -eq 1 ]; then
      read -r -p "${prompt_label} [1-${max_choice}, B=Back, Q=Quit]: " choice
    else
      read -r -p "${prompt_label} [1-${max_choice}, Q=Quit]: " choice
    fi

    case "${choice^^}" in
      Q)
        MENU_CHOICE="QUIT"
        return 0
        ;;
      B)
        if [ "$allow_back" -eq 1 ]; then
          MENU_CHOICE="BACK"
          return 0
        fi
        echo "Invalid selection."
        ;;
      *)
        if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "$max_choice" ]; then
          MENU_CHOICE="$choice"
          return 0
        fi
        echo "Invalid selection."
        ;;
    esac
  done
}

get_env_metadata() {
  local env_name=$1
  local trimmed_env_name
  local board_part
  local variant_part
  local board_family
  local board_modifier
  local variant_label
  local tag_prefix

  trimmed_env_name=$(trim_trailing_underscores "$env_name")
  board_part=$trimmed_env_name
  variant_part=""

  shopt -s nocasematch
  # Split a raw env name into board and variant pieces using the normalized
  # suffix vocabulary defined near the top of the file.
  if [[ "$trimmed_env_name" =~ ^(.+)[_-](${ENV_VARIANT_SUFFIX_PATTERN})$ ]]; then
    board_part=${BASH_REMATCH[1]}
    variant_part=$(canonicalize_variant_suffix "${BASH_REMATCH[2]}")
  fi

  # Fold display and form-factor suffixes into the variant label so related
  # boards share one first-level menu entry.
  case "$board_part" in
    *"$BOARD_MODIFIER_WITHOUT_DISPLAY")
      board_family=${board_part%"$BOARD_MODIFIER_WITHOUT_DISPLAY"}
      board_modifier="$BOARD_LABEL_WITHOUT_DISPLAY"
      ;;
    *"$BOARD_MODIFIER_LOGGING")
      board_family=${board_part%"$BOARD_MODIFIER_LOGGING"}
      board_modifier="$BOARD_LABEL_LOGGING"
      ;;
    *"$BOARD_MODIFIER_TFT")
      board_family=${board_part%"$BOARD_MODIFIER_TFT"}
      board_modifier="$BOARD_LABEL_TFT"
      ;;
    *"$BOARD_MODIFIER_EINK")
      board_family=${board_part%"$BOARD_MODIFIER_EINK"}
      board_modifier="$BOARD_LABEL_EINK"
      ;;
    *"$BOARD_MODIFIER_EINK_SUFFIX")
      board_family=${board_part%"$BOARD_MODIFIER_EINK_SUFFIX"}
      board_modifier="$BOARD_LABEL_EINK"
      ;;
    *)
      board_family=$board_part
      board_modifier=""
      ;;
  esac
  shopt -u nocasematch

  variant_label="$variant_part"
  if [ -n "$board_modifier" ]; then
    if [ -n "$variant_label" ]; then
      variant_label="${board_modifier}_${variant_label}"
    else
      variant_label="$board_modifier"
    fi
  fi

  if [ -z "$variant_label" ]; then
    variant_label="$DEFAULT_VARIANT_LABEL"
  fi

  case "$variant_part" in
    room_server)
      tag_prefix="$TAG_PREFIX_ROOM_SERVER"
      ;;
    companion_radio_*)
      tag_prefix="$TAG_PREFIX_COMPANION"
      ;;
    repeater*)
      tag_prefix="$TAG_PREFIX_REPEATER"
      ;;
    *)
      tag_prefix=""
      ;;
  esac

  printf '%s\t%s\t%s\n' "$board_family" "$variant_label" "$tag_prefix"
}

get_metadata_field() {
  local env_name=$1
  local field_index=$2
  local metadata

  metadata=$(get_env_metadata "$env_name")
  case "$field_index" in
    1)
      echo "${metadata%%$'\t'*}"
      ;;
    2)
      metadata=${metadata#*$'\t'}
      echo "${metadata%%$'\t'*}"
      ;;
    3)
      echo "${metadata##*$'\t'}"
      ;;
  esac
}

get_board_family_for_env() {
  get_metadata_field "$1" 1
}

get_variant_name_for_env() {
  get_metadata_field "$1" 2
}

get_release_tag_prefix_for_env() {
  get_metadata_field "$1" 3
}

get_variants_for_board() {
  local board_family=$1
  local env

  for env in "${ALL_PIO_ENVS[@]}"; do
    if [ "$(get_board_family_for_env "$env")" == "$board_family" ]; then
      echo "$env"
    fi
  done | sort_lines_case_insensitive
}

prompt_for_variant_for_board() {
  local board=$1
  local -A seen_variant_labels=()
  local variants
  local variant_labels
  local i
  local j

  mapfile -t variants < <(get_variants_for_board "$board")
  if [ ${#variants[@]} -eq 0 ]; then
    echo "No firmware variants were found for ${board}."
    return 1
  fi

  if [ ${#variants[@]} -eq 1 ]; then
    SELECTED_TARGET="${variants[0]}"
    return 0
  fi

  variant_labels=()
  for i in "${!variants[@]}"; do
    variant_labels[i]=$(get_variant_name_for_env "${variants[$i]}")
    seen_variant_labels["${variant_labels[$i]}"]=$(( ${seen_variant_labels["${variant_labels[$i]}"]:-0} + 1 ))
  done

  # Stop early if normalization would present the user with ambiguous labels.
  for i in "${!variant_labels[@]}"; do
    if [ "${seen_variant_labels["${variant_labels[$i]}"]}" -gt 1 ]; then
      echo "Ambiguous firmware variants detected for ${board}: ${variant_labels[$i]}"
      echo "The normalized menu labels are not unique for this board family."
      for j in "${!variants[@]}"; do
        echo "  ${variants[$j]}"
      done
      exit 1
    fi
  done

  echo "Select a firmware variant for ${board}:"
  while true; do
    print_numbered_menu "${variant_labels[@]}"
    prompt_menu_choice "Variant selection" "${#variant_labels[@]}" 1
    if [ "$MENU_CHOICE" == "BACK" ]; then
      return 1
    fi
    if [ "$MENU_CHOICE" == "QUIT" ]; then
      echo "Cancelled."
      exit 1
    fi

    SELECTED_TARGET="${variants[$((MENU_CHOICE - 1))]}"
    return 0
  done
}

prompt_for_board_target() {
  local -A seen_boards=()
  local boards=()
  local board
  local env

  if ! [ -t 0 ]; then
    echo "No command provided and no interactive terminal is available."
    global_usage
    exit 1
  fi

  if [ ${#ALL_PIO_ENVS[@]} -eq 0 ]; then
    echo "No PlatformIO environments were found."
    exit 1
  fi

  for env in "${ALL_PIO_ENVS[@]}"; do
    board=$(get_board_family_for_env "$env")
    if [ -z "${seen_boards[$board]}" ]; then
      seen_boards["$board"]=1
      boards+=("$board")
    fi
  done

  mapfile -t boards < <(printf '%s\n' "${boards[@]}" | sort_lines_case_insensitive)

  echo "No command provided. Select a board family:"
  while true; do
    print_numbered_menu "${boards[@]}"
    prompt_menu_choice "Board selection" "${#boards[@]}"
    if [ "$MENU_CHOICE" == "QUIT" ]; then
      echo "Cancelled."
      exit 1
    fi

    board=${boards[$((MENU_CHOICE - 1))]}
    if prompt_for_variant_for_board "$board"; then
      echo "Building firmware for ${SELECTED_TARGET}"
      return 0
    fi
  done
}

get_latest_version_from_tags() {
  local env_name=$1
  local tag_prefix
  local latest_tag
  local fallback_version

  fallback_version="${FALLBACK_VERSION_PREFIX}-$(date "${FALLBACK_VERSION_DATE_FORMAT}")"
  tag_prefix=$(get_release_tag_prefix_for_env "$env_name")
  if [ -z "$tag_prefix" ]; then
    echo "$fallback_version"
    return 0
  fi

  latest_tag=$(git tag --list "${tag_prefix}-v*" --sort=-version:refname | head -n 1)
  if [ -z "$latest_tag" ]; then
    echo "$fallback_version"
    return 0
  fi

  echo "${latest_tag#"${tag_prefix}"-}"
}

derive_default_firmware_version() {
  local env_name=$1
  local base_version

  base_version=$(get_latest_version_from_tags "$env_name")
  case "$base_version" in
    *-dev|dev-*)
      echo "$base_version"
      ;;
    *)
      echo "${base_version}-dev"
      ;;
  esac
}

prompt_for_firmware_version() {
  local env_name=$1
  local suggested_version
  local entered_version

  suggested_version=$(derive_default_firmware_version "$env_name")

  if ! [ -t 0 ]; then
    FIRMWARE_VERSION="$suggested_version"
    echo "FIRMWARE_VERSION not set, using derived default: ${FIRMWARE_VERSION}"
    return 0
  fi

  echo "Suggested firmware version for ${env_name}: ${suggested_version}"
  read -r -e -i "${suggested_version}" -p "Firmware version: " entered_version
  FIRMWARE_VERSION="${entered_version:-$suggested_version}"
}

get_pio_envs_containing_string() {
  local env

  shopt -s nocasematch
  for env in "${ALL_PIO_ENVS[@]}"; do
    if [[ "$env" == *${1}* ]]; then
      echo "$env"
    fi
  done
  shopt -u nocasematch
}

get_pio_envs_ending_with_string() {
  local env

  shopt -s nocasematch
  for env in "${ALL_PIO_ENVS[@]}"; do
    if [[ "$env" == *${1} ]]; then
      echo "$env"
    fi
  done
  shopt -u nocasematch
}

get_platform_for_env() {
  local env_name=$1

  # PlatformIO exposes project config as JSON; scan the selected env's
  # build_flags to recover the platform token used for artifact collection.
  # Feed the cached JSON via stdin to avoid shell echo quirks and argv/env size limits.
  python3 -c "
import sys, json, re
data = json.load(sys.stdin)
for section, options in data:
    if section == 'env:$env_name':
        for key, value in options:
            if key == 'build_flags':
                for flag in value:
                    match = re.search(r'($SUPPORTED_PLATFORM_PATTERN)', flag)
                    if match:
                        print(match.group(1))
                        sys.exit(0)
" <<<"$PIO_CONFIG_JSON"
}

is_supported_platform() {
  local env_platform=$1

  [[ "$env_platform" =~ ^(${SUPPORTED_PLATFORM_PATTERN})$ ]]
}

disable_debug_flags() {
  if [ "$DISABLE_DEBUG" == "1" ]; then
    export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS} -UMESH_DEBUG -UBLE_DEBUG_LOGGING -UWIFI_DEBUG_LOGGING -UBRIDGE_DEBUG -UGPS_NMEA_DEBUG -UCORE_DEBUG_LEVEL -UESPNOW_DEBUG_LOGGING -UDEBUG_RP2040_WIRE -UDEBUG_RP2040_SPI -UDEBUG_RP2040_CORE -UDEBUG_RP2040_PORT -URADIOLIB_DEBUG_SPI -UCFG_DEBUG -URADIOLIB_DEBUG_BASIC -URADIOLIB_DEBUG_PROTOCOL"
  fi
}

copy_build_output() {
  local source_path=$1
  local output_path=$2

  if [ -f "$source_path" ]; then
    cp -- "$source_path" "$output_path"
  fi
}

collect_esp32_artifacts() {
  local env_name=$1
  local firmware_filename=$2

  pio run -t mergebin -e "$env_name"
  copy_build_output ".pio/build/${env_name}/firmware.bin" "out/${firmware_filename}.bin"
  copy_build_output ".pio/build/${env_name}/firmware-merged.bin" "out/${firmware_filename}-merged.bin"
}

collect_nrf52_artifacts() {
  local env_name=$1
  local firmware_filename=$2

  python3 bin/uf2conv/uf2conv.py ".pio/build/${env_name}/firmware.hex" -c -o ".pio/build/${env_name}/firmware.uf2" -f 0xADA52840
  copy_build_output ".pio/build/${env_name}/firmware.uf2" "out/${firmware_filename}.uf2"
  copy_build_output ".pio/build/${env_name}/firmware.zip" "out/${firmware_filename}.zip"
}

collect_stm32_artifacts() {
  local env_name=$1
  local firmware_filename=$2

  copy_build_output ".pio/build/${env_name}/firmware.bin" "out/${firmware_filename}.bin"
  copy_build_output ".pio/build/${env_name}/firmware.hex" "out/${firmware_filename}.hex"
}

collect_rp2040_artifacts() {
  local env_name=$1
  local firmware_filename=$2

  copy_build_output ".pio/build/${env_name}/firmware.bin" "out/${firmware_filename}.bin"
  copy_build_output ".pio/build/${env_name}/firmware.uf2" "out/${firmware_filename}.uf2"
}

collect_build_artifacts() {
  local env_name=$1
  local env_platform=$2
  local firmware_filename=$3

  # Post-build outputs differ by platform, so dispatch to the matching
  # collector after the main firmware build succeeds.
  case "$env_platform" in
    ESP32_PLATFORM)
      collect_esp32_artifacts "$env_name" "$firmware_filename"
      ;;
    NRF52_PLATFORM)
      collect_nrf52_artifacts "$env_name" "$firmware_filename"
      ;;
    STM32_PLATFORM)
      collect_stm32_artifacts "$env_name" "$firmware_filename"
      ;;
    RP2040_PLATFORM)
      collect_rp2040_artifacts "$env_name" "$firmware_filename"
      ;;
  esac
}

build_firmware() {
  local env_name=$1
  local env_platform
  local commit_hash
  local firmware_build_date
  local firmware_version_string
  local firmware_filename

  env_platform=$(get_platform_for_env "$env_name")
  if ! is_supported_platform "$env_platform"; then
    echo "Unsupported or unknown platform for env: $env_name"
    exit 1
  fi

  commit_hash=$(git rev-parse --short HEAD)
  firmware_build_date=$(date '+%d-%b-%Y')

  if [ -z "$FIRMWARE_VERSION" ]; then
    prompt_for_firmware_version "$env_name"
    echo "Using firmware version: ${FIRMWARE_VERSION}"
  fi

  firmware_version_string="${FIRMWARE_VERSION}-${commit_hash}"
  firmware_filename="${env_name}-${firmware_version_string}"

  export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS} -DFIRMWARE_BUILD_DATE='\"${firmware_build_date}\"' -DFIRMWARE_VERSION='\"${firmware_version_string}\"'"
  disable_debug_flags

  pio run -e "$env_name"
  collect_build_artifacts "$env_name" "$env_platform" "$firmware_filename"
}

build_all_firmwares_matching() {
  local envs
  local env

  mapfile -t envs < <(get_pio_envs_containing_string "$1")
  for env in "${envs[@]}"; do
    build_firmware "$env"
  done
}

build_all_firmwares_by_suffix() {
  local envs
  local env

  mapfile -t envs < <(get_pio_envs_ending_with_string "$1")
  for env in "${envs[@]}"; do
    build_firmware "$env"
  done
}

build_repeater_firmwares() {
  build_all_firmwares_by_suffix "$BULK_BUILD_SUFFIX_REPEATER"
}

build_companion_firmwares() {
  build_all_firmwares_by_suffix "$BULK_BUILD_SUFFIX_COMPANION_USB"
  build_all_firmwares_by_suffix "$BULK_BUILD_SUFFIX_COMPANION_BLE"
}

build_room_server_firmwares() {
  build_all_firmwares_by_suffix "$BULK_BUILD_SUFFIX_ROOM_SERVER"
}

build_firmwares() {
  build_companion_firmwares
  build_repeater_firmwares
  build_room_server_firmwares
}

prepare_output_dir() {
  local output_dir="$OUTPUT_DIR"

  if [ -z "$output_dir" ] || [ "$output_dir" == "/" ] || [ "$output_dir" == "." ]; then
    echo "Refusing to clean unsafe output directory: $output_dir"
    exit 1
  fi

  rm -rf -- "$output_dir"
  mkdir -p -- "$output_dir"
}

run_build_firmware_command() {
  local targets=("${@:2}")
  local env

  if [ ${#targets[@]} -eq 0 ]; then
    echo "usage: $0 build-firmware <target>"
    exit 1
  fi

  for env in "${targets[@]}"; do
    build_firmware "$env"
  done
}

run_command() {
  case "$1" in
    build-firmware)
      run_build_firmware_command "$@"
      ;;
    build-matching-firmwares)
      if [ -n "$2" ]; then
        build_all_firmwares_matching "$2"
      else
        echo "usage: $0 build-matching-firmwares <build-match-spec>"
        exit 1
      fi
      ;;
    build-firmwares)
      build_firmwares
      ;;
    build-companion-firmwares)
      build_companion_firmwares
      ;;
    build-repeater-firmwares)
      build_repeater_firmwares
      ;;
    build-room-server-firmwares)
      build_room_server_firmwares
      ;;
    *)
      global_usage
      exit 1
      ;;
  esac
}

main() {
  case "${1:-}" in
    help|usage|-h|--help)
      global_usage
      exit 0
      ;;
    list|-l)
      init_project_context
      get_pio_envs
      exit 0
      ;;
  esac

  init_project_context

  if [ $# -eq 0 ]; then
    prompt_for_board_target
    set -- build-firmware "$SELECTED_TARGET"
  fi

  prepare_output_dir
  run_command "$@"
}

main "$@"
