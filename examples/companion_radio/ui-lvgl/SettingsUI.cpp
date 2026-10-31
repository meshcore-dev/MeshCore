#include "SettingsUI.h"

#include <Arduino.h>
#include <math.h>
#include "UITask.h"
#include "../MyMesh.h"
#include "target.h"
#include <SPIFFS.h>
#include <SD_MMC.h>

#define S_COL_CARD   0x16233B
#define S_COL_ACCENT 0x58B4FF
#define S_COL_MUTED  0x8FA3BF
#define S_KB_H 160

#define TX_MIN 2
#define TX_MAX 22

static UITask* s_task = NULL;

static lv_obj_t* name_val;
static lv_obj_t* radio_val;
static lv_obj_t* tx_val;
static lv_obj_t* pin_val;
static lv_obj_t* tz_val;
static int tz_offset = 0;    // UTC offset hours (persisted; wizard suggests from GPS)

// single-field editor overlay (name / BLE pin)
enum EditMode { EDIT_NAME, EDIT_PIN };
static EditMode edit_mode;
static lv_obj_t* edit_scr;
static lv_obj_t* edit_title;
static lv_obj_t* edit_ta;
static lv_obj_t* edit_kb;

// radio params overlay
static lv_obj_t* radio_scr;
static lv_obj_t* radio_ta[4];   // freq MHz, BW kHz, SF, CR
static lv_obj_t* radio_kb;
static lv_obj_t* radio_apply_btn;
static lv_obj_t* radio_apply_lbl;
static lv_obj_t* radio_preset_dd;
static lv_obj_t* radio_repeat_sw;
static lv_obj_t* tx_minus_btn;
static lv_obj_t* tx_plus_btn;
static bool radio_confirm_armed = false;

// Regional presets. Client repeat mode only runs on the repeat frequencies
// MyMesh validates, so each preset carries the one for its band.
struct RadioPreset {
  const char* label;        // dropdown text
  const char* short_label;  // Settings row text
  float freq; float bw; uint8_t sf; uint8_t cr;
  float repeat_freq;
};
static const RadioPreset RADIO_PRESETS[] = {
  { "USA/CA narrow (rec.)", "US narrow",  910.525f,  62.5f,  7, 5, 918.000f },
  { "USA/CA classic",       "US classic", 910.525f, 250.0f, 10, 5, 918.000f },
  { "EU/UK 869.525",        "EU/UK",      869.525f, 250.0f, 11, 5, 869.495f },
  { "AUS/NZ 915.8",         "AUS/NZ",     915.800f, 250.0f, 11, 5, 918.000f },
};
#define NUM_RADIO_PRESETS (sizeof(RADIO_PRESETS) / sizeof(RADIO_PRESETS[0]))

static float repeatFreqForBand(float freq) {
  if (freq < 500.0f) return 433.000f;
  if (freq < 900.0f) return 869.495f;
  return 918.000f;
}

static void radioSetFreqField(float mhz) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%.3f", mhz);
  lv_textarea_set_text(radio_ta[0], buf);
}

static NodePrefs* prefs() { return s_task->nodePrefs(); }

