// Shell, tabs, settings rows, mesh events and the main loop. The thread,
// contacts, repeater and overlay screens live in the UI*.cpp files next to
// this one; UICommon.h is what they share.

#include "UICommon.h"
#include "MapView.h"
#include <esp_heap_caps.h>
#include <SD_MMC.h>
#ifndef STANDALONE_NO_BT
  #include <BLEDevice.h>
#endif

// from base64.hpp, which defines (not just declares) its functions and is
// already compiled into BaseChatMesh.cpp - re-including would double-define
unsigned int decode_base64(const unsigned char input[], unsigned int len, unsigned char output[]);

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS 60000
#endif

static void addchBuild();
static void sleepShieldBuild();

UITask* ui = NULL;   // singleton for LVGL callbacks

static void lv_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  auto gfx = (lgfx::LGFX_Device*) lv_display_get_user_data(disp);
  int w = area->x2 - area->x1 + 1;
  int h = area->y2 - area->y1 + 1;
  gfx->startWrite();
  gfx->setAddrWindow(area->x1, area->y1, w, h);
  gfx->pushPixels((uint16_t*) px_map, (uint32_t)w * h, true /* swap bytes */);
  gfx->endWrite();
  lv_display_flush_ready(disp);
}

static void lv_touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  auto gfx = (lgfx::LGFX_Device*) lv_indev_get_user_data(indev);
  lgfx::touch_point_t tp;
  if (gfx->getTouch(&tp, 1) > 0) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = tp.x;
    data->point.y = tp.y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static lv_obj_t* status_bar;
static lv_obj_t* lbl_node_name;
static lv_obj_t* lbl_status_right;
static lv_obj_t* tabview;
static lv_obj_t* tab_chats;
static lv_obj_t* tab_contacts;
static lv_obj_t* tab_map;
static lv_obj_t* tab_node;
static lv_obj_t* tab_settings;
// add-channel dialog
static lv_obj_t* addch_scr;
static lv_obj_t* addch_name_ta;
static lv_obj_t* addch_psk_ta;
static lv_obj_t* addch_kb;
static lv_obj_t* chats_list;

static lv_obj_t* node_info_lbl;

// invisible overlay that eats the wake-up tap while the display is dimmed
static lv_obj_t* sleep_shield;

// ---- small persisted comfort prefs ----
static uint32_t auto_off_ms = AUTO_OFF_MILLIS;   // 0 = never sleep
static bool clock_12h = false;
static bool ble_off_pref = false;
static bool ble_pref_applied = false;
bool units_miles = false;
static int  ble_autooff_min = 0;        // 0 = never; else minutes idle before slow advertising
static bool ble_slow = false;
static bool grove_on = true;
static lv_obj_t* ble_sw = NULL;

int prefReadInt(const char* path, int dflt) {
  File f = SPIFFS.open(path, "r");
  if (!f) return dflt;
  int v = f.parseInt();
  f.close();
  return v;
}
void prefWriteInt(const char* path, int v) {
  File f = SPIFFS.open(path, "w");
  if (f) { f.print(v); f.close(); }
}
static void polishPrefsLoad() {
  // only the dropdown values: a torn write could yield a near-zero timeout
  int off = prefReadInt("/autooff", -1);
  static const uint32_t valid_off[] = {30000, 60000, 120000, 300000, 0};
  for (uint32_t v : valid_off) {
    if (off >= 0 && (uint32_t) off == v) { auto_off_ms = v; break; }
  }
  clock_12h = prefReadInt("/clock12", 0) != 0;
  ble_off_pref = prefReadInt("/ble_off", 0) != 0;
  int ba = prefReadInt("/ble_auto", 0);
  ble_autooff_min = (ba == 10 || ba == 30 || ba == 60) ? ba : 0;
  grove_on = prefReadInt("/grove", 1) != 0;
  units_miles = prefReadInt("/units", 0) != 0;
}

// Slow advertising instead of switching the radio off, so phones can still
// reconnect. Intervals are 0.625 ms units; the defaults are 0x20/0x40.
static void bleSlowAdvertising(bool slow) {
#ifndef STANDALONE_NO_BT
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  if (adv == NULL) return;
  adv->setMinInterval(slow ? 1600 : 0x20);
  adv->setMaxInterval(slow ? 2080 : 0x40);
  adv->stop();
  adv->start();
#endif
  ble_slow = slow;
}

// nothing to render while the screen sleeps, so drop to 80 MHz
static void screenPower(bool asleep) {
  setCpuFrequencyMhz(asleep ? 80 : 240);
}

static uint32_t autoOffMs() { return auto_off_ms == 0 ? 0x7FFFFFFFu : auto_off_ms; }

// LiPo discharge curve (single cell, light load) instead of a straight line
static int battPercent(uint16_t mv) {
  static const uint16_t v[] = {4200, 4100, 4000, 3900, 3800, 3700, 3600, 3500, 3300};
  static const uint8_t  pc[] = { 100,   90,   78,   62,   45,   25,   10,    4,    0};
  if (mv >= v[0]) return 100;
  for (int i = 1; i < 9; i++) {
    if (mv >= v[i]) return pc[i] + (int)(mv - v[i]) * (pc[i-1] - pc[i]) / (v[i-1] - v[i]);
  }
  return 0;
}

// ---- backlight brightness (percent, persisted) ----
static uint8_t bright_pct = 60;
static bool bright_dimmed = false;
#define DIM_LEAD_MILLIS 15000

static uint8_t brightRaw() { return (uint8_t)((uint16_t) bright_pct * 255 / 100); }

static void brightLoad() {
  File f = SPIFFS.open("/bright", "r");
  if (f) {
    int v = f.parseInt();
    f.close();
    if (v >= 10 && v <= 100) bright_pct = (uint8_t) v;
  }
}

static void brightSave() {
  File f = SPIFFS.open("/bright", "w");
  if (f) {
    f.print((int) bright_pct);
    f.close();
  }
}

// ---- phone-style keyboard shift: one-shot upper, double-tap = caps lock ----
struct KbShiftState { bool prev_upper; bool shift_pending; bool caps; };
static KbShiftState kb_shift_states[10];
static int kb_shift_count = 0;

