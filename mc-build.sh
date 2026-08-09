#!/usr/bin/env bash
set -euo pipefail

cd "$HOME/meshcore"
source bin/activate
cd MeshCore

export FIRMWARE_VERSION="v1.1x.0.muX"
export DISABLE_DEBUG=0

FIRMWARE_BASE="$HOME/meshcore/Firmware-MU"
BUILD_BASE="$HOME/meshcore/MeshCore/.pio/build"
WINDOWS_COPY_TARGET="/mnt/c/TEMP"

MODELS=(
  "Heltec_v3_repeater:bin merged-bin"
  "RAK_4631_repeater:zip uf2"
  "SenseCap_Solar_repeater:zip uf2"
  "Heltec_v3_companion_radio_ble:bin merged-bin"
  "Heltec_v3_companion_radio_wifi:bin merged-bin"
  "t1000e_companion_radio_ble:zip uf2"
  "WioTrackerL1_companion_radio_ble:zip uf2"
  "Heltec_v3_room_server:bin merged-bin"
)

REPEATERS=(
  "Heltec_v3_repeater"
  "RAK_4631_repeater"
  "SenseCap_Solar_repeater"
)

ROOMSERVERS=(
"Heltec_v3_room_server"
)


COMPANIONS=(
  "Heltec_v3_companion_radio_ble"
  "Heltec_v3_companion_radio_wifi"
  "t1000e_companion_radio_ble"
  "WioTrackerL1_companion_radio_ble"
)

ALL_MODEL_NAMES=()
for entry in "${MODELS[@]}"; do
  IFS=':' read -r model _ <<< "$entry"
  ALL_MODEL_NAMES+=("$model")
done

get_model_formats() {
  local search_model="$1"
  local entry model formats

  for entry in "${MODELS[@]}"; do
    IFS=':' read -r model formats <<< "$entry"
    if [[ "$model" == "$search_model" ]]; then
      echo "$formats"
      return 0
    fi
  done

  return 1
}

copy_artifacts() {
  local model="$1"
  local formats format
  local target_dir="$FIRMWARE_BASE/$model"

  formats="$(get_model_formats "$model")" || {
    echo "Keine Artefakt-Definition für Modell '$model' gefunden." >&2
    return 1
  }

  mkdir -p "$target_dir"

  for format in $formats; do
    case "$format" in
      bin)
        cp "$BUILD_BASE/$model/firmware.bin" \
          "$target_dir/$model-$FIRMWARE_VERSION.bin"
        ;;
      merged-bin)
        cp "$BUILD_BASE/$model/firmware-merged.bin" \
          "$target_dir/$model-$FIRMWARE_VERSION-merged.bin"
        ;;
      zip)
        cp "$BUILD_BASE/$model/firmware.zip" \
          "$target_dir/$model-$FIRMWARE_VERSION.zip"
        ;;
      uf2)
        cp "$BUILD_BASE/$model/firmware.uf2" \
          "$target_dir/$model-$FIRMWARE_VERSION.uf2"
        ;;
      *)
        echo "Unbekanntes Artefakt-Format '$format' für Modell '$model'." >&2
        return 1
        ;;
    esac
  done

  if [[ -d "$WINDOWS_COPY_TARGET" ]]; then
    cp -r "$FIRMWARE_BASE" "$WINDOWS_COPY_TARGET/"
  fi
}

build_model() {
  local model="$1"

  echo "==> Baue $model mit Firmware-Version $FIRMWARE_VERSION"
  bash build.sh build-firmware "$model"
  copy_artifacts "$model"
  echo "==> Fertig: $model"
  echo
}

build_group() {
  local group_name="$1"
  shift
  local models=("$@")

  echo "==> Baue Gruppe: $group_name"
  for model in "${models[@]}"; do
    build_model "$model"
  done
}

#build_single_model_menu() {
#  local model
#  local num_models
#  local back_num
#
#  while true; do
#    echo
#    echo "Verfügbare Modelle:"
#
#    select model in "${ALL_MODEL_NAMES[@]}" "Zurück"; do
#      num_models=${#ALL_MODEL_NAMES[@]}
#      back_num=$((num_models + 1))
#
#      case "$REPLY" in
#        "$back_num")
#          return 0
#          ;;
#        *)
#          if [[ -n "${model:-}" ]]; then
#            build_model "$model"
#            return 0
#          else
#            echo "Ungültige Auswahl."
#          fi
#          ;;
#      esac
#    done
#  done
#}

