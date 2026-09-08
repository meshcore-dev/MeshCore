#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <SPIFFS.h>
#include <SD_MMC.h>
#ifndef STANDALONE_NO_BT
  #include <BLEDevice.h>
#endif
#include "Sound.h"
#include "MapView.h"
#include "SettingsUI.h"

// from base64.hpp, which defines (not just declares) its functions and is
// already compiled into BaseChatMesh.cpp - re-including would double-define
unsigned int encode_base64(const unsigned char input[], unsigned int len, unsigned char output[]);
unsigned int decode_base64(const unsigned char input[], unsigned int len, unsigned char output[]);

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS 60000
#endif

// ---------------------------------------------------------------------------
// theme
// ---------------------------------------------------------------------------
#define COL_BG       0x0A1020   // deep navy
#define COL_CARD     0x16233B   // panel/card
#define COL_ACCENT   0x58B4FF   // meshcore sky blue
#define COL_ACCENT_D 0x1F4E7A   // outgoing bubble
#define COL_TXT      0xE8ECF2
#define COL_MUTED    0x8FA3BF
#define COL_WARN     0xFFA030

// ---------------------------------------------------------------------------
// LVGL <-> LovyanGFX glue
// ---------------------------------------------------------------------------
static UITask* ui = NULL;   // singleton for LVGL callbacks

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

// ---------------------------------------------------------------------------
// widget handles
// ---------------------------------------------------------------------------
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
static lv_obj_t* contacts_list;
static lv_obj_t* node_info_lbl;
// thread overlay
static lv_obj_t* thread_scr;
static lv_obj_t* thread_title;
static lv_obj_t* thread_sub_lbl;   // route (flood/hops) + repeat-echo count
static lv_obj_t* thread_msgs;
static lv_obj_t* thread_input_row;
static lv_obj_t* thread_ta;
static lv_obj_t* thread_kb;

#define KB_HEIGHT 160

// UI font: the icon glyphs the screens use, falling back to montserrat for text.
LV_FONT_DECLARE(fa_icons_14);

#define SYMBOL_TOWER     "\xEF\x94\x99"   /* U+F519 broadcast tower */
#define SYMBOL_ADDR_BOOK "\xEF\x8A\xB9"   /* U+F2B9 address book */

// boot splash
const lv_image_dsc_t* meshcoreLogoImage();
static lv_obj_t* splash_scr;
static lv_obj_t* wiz_scr = NULL;      // first-boot setup wizard
static bool wizard_pending = false;
static void wizSuggestTz();

static void splash_dismiss_cb(lv_timer_t* t) {
  lv_obj_delete(splash_scr);
  splash_scr = NULL;
  lv_timer_delete(t);
  if (wizard_pending && wiz_scr != NULL) { wizSuggestTz(); lv_obj_remove_flag(wiz_scr, LV_OBJ_FLAG_HIDDEN); }
}