static void kb_shift_cb(lv_event_t* e) {
  KbShiftState* st = (KbShiftState*) lv_event_get_user_data(e);
  lv_obj_t* kb = (lv_obj_t*) lv_event_get_target(e);
  lv_keyboard_mode_t mode = lv_keyboard_get_mode(kb);
  if (mode != LV_KEYBOARD_MODE_TEXT_LOWER && mode != LV_KEYBOARD_MODE_TEXT_UPPER) {
    st->prev_upper = false;   // number/special map: shift state doesn't apply
    st->shift_pending = st->caps = false;
    return;
  }
  bool upper = mode == LV_KEYBOARD_MODE_TEXT_UPPER;

  if (upper && !st->prev_upper) {          // shift tapped
    st->shift_pending = true;
    st->caps = false;
  } else if (!upper && st->prev_upper) {   // shift tapped again while shifted
    if (st->shift_pending) {               // double-tap: engage caps lock
      st->caps = true;
      st->shift_pending = false;
      lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
      upper = true;
    } else {
      st->caps = false;
    }
  } else if (upper && st->shift_pending && !st->caps) {
    // character typed on a one-shot shift: drop back to lower case
    // (control glyphs are 3-byte UTF-8 symbols; keep shift through those)
    uint32_t id = lv_keyboard_get_selected_button(kb);
    const char* txt = lv_keyboard_get_button_text(kb, id);
    if (txt != NULL && (uint8_t) txt[0] < 0x80 && strcmp(txt, "1#") != 0) {
      st->shift_pending = false;
      lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
      upper = false;
    }
  }
  st->prev_upper = upper;
}

void kbAttachShiftBehavior(lv_obj_t* kb) {
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_16, LV_PART_ITEMS);   // bigger key labels
  if (kb_shift_count >= 10) return;
  KbShiftState* st = &kb_shift_states[kb_shift_count++];
  st->prev_upper = st->shift_pending = st->caps = false;
  lv_obj_add_event_cb(kb, kb_shift_cb, LV_EVENT_VALUE_CHANGED, st);
}

// per-row keys for the chats list (parallel to buttons, bounded)
#define MAX_THREAD_ROWS 24
static uint8_t chats_row_keys[MAX_THREAD_ROWS][6];

// channels may already carry a leading '#' in their stored name
static void channelLabel(char* dest, size_t sz, const char* name) {
  snprintf(dest, sz, "%s%s", name[0] == '#' ? "" : "#", name);
}

static void chats_row_long_cb(lv_event_t* e) {
  int row = (int)(intptr_t) lv_event_get_user_data(e);
  if (row < 0 || row >= MAX_THREAD_ROWS) return;
  int ch_idx;
  if (isChannelKey(chats_row_keys[row], &ch_idx)) ui->openChannelOptions(chats_row_keys[row]);
}

static void chats_row_cb(lv_event_t* e) {
  int row = (int)(intptr_t) lv_event_get_user_data(e);
  if (row < 0 || row >= MAX_THREAD_ROWS) return;
  const uint8_t* key = chats_row_keys[row];

  int ch_idx;
  if (isChannelKey(key, &ch_idx)) {
    ChannelDetails ch;
    if (the_mesh.getChannel(ch_idx, ch) && ch.name[0]) {
      char name[36];
      channelLabel(name, sizeof(name), ch.name);
      ui->openThread(key, name);
    }
  } else {
    ContactInfo* c = the_mesh.lookupContactByPubKey(key, 6);
    if (c != NULL && c->type != ADV_TYPE_CHAT) {
      ui->openRepeaterManager(*c);   // console threads belong to the manager
      return;
    }
    ui->openThread(key, c ? c->name : "(unknown)");
  }
}

// ---- add-channel dialog ----
static void addch_open_cb(lv_event_t* e) {
  lv_textarea_set_text(addch_name_ta, "");
  lv_textarea_set_text(addch_psk_ta, "");
  lv_obj_add_flag(addch_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(addch_scr, LV_OBJ_FLAG_HIDDEN);
}

static void addch_cancel_cb(lv_event_t* e) {
  lv_keyboard_set_textarea(addch_kb, NULL);
  lv_obj_add_flag(addch_scr, LV_OBJ_FLAG_HIDDEN);
}

static void addch_ta_focus_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* ta = (lv_obj_t*) lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    lv_keyboard_set_textarea(addch_kb, ta);
    lv_obj_remove_flag(addch_kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(addch_kb, LV_OBJ_FLAG_HIDDEN);
  }
}

static void addch_create_cb(lv_event_t* e) {
  const char* typed = lv_textarea_get_text(addch_name_ta);
  while (*typed == '#') typed++;
  if (typed[0] == 0) {
    ui->showToast("Channel needs a name");
    return;
  }
  const char* psk = lv_textarea_get_text(addch_psk_ta);

  ChannelDetails ch;
  memset(&ch, 0, sizeof(ch));
  int keylen;
  if (psk == NULL || psk[0] == 0) {
    // hashtag channel: the key is the first 16 bytes of sha256("#name"), the
    // same rule the app uses, so anyone who knows the name can join
    snprintf(ch.name, sizeof(ch.name), "#%s", typed);
    keylen = 16;
    mesh::Utils::sha256(ch.channel.secret, keylen, (const uint8_t*) ch.name, strlen(ch.name));
  } else {
    // private channel: caller supplies the key, name stored as typed
    keylen = decode_base64((const unsigned char*)psk, strlen(psk), ch.channel.secret);
    if (keylen != 16 && keylen != 32) {
      ui->showToast("PSK must be 16/32 bytes b64");
      return;
    }
    StrHelper::strncpy(ch.name, typed, sizeof(ch.name));
  }
  mesh::Utils::sha256(ch.channel.hash, sizeof(ch.channel.hash), ch.channel.secret, keylen);

  // NOTE: BaseChatMesh::addChannel writes at its own counter which ignores
  // flash-loaded channels (it would overwrite slot 0) - place the channel
  // into the first truly empty slot ourselves
  int slot = -1;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails t;
    if (!the_mesh.getChannel(i, t)) break;
    const char* existing = t.name[0] == '#' ? t.name + 1 : t.name;
    if (t.name[0] != 0 && strcmp(existing, typed) == 0) {
      ui->showToast("Channel already exists");
      return;
    }
    if (t.name[0] == 0 && slot < 0) slot = i;
  }
  if (slot < 0) {
    ui->showToast("Channel table full");
    return;
  }

  if (!the_mesh.setChannel(slot, ch)) {
    ui->showToast("Channel add failed");
    return;
  }
  the_mesh.saveChannels();
  lv_keyboard_set_textarea(addch_kb, NULL);
  lv_obj_add_flag(addch_scr, LV_OBJ_FLAG_HIDDEN);
  ui->refreshChatsTab();
  ui->showToast("Channel added");
}