// ---------------------------------------------------------------------------
// row refresh
// ---------------------------------------------------------------------------
void settingsRefreshRows() {
  if (s_task == NULL) return;
  char buf[48];
  lv_label_set_text(name_val, prefs()->node_name);
  const char* preset = "Custom";
  for (unsigned i = 0; i < NUM_RADIO_PRESETS; i++) {
    const RadioPreset* r = &RADIO_PRESETS[i];
    if (fabsf(prefs()->freq - r->freq) < 0.001f && fabsf((float)prefs()->bw - r->bw) < 0.1f &&
        prefs()->sf == r->sf && prefs()->cr == r->cr) { preset = r->short_label; break; }
  }
  if (prefs()->isRepeatEn()) {
    char rbuf[64];
    snprintf(rbuf, sizeof(rbuf), "%s + repeat", preset);   // e.g. "US narrow + repeat"
    lv_label_set_text(radio_val, rbuf);
  } else {
    lv_label_set_text(radio_val, preset);   // full parameters shown in the dialog
  }
  snprintf(buf, sizeof(buf), "%d dBm", prefs()->tx_power_dbm);
  lv_label_set_text(tx_val, buf);
  if (tx_minus_btn != NULL && tx_plus_btn != NULL) {
    bool at_min = prefs()->tx_power_dbm <= TX_MIN;
    bool at_max = prefs()->tx_power_dbm >= TX_MAX;
    if (at_min) lv_obj_add_state(tx_minus_btn, LV_STATE_DISABLED);
    else lv_obj_remove_state(tx_minus_btn, LV_STATE_DISABLED);
    if (at_max) lv_obj_add_state(tx_plus_btn, LV_STATE_DISABLED);
    else lv_obj_remove_state(tx_plus_btn, LV_STATE_DISABLED);
  }
  snprintf(buf, sizeof(buf), "%u", (unsigned)prefs()->ble_pin);
  lv_label_set_text(pin_val, prefs()->ble_pin ? buf : "(random)");
  snprintf(buf, sizeof(buf), "UTC%+d", tz_offset);
  lv_label_set_text(tz_val, buf);
}

