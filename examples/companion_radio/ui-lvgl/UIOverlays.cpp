// Top-layer overlays: splash, notification banner, toast, channel options,
// about and the first-boot wizard.

#include "UICommon.h"

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

void buildSplash() {
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
void showNotifBanner(const uint8_t key[6], const char* from_name, const char* text) {
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
  ui->showToast(the_mesh.advert(true) ? "Flood advert sent" : "Advert failed");
}
static void about_open_cb(lv_event_t* e) {
  char hex[65];
  const uint8_t* pk = the_mesh.self_id.pub_key;
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
    "flood = no path yet, sent everywhere\n"
    "direct = 0 hops, N hops = via repeaters\n"
    "#name = channel (group chat)\n"
    "Long-press a contact or channel for options.\n"
    "Advert = announce this node to the mesh.",
    FIRMWARE_VERSION, FIRMWARE_BUILD_DATE, the_mesh.getNodeName(), hex, hex + 32);
  lv_label_set_text(about_lbl, buf);
  lv_obj_remove_flag(about_scr, LV_OBJ_FLAG_HIDDEN);
}
void aboutOpen() { about_open_cb(NULL); }

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

void buildPolishOverlays() {
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
  wizard_pending = !SPIFFS.exists("/setup_done");
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
void wizardOpen() {
  if (ui->nodePrefs() != NULL) lv_textarea_set_text(wiz_name_ta, ui->nodePrefs()->node_name);
  wiz_tz = settingsTzOffset();
  lv_label_set_text_fmt(wiz_tz_lbl, "UTC%+d", wiz_tz);
  wizSuggestTz();
  wizShowStep(0);
  lv_obj_remove_flag(wiz_scr, LV_OBJ_FLAG_HIDDEN);
}

static void toast_del_cb(lv_timer_t* t) {
  lv_obj_t* toast = (lv_obj_t*) lv_timer_get_user_data(t);
  lv_obj_delete(toast);
  lv_timer_delete(t);
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
