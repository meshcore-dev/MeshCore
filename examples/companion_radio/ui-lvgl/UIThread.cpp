// Message thread view: bubbles, keyboard, quick replies, sending.

#include "UICommon.h"

// thread overlay
static lv_obj_t* thread_scr;
static lv_obj_t* thread_title;
static lv_obj_t* thread_sub_lbl;   // route (flood/hops)
static lv_obj_t* thread_msgs;
static lv_obj_t* thread_input_row;
static lv_obj_t* thread_ta;
static lv_obj_t* thread_kb;

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

void qrLoad() {
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

void UITask::openConsoleThread() {
  repeaterScrHide();   // manager under terminal, not over it
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

void UITask::buildThreadScr() {
  lv_obj_t* scr = lv_screen_active();
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

// ---- quick replies editor (one reply per line) ----
void qrEditorOpen() {
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

void qrEditorBuild() {
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
  lv_obj_t* lbl = lv_label_create(qb); lv_label_set_text(lbl, LV_SYMBOL_CLOSE); lv_obj_center(lbl);

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

void UITask::openRouteForThread() {
  if (_thread_is_channel) {
    showToast("Channels always flood");
    return;
  }
  for (int idx = MAX_ANON_CONTACTS; idx < the_mesh.getTotalContactSlots(); idx++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(idx, c)) break;
    if (c.name[0] != 0 && memcmp(c.id.pub_key, _thread_key, 6) == 0) {
      contactPathOpen(idx);
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
    repeaterScrShow();   // back to the manager
  } else {
    refreshChatsTab();
  }
}

void UITask::refreshThread() {
  lv_obj_clean(thread_msgs);

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

  if (_thread_is_console) {   // repeater terminal: everything is a CLI command
    ContactInfo* c = the_mesh.lookupContactByPubKey(_thread_key, 6);
    if (c == NULL) { showToast("Contact gone"); return; }
    uint32_t est_timeout;
    timestamp = rtc_clock.getCurrentTimeUnique();   // a repeat within the same second would look like a replay
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

void UITask::updateThreadSubtitle() {
  if (lv_obj_has_flag(thread_scr, LV_OBJ_FLAG_HIDDEN)) return;
  char buf[32] = "";
  if (!_thread_is_channel && !_thread_is_console) {
    ContactInfo* c = the_mesh.lookupContactByPubKey(_thread_key, 6);
    if (c != NULL) {
      if (c->out_path_len == OUT_PATH_UNKNOWN) strcpy(buf, "flood");
      else if ((c->out_path_len & 63) == 0) strcpy(buf, "direct");
      else snprintf(buf, sizeof(buf), "%d hop%s", c->out_path_len & 63, (c->out_path_len & 63) == 1 ? "" : "s");
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

void UITask::showIncoming(const uint8_t* key, const char* from_name, const char* text) {
  bool in_open_thread = false;
  if (key != NULL) {
    chatStorePush(key, false, rtc_clock.getCurrentTime(), text);
    in_open_thread = !lv_obj_has_flag(thread_scr, LV_OBJ_FLAG_HIDDEN)
                     && memcmp(_thread_key, key, 6) == 0;
    if (!in_open_thread) chatStoreUnreadBump(key);
  }

  if (in_open_thread) {
    refreshThread();
  } else {
    if (key != NULL) showNotifBanner(key, from_name, text);
    else {
      char buf[48];
      snprintf(buf, sizeof(buf), LV_SYMBOL_ENVELOPE " %s", from_name);
      showToast(buf);
    }
    refreshChatsTab();
  }
  refreshStatusBar();
}

bool threadOpen() {
  return thread_scr != NULL && !lv_obj_has_flag(thread_scr, LV_OBJ_FLAG_HIDDEN);
}