build_single_model_menu() {
  local model
  local num_models
  local back_num

  while true; do
    echo
    echo "Verfügbare Modelle:"

    num_models=${#ALL_MODEL_NAMES[@]}
    back_num=$((num_models + 1))

    for i in "${!ALL_MODEL_NAMES[@]}"; do
      printf "%d) %s\n" "$((i + 1))" "${ALL_MODEL_NAMES[i]}"
    done
    printf "%d) Zurück\n" "$back_num"
    echo
    read -rp "Bitte Auswahl eingeben: " REPLY

    case "$REPLY" in
      "$back_num")
        return 0
        ;;
      ''|*[!0-9]*)
        echo "Ungültige Auswahl."
        ;;
      *)
        if (( REPLY >= 1 && REPLY <= num_models )); then
          model="${ALL_MODEL_NAMES[REPLY-1]}"
          build_model "$model"
          return 0
        else
          echo "Ungültige Auswahl."
        fi
        ;;
    esac
  done
}

#show_main_menu() {
#  local options=(
#    "Alle kompilieren"
#    "Nur Repeater kompilieren"
#    "Nur Room Server kompilieren"
#    "Nur Companions kompilieren"
#    "Ein einzelnes Modell kompilieren"
#    "Beenden"
#  )
#
#  PS3=$'\nBitte Auswahl eingeben: '
#
#  while true; do
#    echo ""
#    echo "===================================="
#    echo "Building MeshCore Firmware-Version: $FIRMWARE_VERSION"
#    echo "===================================="
#
#    select opt in "${options[@]}"; do
#      case "$REPLY" in
#        1)
#          build_group "Alle" "${ALL_MODEL_NAMES[@]}"
#          return 0
#          ;;
#        2)
#          build_group "Repeater" "${REPEATERS[@]}"
#          return 0
#          ;;
#        3)
#          build_group "Room Servers" "${ROOMSERVERS[@]}"
#          return 0
#          ;;
#        4)
#          build_group "Companions" "${COMPANIONS[@]}"
#          return 0
#          ;;
#        5)
#          build_single_model_menu
#          break
#          ;;
#        6)
#          echo "Abgebrochen."
#          return 0
#          ;;
#        *)
#          echo "Ungültige Auswahl."
#          ;;
#      esac
#    done
#  done
#}

show_main_menu() {
  local options=(
    "Alle kompilieren"
    "Nur Repeater kompilieren"
    "Nur Room Server kompilieren"
    "Nur Companions kompilieren"
    "Ein einzelnes Modell kompilieren"
    "Beenden"
  )
  local num_options
  local i

  while true; do
    echo
    echo "===================================="
    echo "Building MeshCore Firmware-Version: $FIRMWARE_VERSION"
    echo "===================================="

    num_options=${#options[@]}

    for i in "${!options[@]}"; do
      printf '%d) %s\n' "$((i + 1))" "${options[i]}"
    done

    echo
    read -rp "Bitte Auswahl eingeben: " REPLY

    case "$REPLY" in
      1)
        build_group "Alle" "${ALL_MODEL_NAMES[@]}"
        return 0
        ;;
      2)
        build_group "Repeater" "${REPEATERS[@]}"
        return 0
        ;;
      3)
        build_group "Room Servers" "${ROOMSERVERS[@]}"
        return 0
        ;;
      4)
        build_group "Companions" "${COMPANIONS[@]}"
        return 0
        ;;
      5)
        build_single_model_menu
        ;;
      6)
        echo "Abgebrochen."
        return 0
        ;;
      ''|*[!0-9]*)
        echo "Ungültige Auswahl."
        ;;
      *)
        if (( REPLY < 1 || REPLY > num_options )); then
          echo "Ungültige Auswahl."
        else
          echo "Ungültige Auswahl."
        fi
        ;;
    esac
  done
}

main() {
  show_main_menu
}

main "$@"