// ---------------------------------------------------------------------------
// single-field editor
// ---------------------------------------------------------------------------
static void editOpen(EditMode mode) {
  edit_mode = mode;
  if (mode == EDIT_NAME) {
    lv_label_set_text(edit_title, "Node name");
    lv_textarea_set_text(edit_ta, prefs()->node_name);
    lv_keyboard_set_mode(edit_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  } else {
    lv_label_set_text(edit_title, "Bluetooth PIN (6 digits, next boot)");
    lv_textarea_set_text(edit_ta, "");
    lv_keyboard_set_mode(edit_kb, LV_KEYBOARD_MODE_NUMBER);
  }
  lv_obj_remove_flag(edit_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(edit_kb, LV_OBJ_FLAG_HIDDEN);
}

static void editSave() {
  const char* txt = lv_textarea_get_text(edit_ta);
  if (edit_mode == EDIT_NAME) {
    if (txt == NULL || txt[0] == 0) {
      s_task->showToast("Name can't be empty");
      return;
    }
    StrHelper::strncpy(prefs()->node_name, txt, sizeof(prefs()->node_name));
    the_mesh.savePrefs();
    s_task->nodeNameChanged();
    s_task->showToast("Name saved");
  } else {
    uint32_t pin = (uint32_t) strtoul(txt, NULL, 10);
    if (pin < 100000 || pin > 999999) {
      s_task->showToast("Pin must be 6 digits");
      return;
    }
    prefs()->ble_pin = pin;
    the_mesh.savePrefs();
    s_task->showToast("Pin saved - takes effect on reboot");
  }
  lv_obj_add_flag(edit_scr, LV_OBJ_FLAG_HIDDEN);
  settingsRefreshRows();
}

static void edit_save_cb(lv_event_t* e) { editSave(); }
static void edit_cancel_cb(lv_event_t* e) { lv_obj_add_flag(edit_scr, LV_OBJ_FLAG_HIDDEN); }
static void edit_ta_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    lv_obj_remove_flag(edit_kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_READY) {
    editSave();
  }
}

static void name_row_cb(lv_event_t* e) { editOpen(EDIT_NAME); }
static void pin_row_cb(lv_event_t* e) { editOpen(EDIT_PIN); }

// ---------------------------------------------------------------------------
// TX power stepper (applies immediately - safe, reversible)
// ---------------------------------------------------------------------------
static void tx_step_cb(lv_event_t* e) {
  int dir = (int)(intptr_t) lv_event_get_user_data(e);
  int p = prefs()->tx_power_dbm + dir;
  if (p < TX_MIN || p > TX_MAX) return;
  prefs()->tx_power_dbm = p;
  the_mesh.savePrefs();
  radio_driver.setTxPower(p);
  settingsRefreshRows();
}

// ---------------------------------------------------------------------------
// radio params editor
// ---------------------------------------------------------------------------
static void radioDisarm() {
  radio_confirm_armed = false;
  lv_label_set_text(radio_apply_lbl, "Apply");
  lv_obj_set_style_bg_color(radio_apply_btn, lv_color_hex(S_COL_ACCENT), 0);
}

static void radioOpen(lv_event_t* e) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%.3f", prefs()->freq);
  lv_textarea_set_text(radio_ta[0], buf);
  snprintf(buf, sizeof(buf), "%g", (double)prefs()->bw);
  lv_textarea_set_text(radio_ta[1], buf);
  snprintf(buf, sizeof(buf), "%d", prefs()->sf);
  lv_textarea_set_text(radio_ta[2], buf);
  snprintf(buf, sizeof(buf), "%d", prefs()->cr);
  lv_textarea_set_text(radio_ta[3], buf);
  lv_dropdown_set_selected(radio_preset_dd, 0);   // "(presets)" placeholder
  if (prefs()->isRepeatEn()) lv_obj_add_state(radio_repeat_sw, LV_STATE_CHECKED);
  else lv_obj_remove_state(radio_repeat_sw, LV_STATE_CHECKED);
  radioDisarm();
  lv_obj_add_flag(radio_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(radio_scr, LV_OBJ_FLAG_HIDDEN);
}

// repeat mode has its own frequency: move the field with the switch
static void radio_repeat_cb(lv_event_t* e) {
  float freq = atof(lv_textarea_get_text(radio_ta[0]));
  bool on = lv_obj_has_state(radio_repeat_sw, LV_STATE_CHECKED);
  if (on) {
    if (!the_mesh.isValidClientRepeatFreq((uint32_t)(freq * 1000.0f))) {
      float rf = repeatFreqForBand(freq);
      radioSetFreqField(rf);
      char msg[56];
      snprintf(msg, sizeof(msg), "Repeat mode uses %.3f MHz", rf);
      s_task->showToast(msg);
    }
  } else if (the_mesh.isValidClientRepeatFreq((uint32_t)(freq * 1000.0f))) {
    for (unsigned i = 0; i < NUM_RADIO_PRESETS; i++) {
      if (fabsf(RADIO_PRESETS[i].repeat_freq - freq) < 0.001f) {
        radioSetFreqField(RADIO_PRESETS[i].freq);
        break;
      }
    }
  }
  radioDisarm();
}

static void radio_preset_cb(lv_event_t* e) {
  int sel = (int) lv_dropdown_get_selected(radio_preset_dd);
  if (sel < 1 || sel > (int)NUM_RADIO_PRESETS) return;   // index 0 = placeholder
  const RadioPreset* p = &RADIO_PRESETS[sel - 1];
  char buf[20];
  radioSetFreqField(lv_obj_has_state(radio_repeat_sw, LV_STATE_CHECKED) ? p->repeat_freq : p->freq);
  snprintf(buf, sizeof(buf), "%g", (double)p->bw);
  lv_textarea_set_text(radio_ta[1], buf);
  snprintf(buf, sizeof(buf), "%d", p->sf);
  lv_textarea_set_text(radio_ta[2], buf);
  snprintf(buf, sizeof(buf), "%d", p->cr);
  lv_textarea_set_text(radio_ta[3], buf);
  radioDisarm();
}

static void radio_apply_cb(lv_event_t* e) {
  float freq = atof(lv_textarea_get_text(radio_ta[0]));
  float bw = atof(lv_textarea_get_text(radio_ta[1]));
  int sf = atoi(lv_textarea_get_text(radio_ta[2]));
  int cr = atoi(lv_textarea_get_text(radio_ta[3]));

  // same bounds the phone command path enforces (freq narrowed to SX1262 range)
  if (freq < 400.0f || freq > 960.0f || bw < 7.0f || bw > 500.0f ||
      sf < 5 || sf > 12 || cr < 5 || cr > 8) {
    s_task->showToast("Out of range - check values");
    radioDisarm();
    return;
  }

  bool repeat_en = lv_obj_has_state(radio_repeat_sw, LV_STATE_CHECKED);
  if (repeat_en && !the_mesh.isValidClientRepeatFreq((uint32_t)(freq * 1000.0f))) {
    s_task->showToast("Repeat mode needs 433.000, 869.495 or 918.000 MHz");
    radioDisarm();
    return;
  }

  if (!radio_confirm_armed) {   // two-tap confirm: mismatched nodes fall off-mesh
    radio_confirm_armed = true;
    lv_label_set_text(radio_apply_lbl, "CONFIRM?");
    lv_obj_set_style_bg_color(radio_apply_btn, lv_color_hex(0xFFA030), 0);
    return;
  }

  prefs()->freq = freq;
  prefs()->bw = bw;
  prefs()->sf = sf;
  prefs()->cr = cr;
  prefs()->setRepeatEn(repeat_en);
  the_mesh.savePrefs();
  radio_driver.setParams(prefs()->freq, prefs()->bw, prefs()->sf, prefs()->cr);

  lv_obj_add_flag(radio_scr, LV_OBJ_FLAG_HIDDEN);
  settingsRefreshRows();
  s_task->refreshNodeTab();
  s_task->showToast("Radio settings applied");
}

static void radio_cancel_cb(lv_event_t* e) { lv_obj_add_flag(radio_scr, LV_OBJ_FLAG_HIDDEN); }

static void radio_ta_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* ta = (lv_obj_t*) lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    lv_keyboard_set_textarea(radio_kb, ta);
    lv_obj_remove_flag(radio_kb, LV_OBJ_FLAG_HIDDEN);
    radioDisarm();
  } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(radio_kb, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---------------------------------------------------------------------------
// timezone offset (persisted in its own SPIFFS file)
// ---------------------------------------------------------------------------
int settingsTzOffset() { return tz_offset; }

static void tzLoad() {
  File f = SPIFFS.open("/tzoff", "r");
  if (f) {
    int8_t v = 0;
    if (f.read((uint8_t*)&v, 1) == 1 && v >= -12 && v <= 14) tz_offset = v;
    f.close();
  }
}

static void tzSave() {
  File f = SPIFFS.open("/tzoff", "w");
  if (f) {
    int8_t v = (int8_t)tz_offset;
    f.write((uint8_t*)&v, 1);
    f.close();
  }
}

void settingsSetTzOffset(int hours) {
  if (hours < -12 || hours > 14) return;
  tz_offset = hours;
  tzSave();
  settingsRefreshRows();
}

int settingsPresetCount() { return (int) NUM_RADIO_PRESETS; }
const char* settingsPresetLabel(int idx) {
  return (idx >= 0 && idx < (int) NUM_RADIO_PRESETS) ? RADIO_PRESETS[idx].label : "";
}
void settingsApplyPreset(int idx) {
  if (idx < 0 || idx >= (int) NUM_RADIO_PRESETS) return;
  const RadioPreset* r = &RADIO_PRESETS[idx];
  prefs()->freq = r->freq;
  prefs()->bw = r->bw;
  prefs()->sf = r->sf;
  prefs()->cr = r->cr;
  the_mesh.savePrefs();
  radio_driver.setParams(prefs()->freq, prefs()->bw, prefs()->sf, prefs()->cr);
  settingsRefreshRows();
}

static void tz_step_cb(lv_event_t* e) {
  int dir = (int)(intptr_t) lv_event_get_user_data(e);
  int v = tz_offset + dir;
  if (v < -12 || v > 14) return;
  tz_offset = v;
  tzSave();
  settingsRefreshRows();
}

// ---------------------------------------------------------------------------
// node backup / restore / factory reset
// ---------------------------------------------------------------------------
#define BACKUP_DIR "/l2_backup"
static bool restore_armed = false;
static bool factory_armed = false;
static lv_obj_t* restore_lbl;
static lv_obj_t* factory_lbl;

static bool sdReady() { return SD_MMC.cardType() != CARD_NONE && SD_MMC.cardType() != CARD_UNKNOWN; }

static int copyAll(fs::FS& src_fs, const char* src_dir, fs::FS& dst_fs, const char* dst_dir) {
  File root = src_fs.open(src_dir);
  if (!root || !root.isDirectory()) return -1;
  int copied = 0;
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      const char* name = strrchr(f.name(), '/');
      name = name ? name + 1 : f.name();
      char dst_path[96];
      snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, name);
      File out = dst_fs.open(dst_path, "w");
      if (out) {
        uint8_t buf[512];
        int n;
        while ((n = f.read(buf, sizeof(buf))) > 0) out.write(buf, n);
        out.close();
        copied++;
      }
    }
    f = root.openNextFile();
  }
  return copied;
}