static void sleep_shield_cb(lv_event_t* e) {
  lv_obj_add_flag(sleep_shield, LV_OBJ_FLAG_HIDDEN);
}

static void tabview_changed_cb(lv_event_t* e) {
  ui->refreshChatsTab();
  ui->refreshContactsTab();
  ui->refreshNodeTab();
  mapViewRefresh();
  settingsRefreshRows();
}

static void advert_btn_cb(lv_event_t* e) {
  bool flood = (intptr_t) lv_event_get_user_data(e) == 1;
  bool ok = the_mesh.advert(flood);
  ui->showToast(ok ? (flood ? "Flood advert sent" : "Zero-hop advert sent") : "Advert failed");
}

static void reboot_btn_cb(lv_event_t* e) { ui->shutdown(true); }

static void sound_switch_cb(lv_event_t* e) {
  auto sw = (lv_obj_t*) lv_event_get_target(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  ui->nodePrefs()->buzzer_quiet = on ? 0 : 1;
  the_mesh.savePrefs();
  if (on) soundAckTone();   // audible confirmation
  ui->showToast(on ? "Sounds on" : "Sounds off");
}

// L76K (CASIC) GNSS: enable GPS + BeiDou + GLONASS so acquisition has more
// satellites to work with (board default is GPS + BeiDou only). Volatile
// setting; resent on every GPS start.

static File gps_log;
static uint32_t gps_log_lines = 0;
static void gpsLogLine(const char* line) {
  if (!gps_log) {
    if (SD_MMC.cardType() == CARD_NONE || SD_MMC.cardType() == CARD_UNKNOWN) return;
    gps_log = SD_MMC.open("/gps_nmea.log", FILE_APPEND);
    if (!gps_log) return;
    if (gps_log.size() > 2000000) {
      gps_log.close();
      gps_log = SD_MMC.open("/gps_nmea.log", FILE_WRITE);
      if (!gps_log) return;
    }
    gps_log.printf("--- boot +%lums ---\n", millis());
  }
  gps_log.println(line);
  if (++gps_log_lines % 25 == 0) gps_log.flush();
}

static void gpsEnableAllConstellations() {
  Serial1.print("$PCAS04,7*1E\r\n");
}

// The L76K can track strong satellites for many minutes without producing a
// fix, and recovers from a reset. Pulse GNSS reset after 3 minutes of >=4
// strong satellites with no fix, then re-send the constellation config.
static int gps_kicks = 0;
static unsigned long gps_stuck_since = 0;
static unsigned long gps_next_kick = 0;
static bool gps_reconfig_pending = false;

static void gpsWatchdogTick() {
  if (gps_reconfig_pending) {
    gpsEnableAllConstellations();
    gps_reconfig_pending = false;
  }
  bool tracking = gps_tap.streaming() && gps_tap.strongSats() >= 4;
  bool fixed = gps_tap.ggaFix() > 0 || gps_tap.rmcStatus() == 'A';
  if (fixed) { gps_stuck_since = 0; gps_kicks = 0; return; }
  if (!tracking) { gps_stuck_since = 0; return; }
  if (gps_stuck_since == 0) { gps_stuck_since = millis(); return; }
  if (millis() - gps_stuck_since > 180000 && millis() > gps_next_kick) {
    MESH_DEBUG_PRINTLN("gps watchdog: %d strong sats, no fix - GNSS reset #%d",
                       gps_tap.strongSats(), gps_kicks + 1);
    board.gnssReset();
    gps_reconfig_pending = true;
    gps_kicks++;
    gps_next_kick = millis() + 180000;
    gps_stuck_since = 0;
  }
}

static void gps_switch_cb(lv_event_t* e) {
  auto sw = (lv_obj_t*) lv_event_get_target(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  SensorManager* sensors = ui->sensors();
  if (sensors) {
    sensors->setSettingValue("gps", on ? "1" : "0");
    ui->nodePrefs()->gps_enabled = on ? 1 : 0;
    board.setGnssPower(on);              // really off, not just ignored
    if (on) gps_reconfig_pending = true;  // constellations re-sent once it has booted
    the_mesh.savePrefs();
    ui->showToast(on ? "GPS enabled" : "GPS disabled");
  }
}

void UITask::begin(DisplayDriver* display_drv, SensorManager* sensors, NodePrefs* node_prefs) {
  ui = this;
  _sensors = sensors;
  _node_prefs = node_prefs;

  chatStoreLoad();
  repeaterStateLoad();
  qrLoad();
  brightLoad();
  polishPrefsLoad();
  soundLoadTonePref();
  gps_tap.echo = false;               // set true to mirror raw NMEA to the console
  gps_tap.on_line = gpsLogLine;       // raw NMEA log on the SD card
  soundSetAmpControl([](bool on) { board.setSpeakerAmp(on); });
  soundInit();   // claim I2S DMA memory BEFORE LVGL takes its share

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return (uint32_t) millis(); });

  auto gfx = display.lgfxDevice();   // concrete display from target.h

  lv_display_t* disp = lv_display_create(320, 240);
  lv_display_set_user_data(disp, gfx);
  lv_display_set_flush_cb(disp, lv_flush_cb);

  const uint32_t buf_sz = 320 * 60 * 2;   // 60-line partial buffers
  uint8_t* buf1 = (uint8_t*) heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
  uint8_t* buf2 = (uint8_t*) heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
  if (buf1 == NULL) {   // PSRAM exhausted: fall back to internal RAM, single buffer
    buf1 = (uint8_t*) heap_caps_malloc(buf_sz, MALLOC_CAP_8BIT);
    buf2 = NULL;
  }
  if (buf1 == NULL) {
    Serial.println("FATAL: no memory for LVGL framebuffers");
    while (true) { delay(1000); }   // halt visibly rather than crash on flush
  }
  lv_display_set_buffers(disp, buf1, buf2, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(indev, gfx);
  lv_indev_set_read_cb(indev, lv_touch_cb);

  lv_theme_default_init(disp, lv_color_hex(COL_ACCENT), lv_color_hex(COL_ACCENT_D),
                        true /* dark */, &fa_icons_14);

  buildShell();
  buildRepeaterScr();
  addchBuild();
  buildContactOverlays();
  sleepShieldBuild();
  buildPolishOverlays();   // wizard / about / banner / channel options
  buildSplash();
  display.lgfxDevice()->setBrightness(brightRaw());   // apply stored brightness
  refreshChatsTab();
  refreshContactsTab();
  refreshNodeTab();

  // the saved GPS preference is never auto-applied at boot (gps_active
  // starts false regardless), so re-apply it here
  if (_node_prefs != NULL && _node_prefs->gps_enabled && _sensors != NULL) {
    _sensors->setSettingValue("gps", "1");
    gpsEnableAllConstellations();
  } else {
    board.setGnssPower(false);   // GPS disabled: cut the receiver's rail
  }
  board.setGrovePower(grove_on);
}

// add-channel dialog, reached from the Chats tab
static void addchBuild() {
  addch_scr = lv_obj_create(lv_screen_active());
  lv_obj_set_size(addch_scr, 320, 240);
  lv_obj_align(addch_scr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(addch_scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(addch_scr, 0, 0);
  lv_obj_set_style_radius(addch_scr, 0, 0);
  lv_obj_set_style_pad_all(addch_scr, 8, 0);
  lv_obj_add_flag(addch_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(addch_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* addch_title = lv_label_create(addch_scr);
  lv_label_set_text(addch_title, "New channel");
  lv_obj_set_style_text_color(addch_title, lv_color_hex(COL_ACCENT), 0);
  lv_obj_align(addch_title, LV_ALIGN_TOP_LEFT, 0, 0);

  addch_name_ta = lv_textarea_create(addch_scr);
  lv_textarea_set_one_line(addch_name_ta, true);
  lv_textarea_set_placeholder_text(addch_name_ta, "Channel name");
  lv_textarea_set_max_length(addch_name_ta, 30);
  lv_obj_set_width(addch_name_ta, 300);
  lv_obj_set_height(addch_name_ta, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(addch_name_ta, 5, 0);
  lv_obj_set_scroll_dir(addch_name_ta, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(addch_name_ta, LV_SCROLLBAR_MODE_OFF);
  lv_obj_align(addch_name_ta, LV_ALIGN_TOP_MID, 0, 16);
  lv_obj_add_event_cb(addch_name_ta, addch_ta_focus_cb, LV_EVENT_ALL, NULL);

  addch_psk_ta = lv_textarea_create(addch_scr);
  lv_textarea_set_one_line(addch_psk_ta, true);
  lv_textarea_set_placeholder_text(addch_psk_ta, "PSK base64 (blank = joinable #name)");
  lv_textarea_set_max_length(addch_psk_ta, 44);
  lv_obj_set_width(addch_psk_ta, 300);
  lv_obj_set_height(addch_psk_ta, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(addch_psk_ta, 5, 0);
  lv_obj_set_scroll_dir(addch_psk_ta, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(addch_psk_ta, LV_SCROLLBAR_MODE_OFF);
  lv_obj_align(addch_psk_ta, LV_ALIGN_TOP_MID, 0, 46);
  lv_obj_add_event_cb(addch_psk_ta, addch_ta_focus_cb, LV_EVENT_ALL, NULL);

  lv_obj_t* create_btn = lv_button_create(addch_scr);
  lv_obj_set_size(create_btn, 140, 32);
  lv_obj_align(create_btn, LV_ALIGN_TOP_LEFT, 4, 90);
  lv_obj_add_event_cb(create_btn, addch_create_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* cl = lv_label_create(create_btn);
  lv_label_set_text(cl, LV_SYMBOL_OK " Create");
  lv_obj_center(cl);

  lv_obj_t* cancel_btn = lv_button_create(addch_scr);
  lv_obj_set_size(cancel_btn, 140, 32);
  lv_obj_align(cancel_btn, LV_ALIGN_TOP_RIGHT, -4, 90);
  lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(cancel_btn, addch_cancel_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* xl = lv_label_create(cancel_btn);
  lv_label_set_text(xl, LV_SYMBOL_CLOSE " Cancel");
  lv_obj_center(xl);

  addch_kb = lv_keyboard_create(addch_scr);
  kbAttachShiftBehavior(addch_kb);
  lv_obj_set_size(addch_kb, 320, KB_HEIGHT);
  lv_obj_align(addch_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(addch_kb, LV_OBJ_FLAG_HIDDEN);
}

static void sleepShieldBuild() {
  // sleep shield: topmost, eats the first tap when the screen is dark
  sleep_shield = lv_obj_create(lv_layer_top());
  lv_obj_set_size(sleep_shield, 320, 240);
  lv_obj_set_style_bg_opa(sleep_shield, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(sleep_shield, 0, 0);
  lv_obj_add_flag(sleep_shield, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(sleep_shield, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(sleep_shield, sleep_shield_cb, LV_EVENT_CLICKED, NULL);
}

void UITask::buildShell() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_text_color(scr, lv_color_hex(COL_TXT), 0);

  // --- status bar ---
  status_bar = lv_obj_create(scr);
  lv_obj_set_size(status_bar, 320, 22);
  lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(status_bar, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(status_bar, 0, 0);
  lv_obj_set_style_radius(status_bar, 0, 0);
  lv_obj_set_style_pad_all(status_bar, 2, 0);
  lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

  lbl_node_name = lv_label_create(status_bar);
  lv_label_set_text(lbl_node_name, _node_prefs ? _node_prefs->node_name : "MeshCore");
  lv_obj_set_style_text_color(lbl_node_name, lv_color_hex(COL_ACCENT), 0);
  lv_obj_align(lbl_node_name, LV_ALIGN_LEFT_MID, 4, 0);

  lbl_status_right = lv_label_create(status_bar);
  lv_label_set_text(lbl_status_right, "");
  lv_obj_set_style_text_color(lbl_status_right, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(lbl_status_right, LV_ALIGN_RIGHT_MID, -4, 0);

  // --- tabview ---
  tabview = lv_tabview_create(scr);
  lv_obj_set_size(tabview, 320, 240 - 22);
  lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_tabview_set_tab_bar_position(tabview, LV_DIR_BOTTOM);
  lv_tabview_set_tab_bar_size(tabview, 36);   // icons only
  lv_obj_set_style_bg_color(tabview, lv_color_hex(COL_BG), 0);

  tab_chats    = lv_tabview_add_tab(tabview, LV_SYMBOL_ENVELOPE);
  tab_contacts = lv_tabview_add_tab(tabview, SYMBOL_ADDR_BOOK);
  tab_map      = lv_tabview_add_tab(tabview, LV_SYMBOL_GPS);
  tab_settings = lv_tabview_add_tab(tabview, LV_SYMBOL_SETTINGS);
  lv_obj_add_event_cb(tabview, tabview_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
  // tab switching via the tab bar only; swipe gestures belong to the content
  // (map panning especially) and were causing accidental page swaps
  lv_obj_remove_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);

  buildChatsTab(tab_chats);
  buildContactsTab(tab_contacts);
  mapViewBuild(tab_map, _sensors, this);
  buildSettingsTab(tab_settings);
  buildThreadScr();
}

void UITask::buildChatsTab(lv_obj_t* parent) {
  lv_obj_set_style_pad_all(parent, 4, 0);
  chats_list = lv_list_create(parent);
  lv_obj_set_size(chats_list, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(chats_list, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(chats_list, 0, 0);
}

void UITask::buildNodeTab(lv_obj_t* parent) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* hdr = lv_label_create(card);
  lv_label_set_text(hdr, LV_SYMBOL_CHARGE " Telemetry");
  lv_obj_set_style_text_color(hdr, lv_color_hex(COL_ACCENT), 0);
  node_info_lbl = lv_label_create(card);
  lv_label_set_text(node_info_lbl, "");
  lv_obj_set_width(node_info_lbl, LV_PCT(100));
  lv_obj_align_to(node_info_lbl, hdr, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
}

static void tone_dd_cb(lv_event_t* e) {
  lv_obj_t* dd = (lv_obj_t*) lv_event_get_target(e);
  soundSetToneStyle((int) lv_dropdown_get_selected(dd));
  soundMessageTone();   // instant preview of the chosen alert
}

static void ble_switch_cb(lv_event_t* e) {
  bool on = lv_obj_has_state((lv_obj_t*) lv_event_get_target(e), LV_STATE_CHECKED);
  ble_off_pref = !on;
  prefWriteInt("/ble_off", on ? 0 : 1);
  if (on) ui->enableBluetooth(); else ui->disableBluetooth();
  ui->showToast(on ? "Bluetooth on" : "Bluetooth off");
}

static void ble_auto_dd_cb(lv_event_t* e) {
  static const int opts[4] = {0, 10, 30, 60};
  int sel = (int) lv_dropdown_get_selected((lv_obj_t*) lv_event_get_target(e));
  if (sel < 0 || sel > 3) return;
  ble_autooff_min = opts[sel];
  prefWriteInt("/ble_auto", ble_autooff_min);
}

static void grove_switch_cb(lv_event_t* e) {
  grove_on = lv_obj_has_state((lv_obj_t*) lv_event_get_target(e), LV_STATE_CHECKED);
  prefWriteInt("/grove", grove_on ? 1 : 0);
  board.setGrovePower(grove_on);
}

static void autooff_dd_cb(lv_event_t* e) {
  static const uint32_t opts[5] = {30000, 60000, 120000, 300000, 0};
  int sel = (int) lv_dropdown_get_selected((lv_obj_t*) lv_event_get_target(e));
  if (sel < 0 || sel > 4) return;
  auto_off_ms = opts[sel];
  prefWriteInt("/autooff", (int) auto_off_ms);
}

static void units_switch_cb(lv_event_t* e) {
  units_miles = lv_obj_has_state((lv_obj_t*) lv_event_get_target(e), LV_STATE_CHECKED);
  prefWriteInt("/units", units_miles ? 1 : 0);
}

static void clock12_switch_cb(lv_event_t* e) {
  clock_12h = lv_obj_has_state((lv_obj_t*) lv_event_get_target(e), LV_STATE_CHECKED);
  prefWriteInt("/clock12", clock_12h ? 1 : 0);
}

static void bright_slider_cb(lv_event_t* e) {
  lv_obj_t* s = (lv_obj_t*) lv_event_get_target(e);
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    bright_pct = (uint8_t) lv_slider_get_value(s);
    display.lgfxDevice()->setBrightness(brightRaw());
  } else if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    brightSave();
  }
}

static void qredit_row_cb(lv_event_t* e) { qrEditorOpen(); }
static void wizard_row_cb(lv_event_t* e) { wizardOpen(); }
static void about_row_cb(lv_event_t* e) { aboutOpen(); }

void UITask::buildSettingsTab(lv_obj_t* parent) {
  lv_obj_set_style_pad_all(parent, 8, 0);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 8, 0);

  buildNodeTab(parent);   // telemetry card first

  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* lbl = lv_label_create(row);
  lv_label_set_text(lbl, LV_SYMBOL_VOLUME_MAX " Sounds");
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -4, 0);
  if (_node_prefs && !_node_prefs->buzzer_quiet) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, sound_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

  row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 40);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lbl = lv_label_create(row);
  lv_label_set_text(lbl, LV_SYMBOL_BELL " Alert tone");
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_t* tone_dd = lv_dropdown_create(row);
  lv_obj_set_size(tone_dd, 140, 32);
  lv_obj_align(tone_dd, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_dropdown_set_options(tone_dd, "Classic\nChirp\nDing dong\nTrill\nRise\nAlarm");
  lv_dropdown_set_selected(tone_dd, soundGetToneStyle());
  lv_obj_add_event_cb(tone_dd, tone_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

  row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lbl = lv_label_create(row);
  lv_label_set_text(lbl, LV_SYMBOL_GPS " GPS");
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  sw = lv_switch_create(row);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -4, 0);
  if (_node_prefs && _node_prefs->gps_enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, gps_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

#ifndef STANDALONE_NO_BT
  row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lbl = lv_label_create(row);
  lv_label_set_text(lbl, LV_SYMBOL_BLUETOOTH " Bluetooth");
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  sw = lv_switch_create(row);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -4, 0);
  if (!ble_off_pref) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, ble_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
  ble_sw = sw;

  row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 40);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lbl = lv_label_create(row);
  lv_label_set_text(lbl, LV_SYMBOL_BLUETOOTH " BT power save when idle");
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_t* ba_dd = lv_dropdown_create(row);
  lv_obj_set_size(ba_dd, 110, 32);
  lv_obj_align(ba_dd, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_dropdown_set_options(ba_dd, "Never\n10 min\n30 min\n1 hour");
  lv_dropdown_set_selected(ba_dd, ble_autooff_min == 10 ? 1 : ble_autooff_min == 30 ? 2 : ble_autooff_min == 60 ? 3 : 0);
  lv_obj_add_event_cb(ba_dd, ble_auto_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

  row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lbl = lv_label_create(row);
  lv_label_set_text(lbl, LV_SYMBOL_USB " Grove port power");
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  sw = lv_switch_create(row);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -4, 0);
  if (grove_on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, grove_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
#else
  row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lbl = lv_label_create(row);
  lv_label_set_text(lbl, LV_SYMBOL_USB " Grove port power");
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  sw = lv_switch_create(row);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -4, 0);
  if (grove_on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, grove_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
#endif

  row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 40);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lbl = lv_label_create(row);
  lv_label_set_text(lbl, LV_SYMBOL_POWER " Screen off");
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_t* off_dd = lv_dropdown_create(row);
  lv_obj_set_size(off_dd, 120, 32);
  lv_obj_align(off_dd, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_dropdown_set_options(off_dd, "30 sec\n1 min\n2 min\n5 min\nNever");
  lv_dropdown_set_selected(off_dd, auto_off_ms == 30000 ? 0 : auto_off_ms == 60000 ? 1 : auto_off_ms == 120000 ? 2 : auto_off_ms == 300000 ? 3 : auto_off_ms == 0 ? 4 : 1);
  lv_obj_add_event_cb(off_dd, autooff_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

  row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lbl = lv_label_create(row);
  lv_label_set_text(lbl, LV_SYMBOL_REFRESH " 12-hour clock");
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  sw = lv_switch_create(row);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -4, 0);
  if (clock_12h) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, clock12_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

  row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lbl = lv_label_create(row);
  lv_label_set_text(lbl, LV_SYMBOL_GPS " Distances in miles");
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  sw = lv_switch_create(row);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -4, 0);
  if (units_miles) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, units_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

  row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lbl = lv_label_create(row);
  lv_label_set_text(lbl, LV_SYMBOL_EYE_OPEN " Brightness");
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_t* slider = lv_slider_create(row);
  lv_obj_set_size(slider, 160, 14);
  lv_obj_align(slider, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_slider_set_range(slider, 10, 100);
  lv_slider_set_value(slider, bright_pct, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, bright_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(slider, bright_slider_cb, LV_EVENT_RELEASED, NULL);

  lv_obj_t* qr_row = lv_button_create(parent);
  lv_obj_set_size(qr_row, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(qr_row, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(qr_row, qredit_row_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(qr_row);
  lv_label_set_text(lbl, LV_SYMBOL_LIST " Quick replies");
  lv_obj_center(lbl);

  settingsBuildExtras(parent, this);

  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_size(btn, LV_PCT(100), 36);
  lv_obj_add_event_cb(btn, advert_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);
  lbl = lv_label_create(btn);
  lv_label_set_text(lbl, LV_SYMBOL_UPLOAD " Send advert (flood)");
  lv_obj_center(lbl);

  btn = lv_button_create(parent);
  lv_obj_set_size(btn, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(btn, advert_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)0);
  lbl = lv_label_create(btn);
  lv_label_set_text(lbl, LV_SYMBOL_UPLOAD " Send advert (zero hop)");
  lv_obj_center(lbl);

  btn = lv_button_create(parent);
  lv_obj_set_size(btn, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(btn, wizard_row_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(btn);
  lv_label_set_text(lbl, LV_SYMBOL_SETTINGS " Run setup wizard");
  lv_obj_center(lbl);

  btn = lv_button_create(parent);
  lv_obj_set_size(btn, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(btn, about_row_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(btn);
  lv_label_set_text(lbl, LV_SYMBOL_FILE " About & help");
  lv_obj_center(lbl);

  btn = lv_button_create(parent);
  lv_obj_set_size(btn, LV_PCT(100), 36);
  lv_obj_add_event_cb(btn, reboot_btn_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(btn);
  lv_label_set_text(lbl, LV_SYMBOL_REFRESH " Reboot");
  lv_obj_center(lbl);
  qrEditorBuild();
}

void UITask::refreshStatusBar() {
  char buf[64];

  // clock, once the RTC has real time (GPS fix or phone connection syncs it)
  uint32_t now = rtc_clock.getCurrentTime();
  if (now > 1600000000UL) {
    time_t local = (time_t) now + (time_t) settingsTzOffset() * 3600;
    struct tm tmv;
    gmtime_r(&local, &tmv);
    char namebuf[48];
    if (clock_12h) {
      int h12 = tmv.tm_hour % 12; if (h12 == 0) h12 = 12;
      snprintf(namebuf, sizeof(namebuf), "%s  %d:%02d %s",
               _node_prefs ? _node_prefs->node_name : "", h12, tmv.tm_min, tmv.tm_hour < 12 ? "am" : "pm");
    } else {
      snprintf(namebuf, sizeof(namebuf), "%s  %02d:%02d",
               _node_prefs ? _node_prefs->node_name : "", tmv.tm_hour, tmv.tm_min);
    }
    lv_label_set_text(lbl_node_name, namebuf);
  }

  uint16_t mv = getBattMilliVolts();
  int pct = battPercent(mv);
  const char* bsym = pct > 80 ? LV_SYMBOL_BATTERY_FULL
                   : pct > 55 ? LV_SYMBOL_BATTERY_3
                   : pct > 30 ? LV_SYMBOL_BATTERY_2
                   : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
  snprintf(buf, sizeof(buf), "%s%s %d  %s %d%%",
#ifdef STANDALONE_NO_BT
           // no phone link in this build, so report charging instead
           _board->isExternalPowered() ? LV_SYMBOL_CHARGE " " : "",
#else
           hasConnection() ? LV_SYMBOL_BLUETOOTH " " : "",
#endif
           LV_SYMBOL_ENVELOPE, chatStoreUnreadTotal(), bsym, pct);
  lv_label_set_text(lbl_status_right, buf);

  lv_obj_t* bar = lv_tabview_get_tab_bar(tabview);
  lv_obj_t* tab0 = bar != NULL ? lv_obj_get_child(bar, 0) : NULL;
  lv_obj_t* tl = tab0 != NULL ? lv_obj_get_child(tab0, 0) : NULL;
  if (tl != NULL) {
    int unread = chatStoreUnreadTotal();
    if (unread > 0) lv_label_set_text_fmt(tl, LV_SYMBOL_ENVELOPE " %d", unread);
    else lv_label_set_text(tl, LV_SYMBOL_ENVELOPE);
  }
}

void UITask::refreshChatsTab() {
  lv_obj_clean(chats_list);
  int n = chatStoreThreads(chats_row_keys, MAX_THREAD_ROWS);
  for (int i = 0; i < n; i++) {
    char label[64];
    const char* icon = LV_SYMBOL_ENVELOPE;
    int ch_idx;
    if (isChannelKey(chats_row_keys[i], &ch_idx)) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(ch_idx, ch) || ch.name[0] == 0) continue;
      channelLabel(label, sizeof(label), ch.name);
      icon = LV_SYMBOL_ENVELOPE;
    } else {
      ContactInfo* c = the_mesh.lookupContactByPubKey(chats_row_keys[i], 6);
      if (c != NULL && c->type != ADV_TYPE_CHAT) continue;   // repeater consoles: manager only
      snprintf(label, sizeof(label), "%s", c ? c->name : "(unknown)");
    }
    int unread = chatStoreUnreadGet(chats_row_keys[i]);
    if (unread > 0) {
      size_t l = strlen(label);
      snprintf(label + l, sizeof(label) - l, "   (%d)", unread);
    }
    lv_obj_t* btn = lv_list_add_button(chats_list, icon, label);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
    lv_obj_add_event_cb(btn, chats_row_cb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(btn, chats_row_long_cb, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)i);
  }
  for (int idx = 0; idx < MAX_GROUP_CHANNELS; idx++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(idx, ch)) break;
    if (ch.name[0] == 0) continue;
    uint8_t key[6];
    makeChannelKey(idx, key);
    bool listed = false;
    for (int i = 0; i < n; i++) {
      if (memcmp(chats_row_keys[i], key, 6) == 0) { listed = true; break; }
    }
    if (listed || n >= MAX_THREAD_ROWS) continue;
    memcpy(chats_row_keys[n], key, 6);
    char label[40];
    channelLabel(label, sizeof(label), ch.name);
    lv_obj_t* btn = lv_list_add_button(chats_list, LV_SYMBOL_ENVELOPE, label);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
    lv_obj_add_event_cb(btn, chats_row_cb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)n);
    lv_obj_add_event_cb(btn, chats_row_long_cb, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)n);
    n++;
  }
  if (n == 0) {
    lv_list_add_text(chats_list, "No chats yet - pick a contact");
  }
  lv_obj_t* add_btn = lv_list_add_button(chats_list, LV_SYMBOL_PLUS, "Add channel");
  lv_obj_set_style_text_color(add_btn, lv_color_hex(COL_MUTED), 0);
  lv_obj_add_event_cb(add_btn, addch_open_cb, LV_EVENT_CLICKED, NULL);
}

void UITask::refreshNodeTab() {
  if (_node_prefs == NULL) return;
  char buf[400];
  char gps_line[120] = "GPS: off";
  if (_sensors != NULL) {
    LocationProvider* nmea = _sensors->getLocationProvider();
    if (nmea != NULL && nmea->isValid()) {
      snprintf(gps_line, sizeof(gps_line), "GPS: fix, %d sats\n%.5f  %.5f",
               nmea->satellitesCount(),
               nmea->getLatitude() / 1000000., nmea->getLongitude() / 1000000.);
    } else if (_node_prefs->gps_enabled) {
      // sats in view + best SNR come from GSV: shows RF health long before a fix
      snprintf(gps_line, sizeof(gps_line),
               "GPS: searching - %d in view (%d strong), best %d dB\nGGA fix=%d used=%d  RMC=%c\n%s",
               gps_tap.satsInView(), gps_tap.strongSats(), gps_tap.bestSnr(),
               gps_tap.ggaFix(), gps_tap.ggaUsed(), gps_tap.rmcStatus(),
               gps_kicks > 0 ? "auto-restarted, re-acquiring" : "needs open sky for first fix");
    }
  }
  snprintf(buf, sizeof(buf),
           "Node: %s\n\n"
           "Freq: %.3f MHz   SF%d\n"
           "BW: %.2f   CR: %d\n"
           "TX: %d dBm\n\n"
           "%s\n\n"
           "Battery: %d mV\n"
           "BLE pin: %u",
           _node_prefs->node_name,
           _node_prefs->freq, _node_prefs->sf,
           _node_prefs->bw, _node_prefs->cr,
           _node_prefs->tx_power_dbm,
           gps_line,
           getBattMilliVolts(),
           (unsigned) the_mesh.getBLEPin());
  lv_label_set_text(node_info_lbl, buf);
}

void UITask::nodeNameChanged() {
  if (_node_prefs != NULL) lv_label_set_text(lbl_node_name, _node_prefs->node_name);
}

void UITask::onQueueSizeChanged(int msgcount) {
  _msgcount = msgcount;
  refreshStatusBar();
}

void UITask::onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type,
                           uint32_t sender_timestamp, const char* text) {
  if (txt_type == TXT_TYPE_CLI_DATA) {   // repeater CLI reply
    cliReply(from, text);
    return;
  }
  if (txt_type != TXT_TYPE_PLAIN && txt_type != TXT_TYPE_SIGNED_PLAIN) return;

  showIncoming(from.id.pub_key, from.name, text);
  if (!hasConnection()) notify(UIEventType::contactMessage);
}

void UITask::onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details, const char* text) {
  uint8_t key[6];
  int idx = the_mesh.findChannelIdx(channel_details.channel);
  if (idx >= 0) makeChannelKey(idx, key);
  showIncoming(idx >= 0 ? key : NULL, channel_details.name, text);
  if (!hasConnection()) notify(UIEventType::channelMessage);
}

void UITask::onACKRecv(uint32_t ack_crc) {
  if (chatStoreAck(ack_crc)) {
    if (threadOpen()) refreshThread();
  }
}

void UITask::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  if (!hasConnection()) notify(UIEventType::newContactMessage);
}

void UITask::notify(UIEventType t) {
  if (_node_prefs != NULL && _node_prefs->buzzer_quiet) return;   // sounds off
  switch (t) {
    case UIEventType::contactMessage:
    case UIEventType::newContactMessage:
      soundMessageTone();
      break;
    case UIEventType::channelMessage:
    case UIEventType::roomMessage:
      soundChannelTone();
      break;
    case UIEventType::ack:
      soundAckTone();
      break;
    default:
      break;
  }
}

void UITask::shutdown(bool restart) {
  if (restart) _board->reboot();
  else _board->powerOff();
}

void UITask::loop() {
  lv_timer_handler();
  chatStoreFlushLoop();

  if (millis() > _next_status_refresh) {
    refreshStatusBar();
    if (threadOpen()) {
      updateThreadSubtitle();   // route can change as paths are learned
      for (int k = 0; ; k++) {
        ChatMsg* m = chatStoreGet(_thread_key, k);
        if (m == NULL) break;
        if (m->outgoing && m->status == MSG_STATUS_PENDING && millis() > m->timeout_at) {
          refreshThread();
          break;
        }
      }
    } else if (lv_tabview_get_tab_active(tabview) == 3) {   // Settings: telemetry card
      refreshNodeTab();
    } else if (lv_tabview_get_tab_active(tabview) == 2) {   // Map tab: track GPS
      mapViewRefresh();
    }
    repeaterTick();
    traceTick();
    _next_status_refresh = millis() + 2000;
  }

#ifdef SEEED_WIO_TRACKER_L2
  // WAKE button (top edge, on the IO expander): toggles screen lock
  if (millis() > _next_wake_poll) {
    _next_wake_poll = millis() + 150;
    bool pressed = board.readWakeButton();
    if (pressed && !_wake_prev) {
      if (_display_asleep) {
        screenPower(false);
        display.lgfxDevice()->setBrightness(brightRaw());
        bright_dimmed = false;
        lv_obj_add_flag(sleep_shield, LV_OBJ_FLAG_HIDDEN);
        lv_display_trigger_activity(NULL);
        _display_asleep = false;
      } else {
        display.lgfxDevice()->setBrightness(0);
        bright_dimmed = false;
        lv_obj_remove_flag(sleep_shield, LV_OBJ_FLAG_HIDDEN);
        _display_asleep = true;
        _lock_grace = millis() + 1200;
        screenPower(true);
      }
    }
    _wake_prev = pressed;
  }
#endif

  // low battery: warn and power off (never while externally powered)
  static unsigned long next_batt_check = 30000;
  if (millis() > next_batt_check) {
    next_batt_check = millis() + 30000;
    uint16_t mv = getBattMilliVolts();
    // re-applied each pass: the stack restarts fast advertising on disconnect
    static unsigned long last_ble_conn = 0;
    if (hasConnection()) { last_ble_conn = millis(); ble_slow = false; }
    bool want_slow = ble_autooff_min > 0 && !ble_off_pref && isBluetoothEnabled() && !hasConnection() &&
                     millis() - last_ble_conn > (unsigned long) ble_autooff_min * 60000UL;
    if (want_slow && !ble_slow) bleSlowAdvertising(true);
    static bool low_warned = false;
    if (_board->isExternalPowered()) low_warned = false;
    if (mv > 500 && mv < 3450 && !low_warned && !_board->isExternalPowered()) {
      low_warned = true;
      showToast(LV_SYMBOL_BATTERY_1 " Battery low - charge soon");
      soundChannelTone();
    }
    if (mv > 500 && mv < 3250 && !_board->isExternalPowered()) {
      display.lgfxDevice()->setBrightness(brightRaw());
      showToast("LOW BATTERY - shutting down");
      lv_refr_now(NULL);
      delay(3000);
      shutdown(false);
    }
  }

  // backlight auto-dim on inactivity; the sleep shield eats the wake-up tap
  // (stay awake while externally powered - LCD has no burn-in concern)
  uint32_t idle = lv_display_get_inactive_time(NULL);
  if (!_display_asleep && idle > autoOffMs() && _board->isExternalPowered()) {
    lv_display_trigger_activity(NULL);
    idle = 0;
  }
  if (!_display_asleep) {
    bool want_dim = idle > autoOffMs() - DIM_LEAD_MILLIS && idle <= autoOffMs();
    if (want_dim && !bright_dimmed) {
      uint8_t dim = brightRaw() / 4;
      display.lgfxDevice()->setBrightness(dim < 12 ? 12 : dim);
      bright_dimmed = true;
    } else if (!want_dim && bright_dimmed && idle < autoOffMs()) {
      display.lgfxDevice()->setBrightness(brightRaw());
      bright_dimmed = false;
    }
  }
  if (!_display_asleep && idle > autoOffMs()) {
    display.lgfxDevice()->setBrightness(0);
    bright_dimmed = false;
    lv_obj_remove_flag(sleep_shield, LV_OBJ_FLAG_HIDDEN);
    _display_asleep = true;
    screenPower(true);
  } else if (_display_asleep && idle < 1000 && millis() > _lock_grace) {
    screenPower(false);
    display.lgfxDevice()->setBrightness(brightRaw());
    lv_obj_add_flag(sleep_shield, LV_OBJ_FLAG_HIDDEN);
    _display_asleep = false;
  }

#ifndef STANDALONE_NO_BT
  // Bluetooth off preference: apply once the interface is up
  if (!ble_pref_applied && millis() > 4000) {
    ble_pref_applied = true;
    if (ble_off_pref && isBluetoothEnabled()) disableBluetooth();
  }
#endif

  static unsigned long next_gps_diag = 0;
  if (millis() > next_gps_diag) {
    next_gps_diag = millis() + 10000;
    if (_node_prefs != NULL && _node_prefs->gps_enabled && _sensors != NULL) {
      gpsWatchdogTick();
      LocationProvider* n = _sensors->getLocationProvider();
      MESH_DEBUG_PRINTLN("gps: used=%d valid=%d inview=%d strong=%d bestsnr=%d gga=%d/%d rmc=%c nmea=%d | heap int=%u largest=%u",
                    n != NULL ? (int)n->satellitesCount() : -1,
                    n != NULL ? (int)n->isValid() : 0,
                    gps_tap.satsInView(), gps_tap.strongSats(), gps_tap.bestSnr(),
                    gps_tap.ggaFix(), gps_tap.ggaUsed(), gps_tap.rmcStatus(), (int) gps_tap.streaming(),
                    (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                    (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }
  }
}
