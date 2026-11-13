#pragma once

#include <SPIFFS.h>
#include <nvs.h>

// SPIFFS.begin(true) formats the partition whenever the mount fails. That turns
// a transient mount error into a wiped contact database. Format only on the
// first boot, before a successful mount has been recorded in NVS.
inline bool mountSPIFFS() {
  nvs_handle_t handle;
  if (SPIFFS.begin(false)) {
    if (nvs_open("meshcore", NVS_READWRITE, &handle) == ESP_OK) {
      nvs_set_u8(handle, "fsok", 1);
      nvs_commit(handle);
      nvs_close(handle);
    }
    return true;
  }

  bool already_used = false;
  if (nvs_open("meshcore", NVS_READONLY, &handle) == ESP_OK) {
    uint8_t flag = 0;
    if (nvs_get_u8(handle, "fsok", &flag) == ESP_OK && flag == 1) already_used = true;
    nvs_close(handle);
  }
  if (already_used) return false;

  if (!SPIFFS.begin(true)) return false;
  if (nvs_open("meshcore", NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_u8(handle, "fsok", 1);
    nvs_commit(handle);
    nvs_close(handle);
  }
  return true;
}