static void backup_cb(lv_event_t* e) {
  if (!sdReady()) { s_task->showToast("No SD card"); return; }
  SD_MMC.mkdir(BACKUP_DIR);
  int n = copyAll(SPIFFS, "/", SD_MMC, BACKUP_DIR);
  char buf[48];
  snprintf(buf, sizeof(buf), n >= 0 ? "Backed up %d files to SD" : "Backup failed", n);
  s_task->showToast(buf);
}

static void restore_cb(lv_event_t* e) {
  if (!sdReady()) { s_task->showToast("No SD card"); return; }
  if (!restore_armed) {
    restore_armed = true;
    lv_label_set_text(restore_lbl, "SURE? (overwrites node)");
    return;
  }
  int n = copyAll(SD_MMC, BACKUP_DIR, SPIFFS, "");
  if (n > 0) {
    s_task->showToast("Restored - rebooting");
    lv_refr_now(NULL);
    delay(1500);
    s_task->shutdown(true);
  } else {
    s_task->showToast("No backup found on SD");
    restore_armed = false;
    lv_label_set_text(restore_lbl, LV_SYMBOL_DOWNLOAD " Restore from SD");
  }
}

static void factory_cb(lv_event_t* e) {
  if (!factory_armed) {
    factory_armed = true;
    lv_label_set_text(factory_lbl, "SURE? (wipes identity)");
    return;
  }
  SPIFFS.format();
  s_task->showToast("Factory reset - rebooting");
  lv_refr_now(NULL);
  delay(1500);
  s_task->shutdown(true);
}