static void buildSplash() {
  splash_scr = lv_obj_create(lv_layer_top());
  lv_obj_set_size(splash_scr, 320, 240);
  lv_obj_set_style_bg_color(splash_scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(splash_scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(splash_scr, 0, 0);
  lv_obj_set_style_radius(splash_scr, 0, 0);
  lv_obj_remove_flag(splash_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(splash_scr, LV_OBJ_FLAG_CLICKABLE);   // eat taps during boot

  lv_obj_t* title = lv_image_create(splash_scr);
  lv_image_set_src(title, meshcoreLogoImage());
  lv_obj_set_style_image_recolor(title, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_image_recolor_opa(title, LV_OPA_COVER, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -26);

  lv_obj_t* sub = lv_label_create(splash_scr);
  lv_label_set_text(sub, "Wio Tracker L2 Pro");
  lv_obj_set_style_text_color(sub, lv_color_hex(COL_TXT), 0);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 2);

  lv_obj_t* ver = lv_label_create(splash_scr);
  char vbuf[40];
  snprintf(vbuf, sizeof(vbuf), "%s  %s", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE);
  lv_label_set_text(ver, vbuf);
  lv_obj_set_style_text_color(ver, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(ver, LV_ALIGN_CENTER, 0, 26);

  lv_timer_create(splash_dismiss_cb, 2500, NULL);
}

// invisible overlay that eats the wake-up tap while the display is dimmed
static lv_obj_t* sleep_shield;

// repeater manager overlay
static lv_obj_t* rep_scr;
static lv_obj_t* rep_title;
static lv_obj_t* rep_body;
static lv_obj_t* rep_pw_ta;
static lv_obj_t* rep_kb;

// mirrors RepeaterStats in examples/simple_repeater/MyMesh.h (wire format)
struct RepStats {
  uint16_t batt_milli_volts;
  uint16_t curr_tx_queue_len;
  int16_t  noise_floor;
  int16_t  last_rssi;
  uint32_t n_packets_recv;
  uint32_t n_packets_sent;
  uint32_t total_air_time_secs;
  uint32_t total_up_time_secs;
  uint32_t n_sent_flood, n_sent_direct;
  uint32_t n_recv_flood, n_recv_direct;
  uint16_t err_events;
  int16_t  last_snr;   // x 4
  uint16_t n_direct_dups, n_flood_dups;
  uint32_t total_rx_air_time_secs;
  uint32_t n_recv_errors;
};
static RepStats rep_stats;
static bool rep_stats_valid = false;
static bool rep_stats_waiting = false;
static lv_obj_t* rep_stats_lbl;      // card body: status or command reply
static lv_obj_t* rep_card_title;
static bool rep_show_reply = false;  // card is showing a command reply
static char rep_last_cmd[24] = "";
static bool rep_auto_refresh = false;   // opt-in: poll status while open
static char rep_last_reply[110] = "";
static lv_obj_t* rep_remember_cb;
static lv_obj_t* rep_hint_lbl;
static lv_obj_t* rep_flood_cb;
static bool rep_flood_on = false;   // survives screen rebuilds while logging in
static lv_obj_t* rep_login_btn;

// saved repeater passwords (device flash; the node itself is the boundary)
struct SavedPw { uint8_t key[6]; char pw[26]; };
#define MAX_SAVED_PW 8
static SavedPw saved_pw[MAX_SAVED_PW];
static char rep_pending_pw[26] = "";
static bool rep_pending_remember = false;

static void savedPwLoad() {
  File f = SPIFFS.open("/rep_pw.bin", "r");
  if (f) {
    f.read((uint8_t*)saved_pw, sizeof(saved_pw));
    f.close();
  }
}

static void savedPwStore() {
  File f = SPIFFS.open("/rep_pw.bin", "w");
  if (f) {
    f.write((const uint8_t*)saved_pw, sizeof(saved_pw));
    f.close();
  }
}

static const char* savedPwFor(const uint8_t key[6]) {
  for (int i = 0; i < MAX_SAVED_PW; i++) {
    if (saved_pw[i].pw[0] != 0 && memcmp(saved_pw[i].key, key, 6) == 0) return saved_pw[i].pw;
  }
  return NULL;
}

static void savedPwSet(const uint8_t key[6], const char* pw) {
  int slot = -1;
  for (int i = 0; i < MAX_SAVED_PW; i++) {
    if (memcmp(saved_pw[i].key, key, 6) == 0) { slot = i; break; }
    if (saved_pw[i].pw[0] == 0 && slot < 0) slot = i;
  }
  if (slot < 0) slot = 0;   // full: overwrite oldest slot
  memcpy(saved_pw[slot].key, key, 6);
  StrHelper::strncpy(saved_pw[slot].pw, pw, sizeof(saved_pw[slot].pw));
  savedPwStore();
}

static void savedPwForget(const uint8_t key[6]) {
  for (int i = 0; i < MAX_SAVED_PW; i++) {
    if (memcmp(saved_pw[i].key, key, 6) == 0) saved_pw[i].pw[0] = 0;
  }
  savedPwStore();
}

// ---- small persisted comfort prefs ----
static uint32_t auto_off_ms = AUTO_OFF_MILLIS;   // 0 = never sleep
static bool clock_12h = false;
static bool ble_off_pref = false;
static bool ble_pref_applied = false;
static bool units_miles = false;
static int  ble_autooff_min = 0;        // 0 = never; else minutes idle before slow advertising
static bool ble_slow = false;
static bool grove_on = true;
static lv_obj_t* ble_sw = NULL;

static int prefReadInt(const char* path, int dflt) {
  File f = SPIFFS.open(path, "r");
  if (!f) return dflt;
  int v = f.parseInt();
  f.close();
  return v;
}
static void prefWriteInt(const char* path, int v) {
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
  rep_auto_refresh = prefReadInt("/rep_auto", 0) != 0;
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

// ---- contact list filter chips ----
static int contact_filter = 0;          // 0 all, 1 chat, 2 repeater, 3 room
static bool contacts_by_name = false;
static lv_obj_t* filter_btns[5];

static void updateFilterChips() {
  for (int i = 0; i < 5; i++) {
    bool on = i == 4 ? contacts_by_name : (i == contact_filter);
    lv_obj_set_style_bg_color(filter_btns[i], lv_color_hex(on ? COL_ACCENT : COL_CARD), 0);
  }
}
static void contact_filter_cb(lv_event_t* e) {
  int i = (int)(intptr_t) lv_event_get_user_data(e);
  if (i == 4) contacts_by_name = !contacts_by_name;
  else contact_filter = i;
  updateFilterChips();
  ui->refreshContactsTab();
}

// ---- notification banner: sender + preview, tap to open the thread ----
static lv_obj_t* notif_banner = NULL;
static lv_obj_t* notif_lbl = NULL;
static lv_timer_t* notif_timer = NULL;
static uint8_t notif_key[6];
static char notif_name[36];

static void notif_hide_cb(lv_timer_t* tm) {
  lv_obj_add_flag(notif_banner, LV_OBJ_FLAG_HIDDEN);
  notif_timer = NULL;   // repeat count 1: LVGL deletes the timer itself
}
static void notif_tap_cb(lv_event_t* e) {
  if (notif_timer != NULL) { lv_timer_delete(notif_timer); notif_timer = NULL; }
  lv_obj_add_flag(notif_banner, LV_OBJ_FLAG_HIDDEN);
  ui->openThread(notif_key, notif_name);
}
static void showNotifBanner(const uint8_t key[6], const char* from_name, const char* text) {
  if (notif_banner == NULL) return;
  memcpy(notif_key, key, 6);
  int ch_idx;
  if (isChannelKey(key, &ch_idx) && from_name[0] != '#') snprintf(notif_name, sizeof(notif_name), "#%s", from_name);
  else StrHelper::strncpy(notif_name, from_name, sizeof(notif_name));
  lv_label_set_text_fmt(notif_lbl, "%s\n%.60s", notif_name, text);
  lv_obj_remove_flag(notif_banner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(notif_banner);
  if (notif_timer != NULL) lv_timer_delete(notif_timer);
  notif_timer = lv_timer_create(notif_hide_cb, 6000, NULL);
  lv_timer_set_repeat_count(notif_timer, 1);
}

// ---- channel options (long-press a channel row): rename / remove ----
static lv_obj_t* chopt_scr;
static lv_obj_t* chopt_title;
static lv_obj_t* chopt_ta;
static lv_obj_t* chopt_kb;
static lv_obj_t* chopt_remove_lbl;
static int chopt_idx = -1;
static uint8_t chopt_key[6];
static bool chopt_remove_armed = false;

static void chopt_close() {
  lv_obj_add_flag(chopt_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(chopt_kb, LV_OBJ_FLAG_HIDDEN);
}
static void chopt_cancel_cb(lv_event_t* e) { chopt_close(); }
static void chopt_ta_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) lv_obj_remove_flag(chopt_kb, LV_OBJ_FLAG_HIDDEN);
  else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL || code == LV_EVENT_READY) lv_obj_add_flag(chopt_kb, LV_OBJ_FLAG_HIDDEN);
}
static void chopt_save_cb(lv_event_t* e) {
  const char* txt = lv_textarea_get_text(chopt_ta);
  if (txt != NULL && txt[0] == '#') txt++;
  if (txt == NULL || txt[0] == 0) { ui->showToast("Channel needs a name"); return; }
  ChannelDetails ch;
  if (chopt_idx < 0 || !the_mesh.getChannel(chopt_idx, ch)) { chopt_close(); return; }
  StrHelper::strncpy(ch.name, txt, sizeof(ch.name));
  the_mesh.setChannel(chopt_idx, ch);
  the_mesh.saveChannels();
  ui->refreshChatsTab();
  ui->showToast("Channel renamed");
  chopt_close();
}
static void chopt_remove_cb(lv_event_t* e) {
  if (!chopt_remove_armed) {
    chopt_remove_armed = true;
    lv_label_set_text(chopt_remove_lbl, "SURE? (removes channel + history)");
    return;
  }
  ChannelDetails ch;
  if (chopt_idx >= 0 && the_mesh.getChannel(chopt_idx, ch)) {
    memset(&ch, 0, sizeof(ch));   // empty name = free slot
    the_mesh.setChannel(chopt_idx, ch);
    the_mesh.saveChannels();
    chatStoreClearThread(chopt_key);
    ui->refreshChatsTab();
    ui->showToast("Channel removed");
  }
  chopt_close();
}

// ---- about & help ----
static lv_obj_t* about_scr;
static lv_obj_t* about_lbl;
static void about_close_cb(lv_event_t* e) { lv_obj_add_flag(about_scr, LV_OBJ_FLAG_HIDDEN); }
static void about_advert_cb(lv_event_t* e) {
  ui->showToast(the_mesh.advertFlood() ? "Flood advert sent" : "Advert failed");
}
static void about_open_cb(lv_event_t* e) {
  char hex[65];
  const uint8_t* pk = the_mesh.selfId().pub_key;
  for (int i = 0; i < 32; i++) snprintf(&hex[i * 2], 3, "%02X", pk[i]);
  char buf[640];
  snprintf(buf, sizeof(buf),
#ifdef STANDALONE_NO_BT
    "MeshCore %s (%s)\nWio Tracker L2 Pro - standalone (no Bluetooth)\n\n"
#else
    "MeshCore %s (%s)\nWio Tracker L2 Pro - standalone + Bluetooth\n\n"
#endif
    "Node: %s\nPublic key:\n%.32s\n%.32s\n\n"
    "Legend\n"
    LV_SYMBOL_OK " delivered   " LV_SYMBOL_REFRESH " sending   " LV_SYMBOL_WARNING " not delivered (tap to retry)\n"
    LV_SYMBOL_LOOP "N  repeaters heard repeating your message\n"
    "flood = no path yet, sent everywhere\n"
    "direct = 0 hops, N hops = via repeaters\n"
    "#name = channel (group chat)\n"
    "Long-press a contact or channel for options.\n"
    "Advert = announce this node to the mesh.",
    FIRMWARE_VERSION, FIRMWARE_BUILD_DATE, the_mesh.getNodeName(), hex, hex + 32);
  lv_label_set_text(about_lbl, buf);
  lv_obj_remove_flag(about_scr, LV_OBJ_FLAG_HIDDEN);
}

// ---- first-boot setup wizard: region, name, timezone ----
static lv_obj_t* wiz_title;
static lv_obj_t* wiz_steps[3];
static lv_obj_t* wiz_dd;
static lv_obj_t* wiz_name_ta;
static lv_obj_t* wiz_kb;
static lv_obj_t* wiz_tz_lbl;
static lv_obj_t* wiz_next_lbl;
static int wiz_step = 0;
static int wiz_tz = -5;

static void wizShowStep(int s) {
  wiz_step = s;
  static const char* titles[3] = {"Welcome to MeshCore", "Name your node", "Set your timezone"};
  lv_label_set_text(wiz_title, titles[s]);
  for (int i = 0; i < 3; i++) {
    if (i == s) lv_obj_remove_flag(wiz_steps[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(wiz_steps[i], LV_OBJ_FLAG_HIDDEN);
  }
  lv_label_set_text(wiz_next_lbl, s == 2 ? LV_SYMBOL_OK " Finish" : "Next " LV_SYMBOL_RIGHT);
  if (s != 1) lv_obj_add_flag(wiz_kb, LV_OBJ_FLAG_HIDDEN);
}
static void wiz_next_cb(lv_event_t* e) {
  if (wiz_step < 2) { wizShowStep(wiz_step + 1); return; }
  ui->finishSetupWizard((int) lv_dropdown_get_selected(wiz_dd), lv_textarea_get_text(wiz_name_ta), wiz_tz, true);
}
static void wiz_skip_cb(lv_event_t* e) { ui->finishSetupWizard(0, "", wiz_tz, false); }
static void wiz_tz_cb(lv_event_t* e) {
  int v = wiz_tz + (int)(intptr_t) lv_event_get_user_data(e);
  if (v < -12 || v > 14) return;
  wiz_tz = v;
  lv_label_set_text_fmt(wiz_tz_lbl, "UTC%+d", wiz_tz);
}
static void wiz_ta_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) lv_obj_remove_flag(wiz_kb, LV_OBJ_FLAG_HIDDEN);
  else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL || code == LV_EVENT_READY) lv_obj_add_flag(wiz_kb, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t* makeOverlay() {
  lv_obj_t* scr = lv_obj_create(lv_layer_top());
  lv_obj_set_size(scr, 320, 240);
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 8, 0);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}
static lv_obj_t* overlayBtn(lv_obj_t* parent, const char* txt, lv_event_cb_t cb, void* ud,
                            lv_align_t align, int xo, int yo, int w, bool accent) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, w, 32);
  lv_obj_align(b, align, xo, yo);
  if (!accent) lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  return b;
}

static void buildPolishOverlays() {
  // notification banner (top layer, above everything)
  notif_banner = lv_obj_create(lv_layer_top());
  lv_obj_set_size(notif_banner, 304, 46);
  lv_obj_align(notif_banner, LV_ALIGN_TOP_MID, 0, 26);
  lv_obj_set_style_bg_color(notif_banner, lv_color_hex(COL_ACCENT_D), 0);
  lv_obj_set_style_radius(notif_banner, 8, 0);
  lv_obj_set_style_pad_all(notif_banner, 6, 0);
  lv_obj_set_style_border_width(notif_banner, 0, 0);
  lv_obj_add_flag(notif_banner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(notif_banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(notif_banner, notif_tap_cb, LV_EVENT_CLICKED, NULL);
  notif_lbl = lv_label_create(notif_banner);
  lv_obj_set_width(notif_lbl, 290);
  lv_label_set_long_mode(notif_lbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(notif_lbl, &lv_font_montserrat_12, 0);
  lv_obj_add_flag(notif_banner, LV_OBJ_FLAG_HIDDEN);

  // channel options
  chopt_scr = makeOverlay();
  chopt_title = lv_label_create(chopt_scr);
  lv_obj_set_style_text_color(chopt_title, lv_color_hex(COL_ACCENT), 0);
  lv_obj_align(chopt_title, LV_ALIGN_TOP_LEFT, 0, 6);
  overlayBtn(chopt_scr, LV_SYMBOL_OK, chopt_save_cb, NULL, LV_ALIGN_TOP_RIGHT, -78, 0, 72, true);
  overlayBtn(chopt_scr, LV_SYMBOL_CLOSE, chopt_cancel_cb, NULL, LV_ALIGN_TOP_RIGHT, -2, 0, 72, false);
  chopt_ta = lv_textarea_create(chopt_scr);
  lv_textarea_set_one_line(chopt_ta, true);
  lv_textarea_set_max_length(chopt_ta, 30);
  lv_textarea_set_placeholder_text(chopt_ta, "Channel name");
  lv_obj_set_width(chopt_ta, 304);
  lv_obj_set_height(chopt_ta, LV_SIZE_CONTENT);
  lv_obj_align(chopt_ta, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_opa(chopt_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_obj_set_style_opa(chopt_ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_event_cb(chopt_ta, chopt_ta_cb, LV_EVENT_ALL, NULL);
  lv_obj_t* rb = overlayBtn(chopt_scr, LV_SYMBOL_TRASH " Remove channel", chopt_remove_cb, NULL, LV_ALIGN_TOP_MID, 0, 84, 304, false);
  lv_obj_set_style_bg_color(rb, lv_color_hex(0x7A2020), 0);
  chopt_remove_lbl = lv_obj_get_child(rb, 0);
  chopt_kb = lv_keyboard_create(chopt_scr);
  lv_keyboard_set_textarea(chopt_kb, chopt_ta);
  kbAttachShiftBehavior(chopt_kb);
  lv_obj_set_size(chopt_kb, 320, KB_HEIGHT);
  lv_obj_align(chopt_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(chopt_kb, LV_OBJ_FLAG_HIDDEN);

  about_scr = makeOverlay();
  lv_obj_t* body = lv_obj_create(about_scr);
  lv_obj_set_size(body, 304, 186);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 0, 0);
  about_lbl = lv_label_create(body);
  lv_obj_set_width(about_lbl, 290);
  lv_label_set_long_mode(about_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(about_lbl, &lv_font_montserrat_12, 0);
  lv_label_set_text(about_lbl, "");
  overlayBtn(about_scr, LV_SYMBOL_UPLOAD " Send advert", about_advert_cb, NULL, LV_ALIGN_BOTTOM_LEFT, 0, -2, 148, false);
  overlayBtn(about_scr, LV_SYMBOL_CLOSE " Close", about_close_cb, NULL, LV_ALIGN_BOTTOM_RIGHT, 0, -2, 148, true);

  // first-boot wizard
  wiz_scr = makeOverlay();
  wiz_title = lv_label_create(wiz_scr);
  lv_obj_set_style_text_color(wiz_title, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_text_font(wiz_title, &lv_font_montserrat_16, 0);
  lv_obj_align(wiz_title, LV_ALIGN_TOP_LEFT, 0, 0);
  for (int i = 0; i < 3; i++) {
    wiz_steps[i] = lv_obj_create(wiz_scr);
    lv_obj_set_size(wiz_steps[i], 304, 150);
    lv_obj_align(wiz_steps[i], LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_bg_opa(wiz_steps[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wiz_steps[i], 0, 0);
    lv_obj_set_style_pad_all(wiz_steps[i], 0, 0);
    lv_obj_remove_flag(wiz_steps[i], LV_OBJ_FLAG_SCROLLABLE);
  }
  // step 0: region preset
  lv_obj_t* l0 = lv_label_create(wiz_steps[0]);
  lv_obj_set_width(l0, 300);
  lv_label_set_long_mode(l0, LV_LABEL_LONG_WRAP);
  lv_label_set_text(l0, "Pick your region so the radio settings match the other MeshCore nodes around you. You can change this later in Settings.");
  lv_obj_align(l0, LV_ALIGN_TOP_LEFT, 0, 0);
  char opts[200] = "";
  for (int i = 0; i < settingsPresetCount(); i++) {
    if (i > 0) strlcat(opts, "\n", sizeof(opts));
    strlcat(opts, settingsPresetLabel(i), sizeof(opts));
  }
  wiz_dd = lv_dropdown_create(wiz_steps[0]);
  lv_obj_set_size(wiz_dd, 300, 34);
  lv_obj_align(wiz_dd, LV_ALIGN_TOP_MID, 0, 80);
  lv_dropdown_set_options(wiz_dd, opts);
  // step 1: node name
  lv_obj_t* l1 = lv_label_create(wiz_steps[1]);
  lv_obj_set_width(l1, 300);
  lv_label_set_long_mode(l1, LV_LABEL_LONG_WRAP);
  lv_label_set_text(l1, "Give this node a name. Other people see it in their contact list.");
  lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 0, 0);
  wiz_name_ta = lv_textarea_create(wiz_steps[1]);
  lv_textarea_set_one_line(wiz_name_ta, true);
  lv_textarea_set_max_length(wiz_name_ta, 30);
  lv_obj_set_width(wiz_name_ta, 300);
  lv_obj_set_height(wiz_name_ta, LV_SIZE_CONTENT);
  lv_obj_align(wiz_name_ta, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_style_opa(wiz_name_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_obj_set_style_opa(wiz_name_ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_event_cb(wiz_name_ta, wiz_ta_cb, LV_EVENT_ALL, NULL);
  // step 2: timezone
  lv_obj_t* l2 = lv_label_create(wiz_steps[2]);
  lv_obj_set_width(l2, 300);
  lv_label_set_long_mode(l2, LV_LABEL_LONG_WRAP);
  lv_label_set_text(l2, "Hours offset from UTC so message times show local time. Examples: New York -5, London 0, Berlin +1, Sydney +10.");
  lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 0, 0);
  overlayBtn(wiz_steps[2], LV_SYMBOL_MINUS, wiz_tz_cb, (void*)(intptr_t)-1, LV_ALIGN_TOP_MID, -70, 80, 50, false);
  wiz_tz_lbl = lv_label_create(wiz_steps[2]);
  lv_obj_set_style_text_font(wiz_tz_lbl, &lv_font_montserrat_16, 0);
  lv_obj_align(wiz_tz_lbl, LV_ALIGN_TOP_MID, 0, 88);
  overlayBtn(wiz_steps[2], LV_SYMBOL_PLUS, wiz_tz_cb, (void*)(intptr_t)+1, LV_ALIGN_TOP_MID, 70, 80, 50, false);
  // nav
  overlayBtn(wiz_scr, "Skip", wiz_skip_cb, NULL, LV_ALIGN_BOTTOM_LEFT, 0, -2, 100, false);
  lv_obj_t* nb = overlayBtn(wiz_scr, "Next", wiz_next_cb, NULL, LV_ALIGN_BOTTOM_RIGHT, 0, -2, 148, true);
  wiz_next_lbl = lv_obj_get_child(nb, 0);
  wiz_kb = lv_keyboard_create(wiz_scr);
  lv_keyboard_set_textarea(wiz_kb, wiz_name_ta);
  kbAttachShiftBehavior(wiz_kb);
  lv_obj_set_size(wiz_kb, 320, KB_HEIGHT);
  lv_obj_align(wiz_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(wiz_kb, LV_OBJ_FLAG_HIDDEN);
  wiz_tz = settingsTzOffset();
  lv_label_set_text_fmt(wiz_tz_lbl, "UTC%+d", wiz_tz);
  if (ui->nodePrefs() != NULL) lv_textarea_set_text(wiz_name_ta, ui->nodePrefs()->node_name);
  wizShowStep(0);
}

// when the wizard opens with a GPS fix available, suggest the zone from longitude
static void wizSuggestTz() {
  SensorManager* s = ui->sensors();
  LocationProvider* nmea = s != NULL ? s->getLocationProvider() : NULL;
  if (nmea == NULL || !nmea->isValid()) return;
  int guess = (int) lroundf((nmea->getLongitude() / 1000000.0f) / 15.0f);
  if (guess < -12 || guess > 14) return;
  wiz_tz = guess;
  lv_label_set_text_fmt(wiz_tz_lbl, "UTC%+d  (from GPS)", wiz_tz);
}

// re-run the wizard on demand from Settings
static void wizardOpen() {
  if (ui->nodePrefs() != NULL) lv_textarea_set_text(wiz_name_ta, ui->nodePrefs()->node_name);
  wiz_tz = settingsTzOffset();
  lv_label_set_text_fmt(wiz_tz_lbl, "UTC%+d", wiz_tz);
  wizSuggestTz();
  wizShowStep(0);
  lv_obj_remove_flag(wiz_scr, LV_OBJ_FLAG_HIDDEN);
}
static void wizard_row_cb(lv_event_t* e) { wizardOpen(); }

// ---- quick replies: canned messages sendable without the keyboard ----
#define QR_MAX 6
#define QR_TEXT_LEN 64
static char qr_texts[QR_MAX][QR_TEXT_LEN];
static int qr_count = 0;
static lv_obj_t* qr_panel = NULL;
static lv_obj_t* thread_qr_btn;
static lv_obj_t* qredit_scr;
static lv_obj_t* qredit_ta;
static lv_obj_t* qredit_kb;

static void qrDefaults() {
  static const char* defs[] = {"On my way", "Yes", "No", "Copy that", "At camp, all good"};
  qr_count = 0;
  for (auto d : defs) StrHelper::strncpy(qr_texts[qr_count++], d, QR_TEXT_LEN);
}

static void qrLoad() {
  File f = SPIFFS.open("/qreplies.txt", "r");
  if (!f) { qrDefaults(); return; }
  qr_count = 0;
  while (f.available() && qr_count < QR_MAX) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) StrHelper::strncpy(qr_texts[qr_count++], line.c_str(), QR_TEXT_LEN);
  }
  f.close();
  if (qr_count == 0) qrDefaults();
}

static void qrSave() {
  File f = SPIFFS.open("/qreplies.txt", "w");
  if (!f) return;
  for (int i = 0; i < qr_count; i++) {
    f.print(qr_texts[i]);
    f.print('\n');
  }
  f.close();
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

// ---------------------------------------------------------------------------
// event callbacks
// ---------------------------------------------------------------------------
static void contact_row_cb(lv_event_t* e) {
  int idx = (int)(intptr_t) lv_event_get_user_data(e);
  ContactInfo c;
  if (!the_mesh.getContactByIdx(idx, c)) return;
  if (c.type == ADV_TYPE_CHAT) {
    ui->openContactThread(c);
  } else if (c.type == ADV_TYPE_REPEATER || c.type == ADV_TYPE_ROOM) {
    ui->openRepeaterManager(c);
  } else {
    ui->showToast("Not a chat node");
  }
}

static void contact_row_long_cb(lv_event_t* e) {
  ui->openContactDetail((int)(intptr_t) lv_event_get_user_data(e));
}

// ---- contact detail overlay ----
static lv_obj_t* detail_scr;
static lv_obj_t* detail_title;
static lv_obj_t* detail_info;
static lv_obj_t* detail_remove_lbl;
static lv_obj_t* detail_trace_btn;
static int detail_idx = -1;
static bool detail_remove_armed = false;

static void detail_close_cb(lv_event_t* e) { lv_obj_add_flag(detail_scr, LV_OBJ_FLAG_HIDDEN); }

static void detail_msg_cb(lv_event_t* e) {
  ContactInfo c;
  if (!the_mesh.getContactByIdx(detail_idx, c)) return;
  lv_obj_add_flag(detail_scr, LV_OBJ_FLAG_HIDDEN);
  if (c.type == ADV_TYPE_CHAT) ui->openContactThread(c);
  else ui->openRepeaterManager(c);
}

// ---- trace route overlay: per-hop SNR along the contact's path ----
static lv_obj_t* trace_scr;
static lv_obj_t* trace_title;
static lv_obj_t* trace_lbl;

static bool path_from_trace = false;   // the picker returns where it came from
static void path_open_cb(lv_event_t* e);

static void detail_trace_cb(lv_event_t* e) { ui->openTraceForContact(detail_idx); }
static void trace_rerun_cb(lv_event_t* e) { ui->openTraceForContact(detail_idx); }
static void trace_close_cb(lv_event_t* e) { lv_obj_add_flag(trace_scr, LV_OBJ_FLAG_HIDDEN); }
static void trace_setpath_cb(lv_event_t* e) {
  path_from_trace = true;
  lv_obj_add_flag(trace_scr, LV_OBJ_FLAG_HIDDEN);
  path_open_cb(NULL);
}

static void traceHashName(uint8_t hash, char* out, size_t sz) {
  for (int idx = MAX_ANON_CONTACTS; idx < the_mesh.getTotalContactSlots(); idx++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(idx, c)) break;
    if (c.name[0] != 0 && c.id.pub_key[0] == hash) {
      StrHelper::strncpy(out, c.name, sz);
      return;
    }
  }
  snprintf(out, sz, "hop %02X", hash);
}

// ---- manual route picker: up to 3 repeater hops or flood/direct ----
#define PATH_MAX_HOPS 8   // the hop list scrolls, so this is not a screen limit
static lv_obj_t* path_scr;
static lv_obj_t* path_dd[PATH_MAX_HOPS];
static lv_obj_t* path_hop_lbl[PATH_MAX_HOPS];
static lv_obj_t* path_add_btn;
static int path_hops_shown = 1;
static int path_rep_idx[40];   // dropdown option order -> contact index
static int path_rep_count = 0;

// only as many hop rows as are in use, plus the button that reveals the next
static void pathShowHops(int n) {
  if (n < 1) n = 1;
  if (n > PATH_MAX_HOPS) n = PATH_MAX_HOPS;
  path_hops_shown = n;
  for (int i = 0; i < PATH_MAX_HOPS; i++) {
    if (i < n) {
      lv_obj_remove_flag(path_dd[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(path_hop_lbl[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(path_dd[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(path_hop_lbl[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  lv_obj_align(path_add_btn, LV_ALIGN_TOP_LEFT, 20, 4 + n * 38);
  if (n < PATH_MAX_HOPS) lv_obj_remove_flag(path_add_btn, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(path_add_btn, LV_OBJ_FLAG_HIDDEN);
}

static void path_add_cb(lv_event_t* e) { pathShowHops(path_hops_shown + 1); }

static void path_open_cb(lv_event_t* e) {   // NOLINT: forward declared above
  char opts[600] = "(none)";
  path_rep_count = 0;
  for (int idx = MAX_ANON_CONTACTS; idx < the_mesh.getTotalContactSlots() && path_rep_count < 40; idx++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(idx, c)) break;
    if (c.name[0] == 0 || c.type != ADV_TYPE_REPEATER) continue;
    strlcat(opts, "\n", sizeof(opts));
    strlcat(opts, c.name, sizeof(opts));
    path_rep_idx[path_rep_count++] = idx;
  }
  if (path_rep_count == 0) {
    ui->showToast("No repeaters known yet");
    return;
  }
  for (int i = 0; i < PATH_MAX_HOPS; i++) {
    lv_dropdown_set_options(path_dd[i], opts);
    lv_dropdown_set_selected(path_dd[i], 0);
  }

  // show the path already in use, so it can be edited rather than retyped
  int hops = 0;
  ContactInfo cur;
  if (the_mesh.getContactByIdx(detail_idx, cur) && cur.out_path_len != OUT_PATH_UNKNOWN) {
    for (int h = 0; h < cur.out_path_len && h < PATH_MAX_HOPS; h++) {
      for (int o = 0; o < path_rep_count; o++) {
        ContactInfo rep_c;
        if (!the_mesh.getContactByIdx(path_rep_idx[o], rep_c)) continue;
        if (rep_c.id.pub_key[0] == cur.out_path[h]) { lv_dropdown_set_selected(path_dd[h], o + 1); break; }
      }
      hops++;
    }
  }
  pathShowHops(hops);
  lv_obj_remove_flag(path_scr, LV_OBJ_FLAG_HIDDEN);
}

static void path_flood_cb(lv_event_t* e) {
  ContactInfo c;
  if (the_mesh.getContactByIdx(detail_idx, c)) {
    ContactInfo* live = the_mesh.lookupContactByPubKey(c.id.pub_key, 6);
    if (live != NULL) {
      the_mesh.resetPathTo(*live);
      the_mesh.saveContacts();
      ui->showToast("Path reset to flood");
    }
  }
  lv_obj_add_flag(path_scr, LV_OBJ_FLAG_HIDDEN);
  if (path_from_trace) { path_from_trace = false; ui->openTraceForContact(detail_idx); }
  else ui->openContactDetail(detail_idx);
}

static void path_apply_cb(lv_event_t* e) {
  ContactInfo c;
  if (!the_mesh.getContactByIdx(detail_idx, c)) return;
  ContactInfo* live = the_mesh.lookupContactByPubKey(c.id.pub_key, 6);
  if (live == NULL) return;

  uint8_t pos = 0;
  for (int i = 0; i < PATH_MAX_HOPS; i++) {
    int sel = (int) lv_dropdown_get_selected(path_dd[i]);
    if (sel < 1 || sel > path_rep_count) continue;   // "(none)"
    ContactInfo rep;
    if (!the_mesh.getContactByIdx(path_rep_idx[sel - 1], rep)) continue;
    pos += rep.id.copyHashTo(&live->out_path[pos]);
  }
  live->out_path_len = pos;   // 0 hops = zero-hop direct
  the_mesh.saveContacts();
  ui->showToast(pos == 0 ? "Path set: direct (0 hops)" : "Path set");
  lv_obj_add_flag(path_scr, LV_OBJ_FLAG_HIDDEN);
  if (path_from_trace) { path_from_trace = false; ui->openTraceForContact(detail_idx); }
  else ui->openContactDetail(detail_idx);
}

static void path_cancel_cb(lv_event_t* e) {
  lv_obj_add_flag(path_scr, LV_OBJ_FLAG_HIDDEN);
  if (path_from_trace) { path_from_trace = false; lv_obj_remove_flag(trace_scr, LV_OBJ_FLAG_HIDDEN); }
}

static void detail_share_cb(lv_event_t* e) {
  ContactInfo c;
  if (!the_mesh.getContactByIdx(detail_idx, c)) return;
  ui->showToast(the_mesh.shareContactZeroHop(c) ? "Contact shared (zero hop)" : "Share failed");
}

static void detail_remove_cb(lv_event_t* e) {
  if (!detail_remove_armed) {
    detail_remove_armed = true;
    lv_label_set_text(detail_remove_lbl, "SURE?");
    return;
  }
  ContactInfo c;
  if (the_mesh.getContactByIdx(detail_idx, c)) {
    ContactInfo* live = the_mesh.lookupContactByPubKey(c.id.pub_key, 6);
    if (live != NULL && the_mesh.removeContact(*live)) {
      the_mesh.saveContacts();
      ui->showToast("Contact removed");
    } else {
      ui->showToast("Remove failed");
    }
  }
  lv_obj_add_flag(detail_scr, LV_OBJ_FLAG_HIDDEN);
  ui->refreshContactsTab();
  ui->refreshChatsTab();
}

static void rep_back_cb(lv_event_t* e) { ui->closeRepeaterManager(); }

static const char* REP_ACTIONS[6] = {"advert", "clock sync", "ver", "neighbors", "clock", "reboot"};
static void rep_cmd_cb(lv_event_t* e) {
  int i = (int)(intptr_t) lv_event_get_user_data(e);
  if (i < 0 || i >= 6) return;
  rep_show_reply = true;   // results land in the card at the top
  // never push an unset clock to a repeater: a bad admin sync is exactly how
  // repeaters end up years in the past for everyone
  if (strcmp(REP_ACTIONS[i], "clock sync") == 0 && rtc_clock.getCurrentTime() < 1600000000UL) {
    ui->showToast("This node has no valid time yet (GPS or app first)");
    return;
  }
  ui->sendRepeaterCommand(REP_ACTIONS[i]);
}

static void rep_status_cb(lv_event_t* e) {
  rep_show_reply = false;   // back to the status view
  ui->requestRepeaterStatus();
}

// used by both the Login button and the keyboard's accept key
static void repeaterLoginFromField() {
  const char* pw = lv_textarea_get_text(rep_pw_ta);
  if (pw == NULL || pw[0] == 0) {
    pw = ui->savedRepeaterPw();   // empty field: fall back to saved password
    if (pw == NULL) {
      ui->showToast("Enter the repeater admin password");
      return;
    }
  }
  rep_pending_remember = rep_remember_cb != NULL
                         && lv_obj_has_state(rep_remember_cb, LV_STATE_CHECKED);
  bool flood = rep_flood_on;
  StrHelper::strncpy(rep_pending_pw, pw, sizeof(rep_pending_pw));
  lv_obj_add_flag(rep_kb, LV_OBJ_FLAG_HIDDEN);
  ui->repeaterLogin(pw, flood);
  lv_textarea_set_text(rep_pw_ta, "");
}

static void rep_login_btn_cb(lv_event_t* e) { repeaterLoginFromField(); }

static void rep_flood_toggle_cb(lv_event_t* e) {
  rep_flood_on = lv_obj_has_state((lv_obj_t*) lv_event_get_target(e), LV_STATE_CHECKED);
}

static void rep_terminal_cb(lv_event_t* e) { ui->openConsoleThread(); }

static void rep_auto_cb(lv_event_t* e) {
  rep_auto_refresh = lv_obj_has_state((lv_obj_t*) lv_event_get_target(e), LV_STATE_CHECKED);
  prefWriteInt("/rep_auto", rep_auto_refresh ? 1 : 0);
}

// the keyboard covers the lower screen: keep the field and checkbox above it
static void repLoginLayout(bool kb_visible) {
  if (rep_pw_ta == NULL) return;
  if (kb_visible) {
    lv_obj_add_flag(rep_hint_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(rep_login_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(rep_flood_cb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(rep_pw_ta, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_align(rep_remember_cb, LV_ALIGN_TOP_MID, 0, 30);
  } else {
    lv_obj_remove_flag(rep_hint_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(rep_login_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(rep_flood_cb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(rep_pw_ta, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_align(rep_remember_cb, LV_ALIGN_TOP_MID, 0, 58);
  }
}

static void rep_pw_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    lv_obj_remove_flag(rep_kb, LV_OBJ_FLAG_HIDDEN);
    repLoginLayout(true);
  } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(rep_kb, LV_OBJ_FLAG_HIDDEN);
    repLoginLayout(false);
  } else if (code == LV_EVENT_READY) {
    repeaterLoginFromField();   // same path as the Login button
  }
}

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

static void thread_back_cb(lv_event_t* e) { ui->closeThread(); }
static void thread_clear_cb(lv_event_t* e) { ui->clearCurrentThread(); }
static void thread_route_cb(lv_event_t* e) { ui->openRouteForThread(); }
static void bubble_retry_cb(lv_event_t* e) { ui->resendMessage((int)(intptr_t) lv_event_get_user_data(e)); }

static void fmtAge(char* buf, size_t sz, uint32_t ts) {
  long secs = (long) rtc_clock.getCurrentTime() - (long) ts;
  if (secs < 0) secs = 0;
  if (secs < 60) snprintf(buf, sz, "now");
  else if (secs < 3600) snprintf(buf, sz, "%ldm", secs / 60);
  else if (secs < 86400) snprintf(buf, sz, "%ldh", secs / 3600);
  else snprintf(buf, sz, "%ldd", secs / 86400);
}

static void kb_show(bool show) {
  if (show) {
    lv_obj_remove_flag(thread_kb, LV_OBJ_FLAG_HIDDEN);
    // lift the input row above the keyboard so typed text stays visible
    lv_obj_align(thread_input_row, LV_ALIGN_BOTTOM_MID, 0, -KB_HEIGHT);
  } else {
    lv_obj_add_flag(thread_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(thread_input_row, LV_ALIGN_BOTTOM_MID, 0, 0);
  }
}

static void ta_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    // CLICKED too: a still-focused textarea fires no FOCUSED on re-tap,
    // which left the keyboard unreachable after dismissing it
    kb_show(true);
  } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL) {
    kb_show(false);
  } else if (code == LV_EVENT_READY) {   // keyboard checkmark
    const char* txt = lv_textarea_get_text(thread_ta);
    if (txt != NULL && txt[0] != 0) {
      ui->sendFromThread(txt);
      lv_textarea_set_text(thread_ta, "");
    }
    kb_show(false);
  }
}

// ---- quick reply panel (pops up over the thread, one tap to send) ----
static void qrPanelClose() {
  if (qr_panel != NULL) {
    lv_obj_delete(qr_panel);
    qr_panel = NULL;
  }
}

static void qr_pick_cb(lv_event_t* e) {
  int i = (int)(intptr_t) lv_event_get_user_data(e);
  qrPanelClose();
  if (i >= 0 && i < qr_count && qr_texts[i][0] != 0) ui->sendFromThread(qr_texts[i]);
}

static void qr_loc_cb(lv_event_t* e) {
  qrPanelClose();
  SensorManager* s = ui->sensors();
  LocationProvider* nmea = s != NULL ? s->getLocationProvider() : NULL;
  if (nmea == NULL || !nmea->isValid()) {
    ui->showToast("No GPS fix yet");
    return;
  }
  char msg[48];
  snprintf(msg, sizeof(msg), "I'm at %.5f, %.5f",
           nmea->getLatitude() / 1000000.0, nmea->getLongitude() / 1000000.0);
  ui->sendFromThread(msg);
}

static void qr_open_cb(lv_event_t* e) {
  if (qr_panel != NULL) { qrPanelClose(); return; }
  kb_show(false);

  qr_panel = lv_obj_create(thread_scr);
  lv_obj_set_size(qr_panel, 250, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(qr_panel, 168, 0);
  lv_obj_align(qr_panel, LV_ALIGN_BOTTOM_LEFT, 2, -38);
  lv_obj_set_style_bg_color(qr_panel, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_color(qr_panel, lv_color_hex(COL_ACCENT_D), 0);
  lv_obj_set_style_pad_all(qr_panel, 4, 0);
  lv_obj_set_flex_flow(qr_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(qr_panel, 3, 0);

  for (int i = 0; i < qr_count; i++) {
    lv_obj_t* b = lv_button_create(qr_panel);
    lv_obj_set_size(b, LV_PCT(100), 26);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_BG), 0);
    lv_obj_add_event_cb(b, qr_pick_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, qr_texts[i]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  }

  SensorManager* s = ui->sensors();
  LocationProvider* nmea = s != NULL ? s->getLocationProvider() : NULL;
  lv_obj_t* b = lv_button_create(qr_panel);
  lv_obj_set_size(b, LV_PCT(100), 26);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_BG), 0);
  lv_obj_add_event_cb(b, qr_loc_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, LV_SYMBOL_GPS " Send my location");
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  if (nmea == NULL || !nmea->isValid()) lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
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
  const char* raw_name = lv_textarea_get_text(addch_name_ta);
  while (*raw_name == '#') raw_name++;   // stored names carry no leading '#'
  if (raw_name[0] == 0) {
    ui->showToast("Channel needs a name");
    return;
  }
  const char* psk = lv_textarea_get_text(addch_psk_ta);
  char psk_b64[32];
  if (psk == NULL || psk[0] == 0) {
    uint8_t key[16];
    esp_fill_random(key, sizeof(key));
    unsigned int b64len = encode_base64(key, sizeof(key), (unsigned char*)psk_b64);
    psk_b64[b64len] = 0;
    psk = psk_b64;
  }

  // NOTE: BaseChatMesh::addChannel writes at its own counter which ignores
  // flash-loaded channels (it would overwrite slot 0) - place the channel
  // into the first truly empty slot ourselves
  int slot = -1;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails t;
    if (!the_mesh.getChannel(i, t)) break;
    const char* existing = t.name[0] == '#' ? t.name + 1 : t.name;
    if (t.name[0] != 0 && strcmp(existing, raw_name) == 0) {
      ui->showToast("Channel already exists");
      return;
    }
    if (t.name[0] == 0 && slot < 0) slot = i;
  }
  if (slot < 0) {
    ui->showToast("Channel table full");
    return;
  }

  ChannelDetails ch;
  memset(&ch, 0, sizeof(ch));
  int keylen = decode_base64((const unsigned char*)psk, strlen(psk), ch.channel.secret);
  if (keylen != 16 && keylen != 32) {
    ui->showToast("PSK must be 16/32 bytes b64");
    return;
  }
  mesh::Utils::sha256(ch.channel.hash, sizeof(ch.channel.hash), ch.channel.secret, keylen);
  StrHelper::strncpy(ch.name, raw_name, sizeof(ch.name));
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
  bool ok = flood ? the_mesh.advertFlood() : the_mesh.advert();
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

// ---------------------------------------------------------------------------
// UITask
// ---------------------------------------------------------------------------
void UITask::begin(DisplayDriver* display_drv, SensorManager* sensors, NodePrefs* node_prefs) {
  ui = this;
  _sensors = sensors;
  _node_prefs = node_prefs;

  chatStoreLoad();
  savedPwLoad();
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
  buildPolishOverlays();   // wizard / about / banner / channel options
  wizard_pending = !SPIFFS.exists("/setup_done");
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

void UITask::buildRepeaterScr() {
  lv_obj_t* scr = lv_screen_active();
  rep_scr = lv_obj_create(scr);
  lv_obj_set_size(rep_scr, 320, 240);
  lv_obj_align(rep_scr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(rep_scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(rep_scr, 0, 0);
  lv_obj_set_style_radius(rep_scr, 0, 0);
  lv_obj_set_style_pad_all(rep_scr, 0, 0);
  lv_obj_add_flag(rep_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(rep_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* hdr = lv_obj_create(rep_scr);
  lv_obj_set_size(hdr, 320, 26);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 2, 0);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = lv_button_create(hdr);
  lv_obj_set_size(back, 40, 20);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_add_event_cb(back, rep_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* back_lbl = lv_label_create(back);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
  lv_obj_center(back_lbl);

  rep_title = lv_label_create(hdr);
  lv_label_set_text(rep_title, "");
  lv_label_set_long_mode(rep_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(rep_title, 250);
  lv_obj_align(rep_title, LV_ALIGN_LEFT_MID, 50, 0);

  rep_body = lv_obj_create(rep_scr);
  lv_obj_set_size(rep_body, 320, 240 - 26);
  lv_obj_align(rep_body, LV_ALIGN_TOP_MID, 0, 26);
  lv_obj_set_style_bg_color(rep_body, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(rep_body, 0, 0);
  lv_obj_set_style_pad_all(rep_body, 8, 0);

  rep_kb = lv_keyboard_create(rep_scr);
  kbAttachShiftBehavior(rep_kb);
  lv_obj_set_size(rep_kb, 320, KB_HEIGHT);
  lv_obj_align(rep_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(rep_kb, LV_OBJ_FLAG_HIDDEN);

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
  lv_textarea_set_placeholder_text(addch_psk_ta, "PSK base64 (blank = random)");
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

  detail_scr = lv_obj_create(lv_screen_active());
  lv_obj_set_size(detail_scr, 320, 240);
  lv_obj_align(detail_scr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(detail_scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(detail_scr, 0, 0);
  lv_obj_set_style_radius(detail_scr, 0, 0);
  lv_obj_set_style_pad_all(detail_scr, 8, 0);
  lv_obj_add_flag(detail_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(detail_scr, LV_OBJ_FLAG_SCROLLABLE);

  detail_title = lv_label_create(detail_scr);
  lv_label_set_text(detail_title, "");
  lv_obj_set_style_text_color(detail_title, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_text_font(detail_title, &lv_font_montserrat_16, 0);
  lv_obj_align(detail_title, LV_ALIGN_TOP_LEFT, 0, 0);

  detail_info = lv_label_create(detail_scr);
  lv_label_set_text(detail_info, "");
  lv_obj_set_width(detail_info, 300);
  lv_obj_align(detail_info, LV_ALIGN_TOP_LEFT, 0, 26);

  lv_obj_t* b;
  lv_obj_t* bl;
  b = lv_button_create(detail_scr); lv_obj_set_size(b, 148, 32);
  lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, -76);
  lv_obj_add_event_cb(b, detail_msg_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(b); lv_label_set_text(bl, LV_SYMBOL_ENVELOPE " Message"); lv_obj_center(bl);

  b = lv_button_create(detail_scr); lv_obj_set_size(b, 148, 32);
  lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, -76);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(b, path_open_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(b); lv_label_set_text(bl, LV_SYMBOL_SHUFFLE " Set path"); lv_obj_center(bl);

  path_scr = lv_obj_create(lv_screen_active());
  lv_obj_set_size(path_scr, 320, 240);
  lv_obj_align(path_scr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(path_scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(path_scr, 0, 0);
  lv_obj_set_style_radius(path_scr, 0, 0);
  lv_obj_set_style_pad_all(path_scr, 8, 0);
  lv_obj_add_flag(path_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(path_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* pt = lv_label_create(path_scr);
  lv_label_set_text(pt, "Path via repeaters (in order)");
  lv_obj_set_style_text_color(pt, lv_color_hex(COL_ACCENT), 0);
  lv_obj_align(pt, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* ph = lv_label_create(path_scr);
  lv_label_set_text(ph, "hop 1 must be within direct range");
  lv_obj_set_style_text_font(ph, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(ph, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(ph, LV_ALIGN_TOP_RIGHT, 0, 2);

  lv_obj_t* hop_list = lv_obj_create(path_scr);
  lv_obj_set_size(hop_list, 304, 158);
  lv_obj_align(hop_list, LV_ALIGN_TOP_MID, 0, 18);
  lv_obj_set_style_bg_opa(hop_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hop_list, 0, 0);
  lv_obj_set_style_pad_all(hop_list, 0, 0);
  lv_obj_set_scroll_dir(hop_list, LV_DIR_VER);

  for (int i = 0; i < PATH_MAX_HOPS; i++) {
    path_hop_lbl[i] = lv_label_create(hop_list);
    lv_label_set_text_fmt(path_hop_lbl[i], "%d", i + 1);
    lv_obj_set_style_text_color(path_hop_lbl[i], lv_color_hex(COL_MUTED), 0);
    lv_obj_align(path_hop_lbl[i], LV_ALIGN_TOP_LEFT, 2, 10 + i * 38);
    path_dd[i] = lv_dropdown_create(hop_list);
    lv_obj_set_size(path_dd[i], 274, 32);
    lv_obj_align(path_dd[i], LV_ALIGN_TOP_LEFT, 20, 2 + i * 38);
  }

  path_add_btn = lv_button_create(hop_list);
  lv_obj_set_size(path_add_btn, 110, 30);
  lv_obj_set_style_bg_color(path_add_btn, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(path_add_btn, path_add_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(path_add_btn);
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
  lv_label_set_text(bl, LV_SYMBOL_PLUS " Add hop");
  lv_obj_center(bl);
  pathShowHops(1);

  b = lv_button_create(path_scr); lv_obj_set_size(b, 96, 32);
  lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, -4);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(b, path_flood_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(b); lv_label_set_text(bl, "Reset (flood)"); lv_obj_center(bl);

  b = lv_button_create(path_scr); lv_obj_set_size(b, 96, 32);
  lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_add_event_cb(b, path_apply_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(b); lv_label_set_text(bl, LV_SYMBOL_OK " Apply"); lv_obj_center(bl);

  b = lv_button_create(path_scr); lv_obj_set_size(b, 96, 32);
  lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, -4);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(b, path_cancel_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(b); lv_label_set_text(bl, LV_SYMBOL_CLOSE " Cancel"); lv_obj_center(bl);

  b = lv_button_create(detail_scr); lv_obj_set_size(b, 148, 32);
  lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, -40);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(b, detail_share_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(b); lv_label_set_text(bl, LV_SYMBOL_UPLOAD " Share"); lv_obj_center(bl);

  b = lv_button_create(detail_scr); lv_obj_set_size(b, 148, 32);
  lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, -40);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x7A2020), 0);
  lv_obj_add_event_cb(b, detail_remove_cb, LV_EVENT_CLICKED, NULL);
  detail_remove_lbl = lv_label_create(b);
  lv_label_set_text(detail_remove_lbl, LV_SYMBOL_TRASH " Remove");
  lv_obj_center(detail_remove_lbl);

  detail_trace_btn = lv_button_create(detail_scr); lv_obj_set_size(detail_trace_btn, 148, 32);
  lv_obj_align(detail_trace_btn, LV_ALIGN_BOTTOM_LEFT, 0, -2);
  lv_obj_add_event_cb(detail_trace_btn, detail_trace_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(detail_trace_btn); lv_label_set_text(bl, LV_SYMBOL_GPS " Trace path"); lv_obj_center(bl);

  b = lv_button_create(detail_scr); lv_obj_set_size(b, 148, 32);
  lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(b, detail_close_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(b); lv_label_set_text(bl, LV_SYMBOL_CLOSE " Close"); lv_obj_center(bl);

  trace_scr = lv_obj_create(lv_screen_active());
  lv_obj_set_size(trace_scr, 320, 240);
  lv_obj_align(trace_scr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(trace_scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(trace_scr, 0, 0);
  lv_obj_set_style_radius(trace_scr, 0, 0);
  lv_obj_set_style_pad_all(trace_scr, 8, 0);
  lv_obj_add_flag(trace_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(trace_scr, LV_OBJ_FLAG_SCROLLABLE);

  trace_title = lv_label_create(trace_scr);
  lv_label_set_text(trace_title, "");
  lv_obj_set_style_text_color(trace_title, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_text_font(trace_title, &lv_font_montserrat_16, 0);
  lv_obj_set_width(trace_title, 304);
  lv_label_set_long_mode(trace_title, LV_LABEL_LONG_DOT);
  lv_obj_align(trace_title, LV_ALIGN_TOP_LEFT, 0, 0);

  // long hop lists scroll rather than running past the buttons
  lv_obj_t* trace_body = lv_obj_create(trace_scr);
  lv_obj_set_size(trace_body, 304, 158);
  lv_obj_align(trace_body, LV_ALIGN_TOP_LEFT, 0, 26);
  lv_obj_set_style_bg_opa(trace_body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(trace_body, 0, 0);
  lv_obj_set_style_pad_all(trace_body, 0, 0);
  lv_obj_set_scroll_dir(trace_body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(trace_body, LV_SCROLLBAR_MODE_AUTO);

  trace_lbl = lv_label_create(trace_body);
  lv_label_set_text(trace_lbl, "");
  lv_obj_set_width(trace_lbl, 296);
  lv_label_set_long_mode(trace_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_align(trace_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

  b = lv_button_create(trace_scr); lv_obj_set_size(b, 98, 32);
  lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, -2);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(b, trace_setpath_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(b);
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
  lv_label_set_text(bl, LV_SYMBOL_SHUFFLE " Set path"); lv_obj_center(bl);

  b = lv_button_create(trace_scr); lv_obj_set_size(b, 98, 32);
  lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_add_event_cb(b, trace_rerun_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(b);
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
  lv_label_set_text(bl, LV_SYMBOL_REFRESH " Trace"); lv_obj_center(bl);

  b = lv_button_create(trace_scr); lv_obj_set_size(b, 98, 32);
  lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
  lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(b, trace_close_cb, LV_EVENT_CLICKED, NULL);
  bl = lv_label_create(b);
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_12, 0);
  lv_label_set_text(bl, LV_SYMBOL_CLOSE " Close"); lv_obj_center(bl);

  // sleep shield: topmost, eats the first tap when the screen is dark
  sleep_shield = lv_obj_create(lv_layer_top());
  lv_obj_set_size(sleep_shield, 320, 240);
  lv_obj_set_style_bg_opa(sleep_shield, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(sleep_shield, 0, 0);
  lv_obj_add_flag(sleep_shield, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(sleep_shield, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(sleep_shield, sleep_shield_cb, LV_EVENT_CLICKED, NULL);
}

void UITask::refreshRepeaterScr() {
  // detach the keyboard BEFORE cleaning: lv_keyboard_set_textarea dereferences
  // its previous textarea, which lv_obj_clean is about to free (use-after-free)
  lv_keyboard_set_textarea(rep_kb, NULL);
  lv_obj_clean(rep_body);
  lv_label_set_text(rep_title, _rep_name);

  if (!_rep_logged_in) {
    bool have_saved = savedPwFor(_rep_key) != NULL;
    rep_hint_lbl = lv_label_create(rep_body);
    lv_label_set_text(rep_hint_lbl, _rep_logging_in ? "Logging in..."
                         : have_saved ? "Saved password - just tap Login"
                                      : "Admin login required");
    lv_obj_set_style_text_color(rep_hint_lbl, lv_color_hex(have_saved ? COL_ACCENT : COL_MUTED), 0);
    lv_obj_align(rep_hint_lbl, LV_ALIGN_TOP_MID, 0, 4);

    rep_pw_ta = lv_textarea_create(rep_body);
    lv_textarea_set_one_line(rep_pw_ta, true);
    lv_textarea_set_password_mode(rep_pw_ta, true);
    lv_textarea_set_placeholder_text(rep_pw_ta, have_saved ? "********" : "Admin password");
    lv_textarea_set_max_length(rep_pw_ta, 40);
    lv_obj_set_width(rep_pw_ta, 280);
    lv_obj_set_height(rep_pw_ta, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(rep_pw_ta, 5, 0);
    lv_obj_set_scroll_dir(rep_pw_ta, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(rep_pw_ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_opa(rep_pw_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_opa(rep_pw_ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_align(rep_pw_ta, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_add_event_cb(rep_pw_ta, rep_pw_event_cb, LV_EVENT_ALL, NULL);
    lv_keyboard_set_textarea(rep_kb, rep_pw_ta);

    rep_remember_cb = lv_checkbox_create(rep_body);
    lv_checkbox_set_text(rep_remember_cb, "Remember password");
    lv_obj_set_style_text_font(rep_remember_cb, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rep_remember_cb, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(rep_remember_cb, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_add_state(rep_remember_cb, LV_STATE_CHECKED);   // on by default

    // flood the login when the stored route has gone stale
    rep_flood_cb = lv_checkbox_create(rep_body);
    lv_checkbox_set_text(rep_flood_cb, "Flood connect (ignore known path)");
    lv_obj_set_style_text_font(rep_flood_cb, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rep_flood_cb, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(rep_flood_cb, LV_ALIGN_TOP_MID, 0, 80);
    if (rep_flood_on) lv_obj_add_state(rep_flood_cb, LV_STATE_CHECKED);   // keep the choice
    lv_obj_add_event_cb(rep_flood_cb, rep_flood_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    rep_login_btn = lv_button_create(rep_body);
    lv_obj_set_size(rep_login_btn, 160, 36);
    lv_obj_align(rep_login_btn, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_add_event_cb(rep_login_btn, rep_login_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* blbl = lv_label_create(rep_login_btn);
    lv_label_set_text(blbl, LV_SYMBOL_OK " Login");
    lv_obj_center(blbl);
  } else {
    lv_obj_t* card = lv_obj_create(rep_body);
    lv_obj_set_size(card, 300, 92);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_scroll_dir(card, LV_DIR_VER);   // long replies (neighbour lists) scroll
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);

    rep_card_title = lv_label_create(card);
    lv_obj_set_style_text_font(rep_card_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rep_card_title, lv_color_hex(COL_ACCENT), 0);
    lv_obj_align(rep_card_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(rep_card_title, rep_show_reply ? rep_last_cmd : "Status");

    rep_stats_lbl = lv_label_create(card);
    lv_obj_set_width(rep_stats_lbl, 284);
    lv_obj_align(rep_stats_lbl, LV_ALIGN_TOP_LEFT, 0, 15);
    lv_label_set_long_mode(rep_stats_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(rep_stats_lbl, &lv_font_montserrat_12, 0);
    if (rep_show_reply) {
      lv_label_set_text(rep_stats_lbl, rep_last_reply);
    } else if (rep_stats_valid) {
      uint32_t up = rep_stats.total_up_time_secs;
      char up_s[20];
      if (up >= 86400) snprintf(up_s, sizeof(up_s), "%lud %luh", (unsigned long)(up / 86400), (unsigned long)((up % 86400) / 3600));
      else snprintf(up_s, sizeof(up_s), "%luh %lum", (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60));
      lv_label_set_text_fmt(rep_stats_lbl,
        "Batt %u.%02uV   Up %s\n"
        "Noise %d  RSSI %d  SNR %d\n"
        "Sent %lu (f%lu d%lu)\n"
        "Recv %lu (f%lu d%lu)\n"
        "Air TX %lus RX %lus  Q%u  E%u",
        rep_stats.batt_milli_volts / 1000, (rep_stats.batt_milli_volts % 1000) / 10, up_s,
        (int)rep_stats.noise_floor, (int)rep_stats.last_rssi, (int)(rep_stats.last_snr / 4),
        (unsigned long)rep_stats.n_packets_sent, (unsigned long)rep_stats.n_sent_flood, (unsigned long)rep_stats.n_sent_direct,
        (unsigned long)rep_stats.n_packets_recv, (unsigned long)rep_stats.n_recv_flood, (unsigned long)rep_stats.n_recv_direct,
        (unsigned long)rep_stats.total_air_time_secs, (unsigned long)rep_stats.total_rx_air_time_secs,
        (unsigned)rep_stats.curr_tx_queue_len, (unsigned)rep_stats.err_events);
    } else {
      lv_label_set_text(rep_stats_lbl, rep_stats_waiting ? "Fetching status..." : "No status yet - tap refresh");
      lv_obj_set_style_text_color(rep_stats_lbl, lv_color_hex(COL_MUTED), 0);
    }

    lv_obj_t* refresh = lv_button_create(rep_body);
    lv_obj_set_size(refresh, 44, 26);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(refresh, rep_status_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* rl = lv_label_create(refresh);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH);
    lv_obj_center(rl);

    static const char* action_labels[6] = {
      LV_SYMBOL_UPLOAD " Advert", LV_SYMBOL_LOOP " Clock sync", LV_SYMBOL_FILE " Version",
      LV_SYMBOL_WIFI " Neighbors", LV_SYMBOL_REFRESH " Clock", LV_SYMBOL_WARNING " Reboot"
    };
    for (int i = 0; i < 6; i++) {
      lv_obj_t* btn = lv_button_create(rep_body);
      lv_obj_set_size(btn, 96, 30);
      lv_obj_align(btn, LV_ALIGN_TOP_LEFT, (i % 3) * 102, 98 + (i / 3) * 34);
      if (i == 5) lv_obj_set_style_bg_color(btn, lv_color_hex(0x7A2020), 0);
      lv_obj_add_event_cb(btn, rep_cmd_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
      lv_obj_t* blbl = lv_label_create(btn);
      lv_obj_set_style_text_font(blbl, &lv_font_montserrat_12, 0);
      lv_label_set_text(blbl, action_labels[i]);
      lv_obj_center(blbl);
    }

    lv_obj_t* auto_cb = lv_checkbox_create(rep_body);
    lv_checkbox_set_text(auto_cb, "auto-refresh");
    lv_obj_set_style_text_font(auto_cb, &lv_font_montserrat_12, 0);
    lv_obj_align(auto_cb, LV_ALIGN_TOP_LEFT, 2, 172);
    if (rep_auto_refresh) lv_obj_add_state(auto_cb, LV_STATE_CHECKED);
    lv_obj_add_event_cb(auto_cb, rep_auto_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t* btn = lv_button_create(rep_body);
    lv_obj_set_size(btn, 118, 28);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, 0, 166);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
    lv_obj_add_event_cb(btn, rep_terminal_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* blbl = lv_label_create(btn);
    lv_obj_set_style_text_font(blbl, &lv_font_montserrat_12, 0);
    lv_label_set_text(blbl, LV_SYMBOL_KEYBOARD " CLI");
    lv_obj_center(blbl);
  }
}

void UITask::openRepeaterManager(const ContactInfo& contact) {
  // every visit starts at the login screen
  rep_show_reply = false;
  _rep_logged_in = false;
  _rep_logging_in = false;
  if (memcmp(_rep_key, contact.id.pub_key, 6) != 0) {   // different repeater
    rep_stats_valid = false;
    rep_stats_waiting = false;
    rep_last_reply[0] = 0;
  }
  memcpy(_rep_key, contact.id.pub_key, 6);
  StrHelper::strncpy(_rep_name, contact.name, sizeof(_rep_name));
  refreshRepeaterScr();
  lv_obj_remove_flag(rep_scr, LV_OBJ_FLAG_HIDDEN);
}

void UITask::closeRepeaterManager() {
  lv_obj_add_flag(rep_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(rep_kb, LV_OBJ_FLAG_HIDDEN);
  _rep_logged_in = false;   // re-authenticate on the next visit
  _rep_logging_in = false;
}

void UITask::repeaterLogin(const char* password, bool flood) {
  ContactInfo* c = the_mesh.lookupContactByPubKey(_rep_key, 6);
  if (c == NULL) { showToast("Contact gone"); return; }
  if (flood) {
    the_mesh.resetPathTo(*c);
    the_mesh.saveContacts();
  }
  if (the_mesh.uiLogin(*c, password) == MSG_SEND_FAILED) {
    showToast("Login send failed");
  } else {
    _rep_logging_in = true;
    memcpy(_pending_login_key, _rep_key, 6);
    _pending_login_deadline = millis() + 30000;
    refreshRepeaterScr();
  }
}

void UITask::sendRepeaterCommand(const char* cmd) {
  ContactInfo* c = the_mesh.lookupContactByPubKey(_rep_key, 6);
  if (c == NULL) { showToast("Contact gone"); return; }
  uint32_t est_timeout;
  uint32_t timestamp = rtc_clock.getCurrentTimeUnique();
  if (the_mesh.sendCommandData(*c, timestamp, 0, TXT_TYPE_CLI_DATA, cmd, est_timeout) == MSG_SEND_FAILED) {
    showToast("Send failed");
  } else {
    chatStorePush(_rep_key, true, timestamp, cmd);   // terminal history keeps everything
    StrHelper::strncpy(rep_last_cmd, cmd, sizeof(rep_last_cmd));
    rep_show_reply = true;
    snprintf(rep_last_reply, sizeof(rep_last_reply), "waiting for reply...");
    if (!lv_obj_has_flag(rep_scr, LV_OBJ_FLAG_HIDDEN)) refreshRepeaterScr();
  }
}

void UITask::openConsoleThread() {
  lv_obj_add_flag(rep_scr, LV_OBJ_FLAG_HIDDEN);   // manager under terminal, not over it
  lv_obj_add_flag(rep_kb, LV_OBJ_FLAG_HIDDEN);
  _thread_clear_armed = false;
  memcpy(_thread_key, _rep_key, 6);
  _thread_is_channel = false;
  _thread_is_console = true;
  char title[40];
  snprintf(title, sizeof(title), "@%s", _rep_name);
  StrHelper::strncpy(_thread_name, title, sizeof(_thread_name));
  lv_label_set_text(thread_title, title);
  lv_textarea_set_placeholder_text(thread_ta, "Command...");
  lv_obj_add_flag(thread_qr_btn, LV_OBJ_FLAG_HIDDEN);   // canned chat replies make no sense as CLI
  refreshThread();
  lv_obj_remove_flag(thread_scr, LV_OBJ_FLAG_HIDDEN);
  updateThreadSubtitle();
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

  thread_scr = lv_obj_create(scr);
  lv_obj_set_size(thread_scr, 320, 240);
  lv_obj_align(thread_scr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(thread_scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(thread_scr, 0, 0);
  lv_obj_set_style_radius(thread_scr, 0, 0);
  lv_obj_set_style_pad_all(thread_scr, 0, 0);
  lv_obj_add_flag(thread_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(thread_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* hdr = lv_obj_create(thread_scr);
  lv_obj_set_size(hdr, 320, 26);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 2, 0);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = lv_button_create(hdr);
  lv_obj_set_size(back, 40, 20);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_add_event_cb(back, thread_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* back_lbl = lv_label_create(back);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
  lv_obj_center(back_lbl);

  thread_title = lv_label_create(hdr);
  lv_label_set_text(thread_title, "");
  lv_label_set_long_mode(thread_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(thread_title, 106);
  lv_obj_align(thread_title, LV_ALIGN_LEFT_MID, 50, 0);

  thread_sub_lbl = lv_label_create(hdr);
  lv_label_set_text(thread_sub_lbl, "");
  lv_obj_set_style_text_font(thread_sub_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(thread_sub_lbl, lv_color_hex(COL_MUTED), 0);
  lv_label_set_long_mode(thread_sub_lbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(thread_sub_lbl, 82);
  lv_obj_align(thread_sub_lbl, LV_ALIGN_LEFT_MID, 160, 0);

  lv_obj_t* route_btn = lv_button_create(hdr);
  lv_obj_set_size(route_btn, 34, 20);
  lv_obj_align(route_btn, LV_ALIGN_RIGHT_MID, -40, 0);
  lv_obj_set_style_bg_color(route_btn, lv_color_hex(COL_BG), 0);
  lv_obj_add_event_cb(route_btn, thread_route_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* route_lbl = lv_label_create(route_btn);
  lv_label_set_text(route_lbl, LV_SYMBOL_SHUFFLE);
  lv_obj_center(route_lbl);

  lv_obj_t* clear_btn = lv_button_create(hdr);
  lv_obj_set_size(clear_btn, 34, 20);
  lv_obj_align(clear_btn, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_set_style_bg_color(clear_btn, lv_color_hex(COL_BG), 0);
  lv_obj_add_event_cb(clear_btn, thread_clear_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* clear_lbl = lv_label_create(clear_btn);
  lv_label_set_text(clear_lbl, LV_SYMBOL_TRASH);
  lv_obj_center(clear_lbl);

  thread_msgs = lv_obj_create(thread_scr);
  lv_obj_set_size(thread_msgs, 320, 240 - 26 - 34);
  lv_obj_align(thread_msgs, LV_ALIGN_TOP_MID, 0, 26);
  lv_obj_set_style_bg_color(thread_msgs, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(thread_msgs, 0, 0);
  lv_obj_set_style_pad_all(thread_msgs, 4, 0);
  lv_obj_set_flex_flow(thread_msgs, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(thread_msgs, 3, 0);

  thread_input_row = lv_obj_create(thread_scr);
  lv_obj_set_size(thread_input_row, 320, 34);
  lv_obj_align(thread_input_row, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(thread_input_row, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(thread_input_row, 0, 0);
  lv_obj_set_style_radius(thread_input_row, 0, 0);
  lv_obj_set_style_pad_all(thread_input_row, 3, 0);
  lv_obj_remove_flag(thread_input_row, LV_OBJ_FLAG_SCROLLABLE);

  thread_qr_btn = lv_button_create(thread_input_row);
  lv_obj_set_size(thread_qr_btn, 30, 28);
  lv_obj_align(thread_qr_btn, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(thread_qr_btn, lv_color_hex(COL_BG), 0);
  lv_obj_add_event_cb(thread_qr_btn, qr_open_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* qrl = lv_label_create(thread_qr_btn);
  lv_label_set_text(qrl, LV_SYMBOL_LIST);
  lv_obj_center(qrl);

  thread_ta = lv_textarea_create(thread_input_row);
  lv_textarea_set_one_line(thread_ta, true);
  lv_textarea_set_placeholder_text(thread_ta, "Message...");
  lv_textarea_set_max_length(thread_ta, 110);
  // height = exactly one text line + padding: zero vertical scroll freedom
  lv_obj_set_width(thread_ta, 278);
  lv_obj_set_height(thread_ta, LV_SIZE_CONTENT);
  lv_obj_align(thread_ta, LV_ALIGN_LEFT_MID, 34, 0);
  lv_obj_set_style_pad_ver(thread_ta, 5, 0);
  lv_obj_set_style_pad_left(thread_ta, 6, 0);
  lv_obj_set_scroll_dir(thread_ta, LV_DIR_HOR);   // horizontal scroll only
  lv_obj_set_scrollbar_mode(thread_ta, LV_SCROLLBAR_MODE_OFF);   // no bouncing edge lines
  // blinking cursor only while actually typing (focused), not at idle
  lv_obj_set_style_opa(thread_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_obj_set_style_opa(thread_ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_event_cb(thread_ta, ta_event_cb, LV_EVENT_ALL, NULL);

  thread_kb = lv_keyboard_create(thread_scr);
  lv_keyboard_set_textarea(thread_kb, thread_ta);
  lv_obj_set_size(thread_kb, 320, KB_HEIGHT);
  lv_obj_align(thread_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(thread_kb, LV_OBJ_FLAG_HIDDEN);
  kbAttachShiftBehavior(thread_kb);
}

void UITask::buildChatsTab(lv_obj_t* parent) {
  lv_obj_set_style_pad_all(parent, 4, 0);
  chats_list = lv_list_create(parent);
  lv_obj_set_size(chats_list, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(chats_list, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(chats_list, 0, 0);
}

void UITask::buildContactsTab(lv_obj_t* parent) {
  lv_obj_set_style_pad_all(parent, 4, 0);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 4, 0);

  lv_obj_t* chips = lv_obj_create(parent);
  lv_obj_set_size(chips, LV_PCT(100), 28);
  lv_obj_set_style_bg_opa(chips, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chips, 0, 0);
  lv_obj_set_style_pad_all(chips, 0, 0);
  lv_obj_set_style_pad_column(chips, 4, 0);
  lv_obj_remove_flag(chips, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
  static const char* names[5] = {"All", "Chat", "Repeater", "Room", "A-Z"};
  for (int i = 0; i < 5; i++) {
    lv_obj_t* b = lv_button_create(chips);
    lv_obj_set_height(b, 26);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_pad_hor(b, 2, 0);
    lv_obj_add_event_cb(b, contact_filter_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, names[i]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_center(l);
    filter_btns[i] = b;
  }
  updateFilterChips();

  contacts_list = lv_list_create(parent);
  lv_obj_set_width(contacts_list, LV_PCT(100));
  lv_obj_set_flex_grow(contacts_list, 1);
  lv_obj_set_style_bg_color(contacts_list, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(contacts_list, 0, 0);
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

// ---- quick replies editor (one reply per line) ----
static void qredit_open_cb(lv_event_t* e) {
  char buf[QR_MAX * QR_TEXT_LEN];
  int off = 0;
  buf[0] = 0;
  for (int i = 0; i < qr_count; i++) {
    off += snprintf(&buf[off], sizeof(buf) - off, "%s\n", qr_texts[i]);
  }
  lv_textarea_set_text(qredit_ta, buf);
  lv_obj_remove_flag(qredit_scr, LV_OBJ_FLAG_HIDDEN);
}

static void qredit_save_cb(lv_event_t* e) {
  const char* txt = lv_textarea_get_text(qredit_ta);
  qr_count = 0;
  const char* p = txt;
  while (*p != 0 && qr_count < QR_MAX) {
    const char* nl = strchr(p, '\n');
    size_t n = nl != NULL ? (size_t)(nl - p) : strlen(p);
    if (n > 0) {
      if (n >= QR_TEXT_LEN) n = QR_TEXT_LEN - 1;
      memcpy(qr_texts[qr_count], p, n);
      qr_texts[qr_count][n] = 0;
      qr_count++;
    }
    if (nl == NULL) break;
    p = nl + 1;
  }
  if (qr_count == 0) qrDefaults();
  qrSave();
  lv_obj_add_flag(qredit_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(qredit_kb, LV_OBJ_FLAG_HIDDEN);
  ui->showToast("Quick replies saved");
}

static void qredit_cancel_cb(lv_event_t* e) {
  lv_obj_add_flag(qredit_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(qredit_kb, LV_OBJ_FLAG_HIDDEN);
}

static void qredit_ta_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
    lv_obj_remove_flag(qredit_kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
    lv_obj_add_flag(qredit_kb, LV_OBJ_FLAG_HIDDEN);
  }
}

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
  lv_obj_add_event_cb(qr_row, qredit_open_cb, LV_EVENT_CLICKED, NULL);
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
  lv_obj_add_event_cb(btn, about_open_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(btn);
  lv_label_set_text(lbl, LV_SYMBOL_FILE " About & help");
  lv_obj_center(lbl);

  btn = lv_button_create(parent);
  lv_obj_set_size(btn, LV_PCT(100), 36);
  lv_obj_add_event_cb(btn, reboot_btn_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(btn);
  lv_label_set_text(lbl, LV_SYMBOL_REFRESH " Reboot");
  lv_obj_center(lbl);

  qredit_scr = lv_obj_create(lv_layer_top());
  lv_obj_set_size(qredit_scr, 320, 240);
  lv_obj_set_style_bg_color(qredit_scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(qredit_scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(qredit_scr, 0, 0);
  lv_obj_set_style_radius(qredit_scr, 0, 0);
  lv_obj_set_style_pad_all(qredit_scr, 8, 0);
  lv_obj_add_flag(qredit_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(qredit_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* qt = lv_label_create(qredit_scr);
  lv_label_set_text(qt, "Quick replies");
  lv_obj_set_style_text_color(qt, lv_color_hex(COL_ACCENT), 0);
  lv_obj_align(qt, LV_ALIGN_TOP_LEFT, 0, 6);

  lv_obj_t* qb = lv_button_create(qredit_scr);
  lv_obj_set_size(qb, 70, 28);
  lv_obj_align(qb, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(qb, lv_color_hex(COL_CARD), 0);
  lv_obj_add_event_cb(qb, qredit_cancel_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(qb); lv_label_set_text(lbl, LV_SYMBOL_CLOSE); lv_obj_center(lbl);

  qb = lv_button_create(qredit_scr);
  lv_obj_set_size(qb, 70, 28);
  lv_obj_align(qb, LV_ALIGN_TOP_RIGHT, -76, 0);
  lv_obj_add_event_cb(qb, qredit_save_cb, LV_EVENT_CLICKED, NULL);
  lbl = lv_label_create(qb); lv_label_set_text(lbl, LV_SYMBOL_OK); lv_obj_center(lbl);

  lv_obj_t* qh = lv_label_create(qredit_scr);
  lv_label_set_text_fmt(qh, "One reply per line, up to %d", QR_MAX);
  lv_obj_set_style_text_font(qh, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(qh, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(qh, LV_ALIGN_TOP_LEFT, 0, 32);

  qredit_ta = lv_textarea_create(qredit_scr);
  lv_textarea_set_max_length(qredit_ta, QR_MAX * QR_TEXT_LEN - 1);
  lv_obj_set_size(qredit_ta, 304, 172);
  lv_obj_align(qredit_ta, LV_ALIGN_TOP_MID, 0, 52);
  lv_obj_set_style_opa(qredit_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_obj_set_style_opa(qredit_ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_event_cb(qredit_ta, qredit_ta_cb, LV_EVENT_ALL, NULL);

  qredit_kb = lv_keyboard_create(qredit_scr);
  lv_keyboard_set_textarea(qredit_kb, qredit_ta);
  kbAttachShiftBehavior(qredit_kb);
  lv_obj_set_size(qredit_kb, 320, KB_HEIGHT);
  lv_obj_align(qredit_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(qredit_kb, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// refreshers
// ---------------------------------------------------------------------------
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

void UITask::refreshContactsTab() {
  lv_obj_clean(contacts_list);

  // sort by most recently heard (or by name), honoring the type filter
  struct Entry { int idx; uint32_t ts; char name[32]; };
  static Entry order[64];
  int n = 0;
  for (int idx = MAX_ANON_CONTACTS; idx < the_mesh.getTotalContactSlots() && n < 64; idx++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(idx, c)) break;
    if (c.name[0] == 0) continue;
    if (contact_filter == 1 && c.type != ADV_TYPE_CHAT) continue;
    if (contact_filter == 2 && c.type != ADV_TYPE_REPEATER) continue;
    if (contact_filter == 3 && c.type != ADV_TYPE_ROOM) continue;
    order[n].idx = idx;
    order[n].ts = c.last_advert_timestamp;
    StrHelper::strncpy(order[n].name, c.name, sizeof(order[n].name));
    n++;
  }
  for (int i = 1; i < n; i++) {   // insertion sort
    Entry key = order[i];
    int j = i - 1;
    while (j >= 0 && (contacts_by_name ? strcasecmp(order[j].name, key.name) > 0 : order[j].ts < key.ts)) {
      order[j + 1] = order[j]; j--;
    }
    order[j + 1] = key;
  }

  int shown = 0;
  for (int oi = 0; oi < n; oi++) {
    int idx = order[oi].idx;
    ContactInfo c;
    if (!the_mesh.getContactByIdx(idx, c)) continue;
    const char* icon = LV_SYMBOL_ENVELOPE;
    if (c.type == ADV_TYPE_REPEATER) icon = SYMBOL_TOWER;
    else if (c.type == ADV_TYPE_ROOM) icon = LV_SYMBOL_HOME;
    else if (c.type == ADV_TYPE_SENSOR) icon = LV_SYMBOL_TINT;
    lv_obj_t* btn = lv_list_add_button(contacts_list, icon, c.name);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
    lv_obj_add_event_cb(btn, contact_row_cb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)idx);
    lv_obj_add_event_cb(btn, contact_row_long_cb, LV_EVENT_LONG_PRESSED, (void*)(intptr_t)idx);
    shown++;
  }
  if (shown == 0) {
    lv_list_add_text(contacts_list, contact_filter == 0 ? "No contacts yet - waiting for adverts" : "No contacts of this type");
  }
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

// ---------------------------------------------------------------------------
// thread view
// ---------------------------------------------------------------------------
void UITask::openRouteForThread() {
  if (_thread_is_channel) {
    showToast("Channels always flood");
    return;
  }
  for (int idx = MAX_ANON_CONTACTS; idx < the_mesh.getTotalContactSlots(); idx++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(idx, c)) break;
    if (c.name[0] != 0 && memcmp(c.id.pub_key, _thread_key, 6) == 0) {
      detail_idx = idx;
      path_open_cb(NULL);
      return;
    }
  }
  showToast("Contact not found");
}

void UITask::clearCurrentThread() {
  if (!_thread_clear_armed) {
    _thread_clear_armed = true;
    showToast("Tap trash again to clear thread");
    return;
  }
  _thread_clear_armed = false;
  chatStoreClearThread(_thread_key);
  refreshThread();
  refreshChatsTab();
  showToast("Thread cleared");
}

void UITask::openThread(const uint8_t key[6], const char* name) {
  memcpy(_thread_key, key, 6);
  _thread_clear_armed = false;
  chatStoreUnreadClear(key);
  refreshStatusBar();
  _thread_is_channel = isChannelKey(key, &_thread_channel_idx);
  _thread_is_console = false;
  StrHelper::strncpy(_thread_name, name, sizeof(_thread_name));
  lv_label_set_text(thread_title, name);
  lv_textarea_set_placeholder_text(thread_ta, "Message...");
  lv_obj_remove_flag(thread_qr_btn, LV_OBJ_FLAG_HIDDEN);
  refreshThread();
  lv_obj_remove_flag(thread_scr, LV_OBJ_FLAG_HIDDEN);
  updateThreadSubtitle();
}

void UITask::openContactThread(const ContactInfo& contact) {
  openThread(contact.id.pub_key, contact.name);
}

void UITask::closeThread() {
  qrPanelClose();
  lv_obj_add_flag(thread_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(thread_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(thread_input_row, LV_ALIGN_BOTTOM_MID, 0, 0);
  if (_thread_is_console) {
    _thread_is_console = false;
    lv_obj_remove_flag(rep_scr, LV_OBJ_FLAG_HIDDEN);   // back to the manager
  } else {
    refreshChatsTab();
  }
}

static lv_obj_t* echo_meta_lbl = NULL;
static char echo_meta_base[16];

void UITask::refreshThread() {
  lv_obj_clean(thread_msgs);
  echo_meta_lbl = NULL;

  int count = 0;
  while (chatStoreGet(_thread_key, count) != NULL && count < CHAT_STORE_SIZE) count++;
  if (count > 50) count = 50;   // render the newest 50; full history stays stored

  for (int k = count - 1; k >= 0; k--) {   // oldest first, newest at bottom
    ChatMsg* m = chatStoreGet(_thread_key, k);
    if (m == NULL) continue;

    lv_obj_t* row = lv_obj_create(thread_msgs);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, m->outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    char age[16];
    fmtAge(age, sizeof(age), m->timestamp);
    if (m->outgoing) {   // time sits left of an outgoing (right-aligned) bubble
      lv_obj_t* tl = lv_label_create(row);
      lv_label_set_text(tl, age);
      lv_obj_set_style_text_font(tl, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(tl, lv_color_hex(COL_MUTED), 0);
      if (m->timestamp == _echo_msg_ts && millis() <= _echo_window_end) {
        echo_meta_lbl = tl;   // repeat-echo counter rides on this label
        StrHelper::strncpy(echo_meta_base, age, sizeof(echo_meta_base));
        if (_echo_count > 0) lv_label_set_text_fmt(tl, "%s " LV_SYMBOL_LOOP "%d", age, _echo_count);
      }
    }

    lv_obj_t* bubble = lv_label_create(row);
    if (m->outgoing && m->status != MSG_STATUS_NONE) {
      if (m->status == MSG_STATUS_PENDING && millis() > m->timeout_at) m->status = MSG_STATUS_NO_ACK;
      const char* mark = m->status == MSG_STATUS_DELIVERED ? LV_SYMBOL_OK
                       : m->status == MSG_STATUS_PENDING ? LV_SYMBOL_REFRESH : LV_SYMBOL_WARNING;
      lv_label_set_text_fmt(bubble, "%s %s", m->text, mark);
      if (m->status == MSG_STATUS_NO_ACK) {   // tap to resend
        lv_obj_add_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(bubble, bubble_retry_cb, LV_EVENT_CLICKED, (void*)(intptr_t)k);
      }
    } else {
      lv_label_set_text(bubble, m->text);
    }
    lv_label_set_long_mode(bubble, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(bubble, 240, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(m->outgoing ? COL_ACCENT_D : COL_CARD), 0);
    lv_obj_set_style_radius(bubble, 8, 0);
    lv_obj_set_style_pad_all(bubble, 6, 0);

    if (!m->outgoing) {   // time sits right of an incoming bubble
      lv_obj_t* tl = lv_label_create(row);
      lv_label_set_text(tl, age);
      lv_obj_set_style_text_font(tl, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(tl, lv_color_hex(COL_MUTED), 0);
    }
  }
  lv_obj_scroll_to_y(thread_msgs, LV_COORD_MAX, LV_ANIM_OFF);
}

void UITask::sendFromThread(const char* text) {
  uint32_t timestamp = rtc_clock.getCurrentTime();

  _echo_count = 0;
  _echo_window_end = millis() + 60000;
  _echo_msg_ts = timestamp;

  if (_thread_is_console) {   // repeater terminal: everything is a CLI command
    ContactInfo* c = the_mesh.lookupContactByPubKey(_thread_key, 6);
    if (c == NULL) { showToast("Contact gone"); return; }
    uint32_t est_timeout;
    timestamp = rtc_clock.getCurrentTimeUnique();   // a repeat within the same second would look like a replay
    _echo_msg_ts = timestamp;
    if (the_mesh.sendCommandData(*c, timestamp, 0, TXT_TYPE_CLI_DATA, text, est_timeout) == MSG_SEND_FAILED) {
      showToast("Send failed");
    } else {
      chatStorePush(_thread_key, true, timestamp, text);
    }
    refreshThread();
    return;
  }

  if (_thread_is_channel) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(_thread_channel_idx, ch) || ch.name[0] == 0) return;
    if (the_mesh.sendGroupMessage(timestamp, ch.channel, _node_prefs->node_name, text, strlen(text))) {
      chatStorePush(_thread_key, true, timestamp, text);
    } else {
      showToast("Send failed");
    }
  } else {
    ContactInfo* c = the_mesh.lookupContactByPubKey(_thread_key, 6);
    if (c == NULL) { showToast("Contact gone"); return; }
    uint32_t expected_ack, est_timeout;
    int result = the_mesh.sendMessage(*c, timestamp, 0, text, expected_ack, est_timeout);
    if (result == MSG_SEND_FAILED) {
      showToast("Send failed");
    } else {
      ChatMsg* m = chatStorePush(_thread_key, true, timestamp, text);
      if (m != NULL && expected_ack != 0) {
        m->status = MSG_STATUS_PENDING;
        m->expected_ack = expected_ack;
        // flood acks route back through repeaters and can take a long while;
        // est_timeout models the direct case only - be patient like the app is
        uint32_t wait = est_timeout * 4;
        if (wait < 60000) wait = 60000;
        m->timeout_at = millis() + wait;
      }
    }
  }
  refreshThread();
}

// ---------------------------------------------------------------------------
// toast
// ---------------------------------------------------------------------------
static void toast_del_cb(lv_timer_t* t) {
  lv_obj_t* toast = (lv_obj_t*) lv_timer_get_user_data(t);
  lv_obj_delete(toast);
  lv_timer_delete(t);
}

void UITask::openContactDetail(int contact_idx) {
  ContactInfo c;
  if (!the_mesh.getContactByIdx(contact_idx, c) || c.name[0] == 0) return;
  detail_idx = contact_idx;
  detail_remove_armed = false;
  lv_label_set_text(detail_remove_lbl, LV_SYMBOL_TRASH " Remove");

  lv_label_set_text(detail_title, c.name);

  const char* type_s = c.type == ADV_TYPE_CHAT ? "chat node"
                     : c.type == ADV_TYPE_REPEATER ? "repeater"
                     : c.type == ADV_TYPE_ROOM ? "room server" : "sensor";

  char heard[32] = "never";
  if (c.last_advert_timestamp > 0) {
    long secs = (long)rtc_clock.getCurrentTime() - (long)c.last_advert_timestamp;
    if (secs < 0) secs = 0;
    if (secs < 3600) snprintf(heard, sizeof(heard), "%ldm ago", secs / 60);
    else if (secs < 86400) snprintf(heard, sizeof(heard), "%ldh ago", secs / 3600);
    else snprintf(heard, sizeof(heard), "%ldd ago", secs / 86400);
  }

  char path[32];
  if (c.out_path_len == OUT_PATH_UNKNOWN) strcpy(path, "flood (no path learned yet)");
  else snprintf(path, sizeof(path), "direct, %d hop%s", c.out_path_len, c.out_path_len == 1 ? "" : "s");

  char pos[80] = "position: unknown";
  if (c.gps_lat != 0 || c.gps_lon != 0) {
    double clat = c.gps_lat / 1000000.0, clon = c.gps_lon / 1000000.0;
    LocationProvider* nmea = _sensors != NULL ? _sensors->getLocationProvider() : NULL;
    if (nmea != NULL && nmea->isValid()) {
      double mlat = nmea->getLatitude() / 1000000.0, mlon = nmea->getLongitude() / 1000000.0;
      // haversine distance + initial bearing
      double dlat = (clat - mlat) * M_PI / 180.0, dlon = (clon - mlon) * M_PI / 180.0;
      double a = sin(dlat / 2) * sin(dlat / 2) +
                 cos(mlat * M_PI / 180.0) * cos(clat * M_PI / 180.0) * sin(dlon / 2) * sin(dlon / 2);
      double dist_km = 6371.0 * 2.0 * atan2(sqrt(a), sqrt(1 - a));
      double y = sin(dlon) * cos(clat * M_PI / 180.0);
      double x = cos(mlat * M_PI / 180.0) * sin(clat * M_PI / 180.0) -
                 sin(mlat * M_PI / 180.0) * cos(clat * M_PI / 180.0) * cos(dlon);
      int brg = ((int)(atan2(y, x) * 180.0 / M_PI) + 360) % 360;
      static const char* dirs[] = {"N","NE","E","SE","S","SW","W","NW"};
      double dist = units_miles ? dist_km * 0.621371 : dist_km;
      snprintf(pos, sizeof(pos), "%.2f %s %s (%d deg)\n%.5f  %.5f",
               dist, units_miles ? "mi" : "km", dirs[((brg + 22) / 45) % 8], brg, clat, clon);
    } else {
      snprintf(pos, sizeof(pos), "%.5f  %.5f", clat, clon);
    }
  }

  char info[220];
  snprintf(info, sizeof(info), "%s\nlast heard: %s\npath: %s\n%s", type_s, heard, path, pos);
  lv_label_set_text(detail_info, info);
  // trace only applies to nodes that relay it
  if (c.type == ADV_TYPE_REPEATER || c.type == ADV_TYPE_ROOM) {
    lv_obj_remove_flag(detail_trace_btn, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(detail_trace_btn, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_remove_flag(detail_scr, LV_OBJ_FLAG_HIDDEN);
}

void UITask::openTraceForContact(int contact_idx) {
  ContactInfo c;
  if (!the_mesh.getContactByIdx(contact_idx, c)) return;
  detail_idx = contact_idx;
  // only repeaters and room servers relay a trace; clients do not
  if (c.type != ADV_TYPE_REPEATER && c.type != ADV_TYPE_ROOM) {
    showToast("Trace measures repeaters only");
    return;
  }
  lv_label_set_text_fmt(trace_title, LV_SYMBOL_GPS " Trace path: %s", c.name);
  if (c.out_path_len == OUT_PATH_UNKNOWN) {
    lv_label_set_text(trace_lbl, "No path to this repeater yet.\n\nUse Set path to choose which\nrepeaters to trace through.");
    lv_obj_remove_flag(trace_scr, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  // out through the known hops, the target repeater, then back the same way
  uint8_t path[MAX_PATH_SIZE * 2 + 1];
  uint8_t n = 0;
  for (int i = 0; i < c.out_path_len; i++) path[n++] = c.out_path[i];
  n += c.id.copyHashTo(&path[n]);
  for (int i = c.out_path_len - 1; i >= 0; i--) path[n++] = c.out_path[i];

  uint32_t tag = esp_random();
  if (tag == 0) tag = 1;
  int est_timeout = the_mesh.uiTracePath(path, n, tag);
  if (est_timeout <= 0) {
    showToast("Trace send failed");
    return;
  }

  _trace_tag = tag;
  _trace_started = millis();
  _trace_deadline = millis() + est_timeout + 3000;

  lv_label_set_text(trace_lbl, "Tracing out and back...\n\nEach repeater on the path reports the\nlevel it heard. The reply lands here\nonly if this node can hear the first\nhop directly.");
  lv_obj_remove_flag(trace_scr, LV_OBJ_FLAG_HIDDEN);
}

void UITask::traceResponse(uint32_t tag, const uint8_t* path_hashes, const uint8_t* path_snrs,
                           uint8_t hop_count, int8_t final_snr) {
  if (_trace_tag == 0 || tag != _trace_tag) return;
  _trace_tag = 0;

  unsigned long rtt = millis() - _trace_started;
  int out_hops = (hop_count + 1) / 2;   // round trip turns around at the far end

  char buf[768];   // up to eight hops out and back, plus the round-trip line
  int off = snprintf(buf, sizeof(buf), "Round trip %lu.%lus\n\n", rtt / 1000, (rtt % 1000) / 100);
  for (int i = 0; i < hop_count && off < (int) sizeof(buf) - 48; i++) {
    char nm[20];
    traceHashName(path_hashes[i], nm, sizeof(nm));
    off += snprintf(&buf[off], sizeof(buf) - off, "%s %.14s   %+.1f dB\n",
                    i < out_hops ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT, nm, (int8_t) path_snrs[i] / 4.0);
  }
  snprintf(&buf[off], sizeof(buf) - off, "%s You   %+.1f dB", LV_SYMBOL_LEFT, final_snr / 4.0);
  lv_label_set_text(trace_lbl, buf);
}

void UITask::nodeNameChanged() {
  if (_node_prefs != NULL) lv_label_set_text(lbl_node_name, _node_prefs->node_name);
}

void UITask::updateThreadSubtitle() {
  if (lv_obj_has_flag(thread_scr, LV_OBJ_FLAG_HIDDEN)) return;
  char buf[32] = "";
  if (!_thread_is_channel && !_thread_is_console) {
    ContactInfo* c = the_mesh.lookupContactByPubKey(_thread_key, 6);
    if (c != NULL) {
      if (c->out_path_len == OUT_PATH_UNKNOWN) strcpy(buf, "flood");
      else if (c->out_path_len == 0) strcpy(buf, "direct");
      else snprintf(buf, sizeof(buf), "%d hop%s", c->out_path_len, c->out_path_len == 1 ? "" : "s");
    }
  }
  lv_label_set_text(thread_sub_lbl, buf);
}

void UITask::resendMessage(int k) {
  ChatMsg* m = chatStoreGet(_thread_key, k);
  if (m == NULL || !m->outgoing || m->status != MSG_STATUS_NO_ACK) return;
  char txt[200];
  StrHelper::strncpy(txt, m->text, sizeof(txt));
  sendFromThread(txt);
  showToast("Resent");
}

void UITask::openChannelOptions(const uint8_t key[6]) {
  int idx;
  ChannelDetails ch;
  if (!isChannelKey(key, &idx) || !the_mesh.getChannel(idx, ch) || ch.name[0] == 0) return;
  chopt_idx = idx;
  memcpy(chopt_key, key, 6);
  chopt_remove_armed = false;
  lv_label_set_text(chopt_remove_lbl, LV_SYMBOL_TRASH " Remove channel");
  lv_label_set_text_fmt(chopt_title, "#%s", ch.name);
  lv_textarea_set_text(chopt_ta, ch.name);
  lv_obj_remove_flag(chopt_scr, LV_OBJ_FLAG_HIDDEN);
}

void UITask::finishSetupWizard(int preset_idx, const char* name, int tz_hours, bool apply) {
  if (apply) {
    settingsApplyPreset(preset_idx);
    if (name != NULL && name[0] != 0 && _node_prefs != NULL) {
      StrHelper::strncpy(_node_prefs->node_name, name, sizeof(_node_prefs->node_name));
      the_mesh.savePrefs();
      nodeNameChanged();
    }
    settingsSetTzOffset(tz_hours);
    refreshStatusBar();
    refreshNodeTab();
    showToast("Setup complete");
  }
  File f = SPIFFS.open("/setup_done", "w");
  if (f) { f.print(1); f.close(); }
  wizard_pending = false;
  lv_obj_add_flag(wiz_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wiz_kb, LV_OBJ_FLAG_HIDDEN);
}

void UITask::msgEchoHeard() {
  if (millis() > _echo_window_end) return;   // stale echo, window closed
  _echo_count++;
  if (echo_meta_lbl != NULL) {
    lv_label_set_text_fmt(echo_meta_lbl, "%s " LV_SYMBOL_LOOP "%d", echo_meta_base, _echo_count);
  }
}

void UITask::showToast(const char* text) {
  lv_obj_t* toast = lv_obj_create(lv_layer_top());
  lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(toast, lv_color_hex(COL_ACCENT_D), 0);
  lv_obj_set_style_radius(toast, 8, 0);
  lv_obj_set_style_pad_all(toast, 8, 0);
  lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 30);
  lv_obj_t* lbl = lv_label_create(toast);
  lv_label_set_text(lbl, text);
  lv_timer_create(toast_del_cb, 2000, toast);
}

// ---------------------------------------------------------------------------
// mesh events
// ---------------------------------------------------------------------------
void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  refreshStatusBar();
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;

  bool in_open_thread = false;
  uint8_t msg_key[6] = {0};
  bool have_key = false;
  ContactInfo* from = the_mesh.searchContactsByPrefix(from_name);
  if (from != NULL) {
    memcpy(msg_key, from->id.pub_key, 6);
    have_key = true;
  } else {
    for (int idx = 0; idx < MAX_GROUP_CHANNELS; idx++) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(idx, ch)) break;
      if (ch.name[0] == 0 || strcmp(ch.name, from_name) != 0) continue;
      makeChannelKey(idx, msg_key);
      have_key = true;
      break;
    }
  }
  if (have_key) {
    chatStorePush(msg_key, false, rtc_clock.getCurrentTime(), text);
    in_open_thread = !lv_obj_has_flag(thread_scr, LV_OBJ_FLAG_HIDDEN)
                     && memcmp(_thread_key, msg_key, 6) == 0;
    if (!in_open_thread) chatStoreUnreadBump(msg_key);
  }

  if (in_open_thread) {
    refreshThread();
  } else {
    if (have_key) showNotifBanner(msg_key, from_name, text);
    else {
      char buf[48];
      snprintf(buf, sizeof(buf), LV_SYMBOL_ENVELOPE " %s", from_name);
      showToast(buf);
    }
    refreshChatsTab();
  }
  refreshStatusBar();
}

void UITask::msgAck(uint32_t ack_crc) {
  if (chatStoreAck(ack_crc)) {
    if (!lv_obj_has_flag(thread_scr, LV_OBJ_FLAG_HIDDEN)) refreshThread();
  }
}

void UITask::loginResult(const uint8_t* pub_key, bool success) {
  // only react to the login we're actually waiting on (phone app logins and
  // stale responses for other repeaters shouldn't clear state or toast)
  if (_pending_login_deadline != 0 && memcmp(_pending_login_key, pub_key, 6) == 0) {
    _pending_login_deadline = 0;
    showToast(success ? "Login OK" : "Login failed");
  }
  if (memcmp(_rep_key, pub_key, 6) == 0) {
    _rep_logging_in = false;
    _rep_logged_in = success;
    if (success && rep_pending_pw[0] != 0 && rep_pending_remember) {
      savedPwSet(_rep_key, rep_pending_pw);
    } else if (!success && rep_pending_pw[0] != 0) {
      savedPwForget(_rep_key);   // stale saved password: drop it
    }
    rep_pending_pw[0] = 0;
    if (success) {
      ContactInfo* c = the_mesh.lookupContactByPubKey(_rep_key, 6);
      if (c != NULL && c->out_path_len != OUT_PATH_UNKNOWN) rep_flood_on = false;
    }
    if (!lv_obj_has_flag(rep_scr, LV_OBJ_FLAG_HIDDEN)) refreshRepeaterScr();
    if (success && rep_auto_refresh) requestRepeaterStatus();   // opt-in
  }
}

const char* UITask::savedRepeaterPw() {
  return savedPwFor(_rep_key);
}

void UITask::requestRepeaterStatus() {
  ContactInfo* c = the_mesh.lookupContactByPubKey(_rep_key, 6);
  if (c == NULL) return;
  if (the_mesh.uiRequestStatus(*c) == MSG_SEND_FAILED) {
    showToast("Status request failed");
  } else {
    rep_stats_waiting = true;
    rep_stats_valid = false;
    if (!lv_obj_has_flag(rep_scr, LV_OBJ_FLAG_HIDDEN)) refreshRepeaterScr();
  }
}

void UITask::cliResponse(const char* from_name, const char* text) {
  ContactInfo* from = the_mesh.searchContactsByPrefix(from_name);
  if (from == NULL) return;
  chatStorePush(from->id.pub_key, false, rtc_clock.getCurrentTime(), text);

  if (memcmp(_rep_key, from->id.pub_key, 6) == 0
      && !lv_obj_has_flag(rep_scr, LV_OBJ_FLAG_HIDDEN)) {
    StrHelper::strncpy(rep_last_reply, text, sizeof(rep_last_reply));
    refreshRepeaterScr();
    return;
  }
  if (!lv_obj_has_flag(thread_scr, LV_OBJ_FLAG_HIDDEN)
      && memcmp(_thread_key, from->id.pub_key, 6) == 0) {
    refreshThread();   // terminal is open: reply appears in place
  } else {
    char buf[48];
    snprintf(buf, sizeof(buf), LV_SYMBOL_KEYBOARD " %s replied", from_name);
    showToast(buf);
  }
}

void UITask::statusResponse(const uint8_t* pub_key, const uint8_t* data, int len) {
  if (memcmp(_rep_key, pub_key, 6) != 0 || len <= 0) return;
  memset(&rep_stats, 0, sizeof(rep_stats));
  memcpy(&rep_stats, data, len < (int)sizeof(rep_stats) ? len : (int)sizeof(rep_stats));
  rep_stats_valid = true;
  rep_stats_waiting = false;
  if (!lv_obj_has_flag(rep_scr, LV_OBJ_FLAG_HIDDEN)) refreshRepeaterScr();
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
    if (!lv_obj_has_flag(thread_scr, LV_OBJ_FLAG_HIDDEN)) {
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
    if (rep_auto_refresh && !lv_obj_has_flag(rep_scr, LV_OBJ_FLAG_HIDDEN) && _rep_logged_in) {
      static unsigned long next_rep_auto = 0;
      if (millis() > next_rep_auto) {
        next_rep_auto = millis() + 30000;
        requestRepeaterStatus();   // keep the admin dashboard live
      }
    }
    if (_trace_tag != 0 && millis() > _trace_deadline) {
      _trace_tag = 0;
      if (!lv_obj_has_flag(trace_scr, LV_OBJ_FLAG_HIDDEN)) {
        lv_label_set_text(trace_lbl,
            "No reply - trace timed out.\n\nA trace returns through the same\nhops, so hop 1 must be a repeater\nthis node can hear directly. Check\nthe path with Set path, or a hop is\noffline.");
      }
    }
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

  if (_pending_login_deadline != 0 && millis() > _pending_login_deadline) {
    _pending_login_deadline = 0;
    _rep_logging_in = false;
    if (!lv_obj_has_flag(rep_scr, LV_OBJ_FLAG_HIDDEN)) refreshRepeaterScr();
    showToast("Login: no response - check password/range");
  }
}
