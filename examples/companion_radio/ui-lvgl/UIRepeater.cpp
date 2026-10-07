// Repeater manager: login, status card, admin actions, saved passwords.

#include "UICommon.h"

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

void repeaterStateLoad() {
  rep_auto_refresh = prefReadInt("/rep_auto", 0) != 0;
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
  uint32_t est_timeout;
  if (the_mesh.sendLogin(*c, password, est_timeout) == MSG_SEND_FAILED) {
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
  uint32_t tag, est_timeout;
  if (the_mesh.sendRequest(*c, REQ_TYPE_GET_STATUS, tag, est_timeout) == MSG_SEND_FAILED) {
    showToast("Status request failed");
  } else {
    _stats_tag = tag;
    rep_stats_waiting = true;
    rep_stats_valid = false;
    if (!lv_obj_has_flag(rep_scr, LV_OBJ_FLAG_HIDDEN)) refreshRepeaterScr();
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

// login and status replies are matched by sender, the same way the phone
// command path does it; the request tag is not echoed by older repeaters
bool UITask::onUnhandledResponse(const ContactInfo& from, uint32_t tag, const uint8_t* data, uint8_t len) {
  if (len == 0) return false;
  if (_pending_login_deadline != 0 && memcmp(_pending_login_key, from.id.pub_key, 6) == 0) {
    // a refused login gets no reply at all, so only a well-formed OK counts
    bool ok = (len >= 2 && memcmp(data, "OK", 2) == 0) || data[0] == RESP_SERVER_LOGIN_OK;
    if (ok) {
      loginResult(from.id.pub_key, true);
      return true;
    }
  }
  if (rep_stats_waiting && tag == _stats_tag && memcmp(_rep_key, from.id.pub_key, 6) == 0) {
    _stats_tag = 0;
    statusResponse(from.id.pub_key, data, len);
    return true;
  }
  return false;
}

void repeaterScrShow() { lv_obj_remove_flag(rep_scr, LV_OBJ_FLAG_HIDDEN); }

void repeaterScrHide() {
  lv_obj_add_flag(rep_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(rep_kb, LV_OBJ_FLAG_HIDDEN);
}

void UITask::cliReply(const ContactInfo& from, const char* text) {
  chatStorePush(from.id.pub_key, false, rtc_clock.getCurrentTime(), text);
  if (memcmp(_rep_key, from.id.pub_key, 6) == 0 && !lv_obj_has_flag(rep_scr, LV_OBJ_FLAG_HIDDEN)) {
    StrHelper::strncpy(rep_last_reply, text, sizeof(rep_last_reply));
    refreshRepeaterScr();
  } else if (threadOpen() && memcmp(_thread_key, from.id.pub_key, 6) == 0) {
    refreshThread();   // terminal is open: reply appears in place
  } else {
    char buf[48];
    snprintf(buf, sizeof(buf), LV_SYMBOL_KEYBOARD " %s replied", from.name);
    showToast(buf);
  }
}

// from loop(): status auto-refresh while the manager is open, and the login timeout
void UITask::repeaterTick() {
  if (rep_auto_refresh && !lv_obj_has_flag(rep_scr, LV_OBJ_FLAG_HIDDEN) && _rep_logged_in) {
    static unsigned long next_rep_auto = 0;
    if (millis() > next_rep_auto) {
      next_rep_auto = millis() + 30000;
      requestRepeaterStatus();   // keep the admin dashboard live
    }
  }
  if (_pending_login_deadline != 0 && millis() > _pending_login_deadline) {
    _pending_login_deadline = 0;
    _rep_logging_in = false;
    if (!lv_obj_has_flag(rep_scr, LV_OBJ_FLAG_HIDDEN)) refreshRepeaterScr();
    showToast("Login: no response - check password/range");
  }
}