// ---------------------------------------------------------------------------
// builders
// ---------------------------------------------------------------------------
static lv_obj_t* makeRow(lv_obj_t* parent, const char* title, lv_event_cb_t cb, lv_obj_t** val_out) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(row, lv_color_hex(S_COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  if (cb != NULL) {
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
  }
  lv_obj_t* lbl = lv_label_create(row);
  lv_label_set_text(lbl, title);
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_t* val = lv_label_create(row);
  lv_label_set_text(val, "");
  lv_obj_set_style_text_color(val, lv_color_hex(S_COL_MUTED), 0);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
  // bounded so long values elide instead of running into the title
  lv_obj_set_width(val, 168);
  lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(val, LV_ALIGN_RIGHT_MID, -4, 0);
  *val_out = val;
  return row;
}

static lv_obj_t* makeDialog(const char* none) {
  lv_obj_t* scr = lv_obj_create(lv_layer_top());
  lv_obj_set_size(scr, 320, 240);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A1020), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 8, 0);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

static void styleTa(lv_obj_t* ta) {
  lv_obj_set_height(ta, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(ta, 5, 0);
  lv_obj_set_scroll_dir(ta, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_obj_set_style_opa(ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
}

static lv_obj_t* makeBtn(lv_obj_t* parent, const char* txt, lv_event_cb_t cb, void* ud,
                         lv_align_t align, int xo, int yo, int w) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_size(btn, w, 32);
  lv_obj_align(btn, align, xo, yo);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
  lv_obj_t* lbl = lv_label_create(btn);
  lv_label_set_text(lbl, txt);
  lv_obj_center(lbl);
  return btn;
}

void settingsBuildExtras(lv_obj_t* parent, UITask* task) {
  s_task = task;

  makeRow(parent, LV_SYMBOL_EDIT " Node name", name_row_cb, &name_val);
  makeRow(parent, LV_SYMBOL_WIFI " Radio settings", radioOpen, &radio_val);

  lv_obj_t* tx_row = lv_obj_create(parent);
  lv_obj_set_size(tx_row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(tx_row, lv_color_hex(S_COL_CARD), 0);
  lv_obj_set_style_border_width(tx_row, 0, 0);
  lv_obj_remove_flag(tx_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* tx_lbl = lv_label_create(tx_row);
  lv_label_set_text(tx_lbl, LV_SYMBOL_CHARGE " TX power");
  lv_obj_align(tx_lbl, LV_ALIGN_LEFT_MID, 4, 0);
  tx_minus_btn = lv_button_create(tx_row);
  lv_obj_set_size(tx_minus_btn, 30, 26);
  lv_obj_align(tx_minus_btn, LV_ALIGN_RIGHT_MID, -108, 0);
  lv_obj_add_event_cb(tx_minus_btn, tx_step_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);
  lv_obj_t* ml = lv_label_create(tx_minus_btn); lv_label_set_text(ml, LV_SYMBOL_MINUS); lv_obj_center(ml);
  tx_val = lv_label_create(tx_row);
  lv_label_set_text(tx_val, "");
  lv_obj_set_width(tx_val, 62);
  lv_obj_set_style_text_align(tx_val, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(tx_val, LV_ALIGN_RIGHT_MID, -40, 0);
  tx_plus_btn = lv_button_create(tx_row);
  lv_obj_set_size(tx_plus_btn, 30, 26);
  lv_obj_align(tx_plus_btn, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(tx_plus_btn, tx_step_cb, LV_EVENT_CLICKED, (void*)(intptr_t)+1);
  lv_obj_t* pl = lv_label_create(tx_plus_btn); lv_label_set_text(pl, LV_SYMBOL_PLUS); lv_obj_center(pl);

#ifndef STANDALONE_NO_BT
  makeRow(parent, LV_SYMBOL_BLUETOOTH " Bluetooth PIN", pin_row_cb, &pin_val);
#else
  pin_val = lv_label_create(parent);   // keep the refresh path valid; never shown
  lv_obj_add_flag(pin_val, LV_OBJ_FLAG_HIDDEN);
#endif

  tzLoad();
  lv_obj_t* tz_row = lv_obj_create(parent);
  lv_obj_set_size(tz_row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(tz_row, lv_color_hex(S_COL_CARD), 0);
  lv_obj_set_style_border_width(tz_row, 0, 0);
  lv_obj_remove_flag(tz_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* tz_lbl = lv_label_create(tz_row);
  lv_label_set_text(tz_lbl, LV_SYMBOL_REFRESH " Timezone");
  lv_obj_align(tz_lbl, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_t* tzm = lv_button_create(tz_row);
  lv_obj_set_size(tzm, 30, 26);
  lv_obj_align(tzm, LV_ALIGN_RIGHT_MID, -100, 0);
  lv_obj_add_event_cb(tzm, tz_step_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);
  lv_obj_t* tzml = lv_label_create(tzm); lv_label_set_text(tzml, LV_SYMBOL_MINUS); lv_obj_center(tzml);
  tz_val = lv_label_create(tz_row);
  lv_label_set_text(tz_val, "");
  lv_obj_align(tz_val, LV_ALIGN_RIGHT_MID, -44, 0);
  lv_obj_t* tzp = lv_button_create(tz_row);
  lv_obj_set_size(tzp, 30, 26);
  lv_obj_align(tzp, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(tzp, tz_step_cb, LV_EVENT_CLICKED, (void*)(intptr_t)+1);
  lv_obj_t* tzpl = lv_label_create(tzp); lv_label_set_text(tzpl, LV_SYMBOL_PLUS); lv_obj_center(tzpl);

  lv_obj_t* bkp_btn = lv_button_create(parent);
  lv_obj_set_size(bkp_btn, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(bkp_btn, lv_color_hex(S_COL_CARD), 0);
  lv_obj_add_event_cb(bkp_btn, backup_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* bkp_lbl = lv_label_create(bkp_btn);
  lv_label_set_text(bkp_lbl, LV_SYMBOL_SD_CARD " Backup node to SD");
  lv_obj_center(bkp_lbl);

  lv_obj_t* rst_btn = lv_button_create(parent);
  lv_obj_set_size(rst_btn, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(rst_btn, lv_color_hex(S_COL_CARD), 0);
  lv_obj_add_event_cb(rst_btn, restore_cb, LV_EVENT_CLICKED, NULL);
  restore_lbl = lv_label_create(rst_btn);
  lv_label_set_text(restore_lbl, LV_SYMBOL_DOWNLOAD " Restore from SD");
  lv_obj_center(restore_lbl);

  lv_obj_t* fct_btn = lv_button_create(parent);
  lv_obj_set_size(fct_btn, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(fct_btn, lv_color_hex(0x7A2020), 0);
  lv_obj_add_event_cb(fct_btn, factory_cb, LV_EVENT_CLICKED, NULL);
  factory_lbl = lv_label_create(fct_btn);
  lv_label_set_text(factory_lbl, LV_SYMBOL_WARNING " Factory reset");
  lv_obj_center(factory_lbl);

  edit_scr = makeDialog(NULL);
  edit_title = lv_label_create(edit_scr);
  lv_label_set_text(edit_title, "");
  lv_obj_set_style_text_color(edit_title, lv_color_hex(S_COL_ACCENT), 0);
  lv_obj_align(edit_title, LV_ALIGN_TOP_LEFT, 0, 0);
  edit_ta = lv_textarea_create(edit_scr);
  lv_textarea_set_one_line(edit_ta, true);
  lv_textarea_set_max_length(edit_ta, 30);
  lv_obj_set_width(edit_ta, 300);
  styleTa(edit_ta);
  lv_obj_align(edit_ta, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_add_event_cb(edit_ta, edit_ta_cb, LV_EVENT_ALL, NULL);
  // in the title row, clear of the keyboard
  makeBtn(edit_scr, LV_SYMBOL_OK, edit_save_cb, NULL, LV_ALIGN_TOP_RIGHT, -78, 0, 72);
  makeBtn(edit_scr, LV_SYMBOL_CLOSE, edit_cancel_cb, NULL, LV_ALIGN_TOP_RIGHT, -2, 0, 72);
  edit_kb = lv_keyboard_create(edit_scr);
  lv_keyboard_set_textarea(edit_kb, edit_ta);
  kbAttachShiftBehavior(edit_kb);
  lv_obj_set_size(edit_kb, 320, S_KB_H);
  lv_obj_align(edit_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(edit_kb, LV_OBJ_FLAG_HIDDEN);

  radio_scr = makeDialog(NULL);
  lv_obj_t* rt = lv_label_create(radio_scr);
  lv_label_set_text(rt, "Radio settings");
  lv_obj_set_style_text_color(rt, lv_color_hex(S_COL_ACCENT), 0);
  lv_obj_set_style_text_font(rt, &lv_font_montserrat_16, 0);
  lv_obj_align(rt, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t* rh = lv_label_create(radio_scr);
  lv_label_set_text(rh, "Every node on your mesh must use the same values.");
  lv_obj_set_style_text_font(rh, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(rh, lv_color_hex(S_COL_MUTED), 0);
  lv_obj_align(rh, LV_ALIGN_TOP_LEFT, 0, 20);

  static const char* labels[4] = {"Frequency MHz", "Bandwidth kHz", "Spread factor", "Coding rate"};
  for (int i = 0; i < 4; i++) {
    int x = i * 76;
    lv_obj_t* l = lv_label_create(radio_scr);
    lv_label_set_text(l, labels[i]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(S_COL_MUTED), 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, x + 1, 40);
    radio_ta[i] = lv_textarea_create(radio_scr);
    lv_textarea_set_one_line(radio_ta[i], true);
    lv_textarea_set_max_length(radio_ta[i], 8);
    lv_obj_set_width(radio_ta[i], 72);
    styleTa(radio_ta[i]);
    lv_obj_align(radio_ta[i], LV_ALIGN_TOP_LEFT, x, 56);
    lv_obj_add_event_cb(radio_ta[i], radio_ta_cb, LV_EVENT_ALL, NULL);
  }

  radio_preset_dd = lv_dropdown_create(radio_scr);
  char dd_opts[160] = "(presets)";
  for (unsigned i = 0; i < NUM_RADIO_PRESETS; i++) {
    strlcat(dd_opts, "\n", sizeof(dd_opts));
    strlcat(dd_opts, RADIO_PRESETS[i].label, sizeof(dd_opts));
  }
  lv_dropdown_set_options(radio_preset_dd, dd_opts);
  lv_obj_set_size(radio_preset_dd, 304, 32);
  lv_obj_align(radio_preset_dd, LV_ALIGN_TOP_MID, 0, 96);
  lv_obj_add_event_cb(radio_preset_dd, radio_preset_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // repeat mode: this node forwards other nodes' packets
  lv_obj_t* rep_lbl = lv_label_create(radio_scr);
  lv_label_set_text(rep_lbl, "Repeat mode");
  lv_obj_set_style_text_color(rep_lbl, lv_color_hex(S_COL_MUTED), 0);
  lv_label_set_text(rep_lbl, "Repeat mode - this node relays for others");
  lv_obj_set_style_text_font(rep_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(rep_lbl, LV_ALIGN_TOP_LEFT, 2, 142);
  radio_repeat_sw = lv_switch_create(radio_scr);
  lv_obj_align(radio_repeat_sw, LV_ALIGN_TOP_RIGHT, -2, 134);
  lv_obj_add_event_cb(radio_repeat_sw, radio_repeat_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t* rep_hint = lv_label_create(radio_scr);
  lv_label_set_text(rep_hint, "Repeat mode runs on its own frequency\n(433.000 / 869.495 / 918.000 MHz) and is\nset here automatically.");
  lv_obj_set_style_text_font(rep_hint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(rep_hint, lv_color_hex(S_COL_MUTED), 0);
  lv_obj_set_width(rep_hint, 304);
  lv_label_set_long_mode(rep_hint, LV_LABEL_LONG_WRAP);
  lv_obj_align(rep_hint, LV_ALIGN_TOP_LEFT, 2, 162);

  radio_apply_btn = makeBtn(radio_scr, LV_SYMBOL_OK " Apply", radio_apply_cb, NULL, LV_ALIGN_BOTTOM_LEFT, 0, -2, 148);
  radio_apply_lbl = lv_obj_get_child(radio_apply_btn, 0);
  makeBtn(radio_scr, LV_SYMBOL_CLOSE " Cancel", radio_cancel_cb, NULL, LV_ALIGN_BOTTOM_RIGHT, 0, -2, 148);

  radio_kb = lv_keyboard_create(radio_scr);
  lv_keyboard_set_mode(radio_kb, LV_KEYBOARD_MODE_NUMBER);
  lv_obj_set_style_text_font(radio_kb, &lv_font_montserrat_16, LV_PART_ITEMS);
  lv_obj_set_size(radio_kb, 320, 128);   // numeric pad: keep the fields visible while typing
  lv_obj_align(radio_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(radio_kb, LV_OBJ_FLAG_HIDDEN);

  settingsRefreshRows();
}
