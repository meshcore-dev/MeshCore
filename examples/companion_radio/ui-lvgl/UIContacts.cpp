// Contacts tab, contact detail, manual path picker and trace route.

#include "UICommon.h"
#include <esp_random.h>
#include <math.h>

static lv_obj_t* contacts_list;

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

static void traceHashName(const uint8_t* hash, uint8_t hash_len, char* out, size_t sz) {
  for (int idx = MAX_ANON_CONTACTS; idx < the_mesh.getTotalContactSlots(); idx++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(idx, c)) break;
    if (c.name[0] != 0 && memcmp(c.id.pub_key, hash, hash_len) == 0) {
      StrHelper::strncpy(out, c.name, sz);
      return;
    }
  }
  snprintf(out, sz, "hop %02X", hash[0]);
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
    for (int h = 0; h < (cur.out_path_len & 63) && h < PATH_MAX_HOPS; h++) {
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

// contact detail, manual path picker and trace overlays
void UITask::buildContactOverlays() {
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
  else snprintf(path, sizeof(path), "direct, %d hop%s", c.out_path_len & 63, (c.out_path_len & 63) == 1 ? "" : "s");

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

  // the stored path carries its hop hash width (1..3 bytes) in the top two
  // bits; a trace declares the width as a shift (1, 2 or 4 bytes)
  uint8_t hash_sz = (c.out_path_len >> 6) + 1;
  int hops = c.out_path_len & 63;
  uint8_t flags = hash_sz == 1 ? 0 : hash_sz == 2 ? 1 : 0xFF;
  int n = (2 * hops + 1) * hash_sz;
  if (flags == 0xFF || n > 160) {
    showToast("Path cannot be traced");
    return;
  }

  // out through the known hops, the target repeater, then back the same way
  uint8_t path[168];
  int p = 0;
  memcpy(&path[p], c.out_path, hops * hash_sz);
  p += hops * hash_sz;
  p += c.id.copyHashTo(&path[p], hash_sz);
  for (int i = hops - 1; i >= 0; i--) {
    memcpy(&path[p], &c.out_path[i * hash_sz], hash_sz);
    p += hash_sz;
  }

  uint32_t tag = esp_random();
  if (tag == 0) tag = 1;
  mesh::Packet* pkt = the_mesh.createTrace(tag, 0, flags);
  if (pkt == NULL) {
    showToast("Trace send failed");
    return;
  }
  the_mesh.sendDirect(pkt, path, (uint8_t) n);

  _trace_tag = tag;
  _trace_started = millis();
  _trace_deadline = millis() + 4000 + 2500UL * (n + 1);   // out and back through every hop

  lv_label_set_text(trace_lbl, "Tracing out and back...\n\nEach repeater on the path reports the\nlevel it heard. The reply lands here\nonly if this node can hear the first\nhop directly.");
  lv_obj_remove_flag(trace_scr, LV_OBJ_FLAG_HIDDEN);
}

void UITask::onTraceRecv(mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) {
  if (_trace_tag == 0 || tag != _trace_tag) return;
  _trace_tag = 0;

  uint8_t hash_sz = 1 << (flags & 0x03);   // one, two or four bytes per hop
  uint8_t hop_count = path_len / hash_sz;
  int8_t final_snr = (int8_t)(pkt->getSNR() * 4);
  unsigned long rtt = millis() - _trace_started;
  int out_hops = (hop_count + 1) / 2;   // round trip turns around at the far end

  char buf[768];   // up to eight hops out and back, plus the round-trip line
  int off = snprintf(buf, sizeof(buf), "Round trip %lu.%lus\n\n", rtt / 1000, (rtt % 1000) / 100);
  for (int i = 0; i < hop_count && off < (int) sizeof(buf) - 48; i++) {
    char nm[20];
    traceHashName(&path_hashes[i * hash_sz], hash_sz, nm, sizeof(nm));
    off += snprintf(&buf[off], sizeof(buf) - off, "%s %.14s   %+.1f dB\n",
                    i < out_hops ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT, nm, (int8_t) path_snrs[i] / 4.0);
  }
  snprintf(&buf[off], sizeof(buf) - off, "%s You   %+.1f dB", LV_SYMBOL_LEFT, final_snr / 4.0);
  lv_label_set_text(trace_lbl, buf);
}

void contactPathOpen(int contact_idx) {
  detail_idx = contact_idx;
  path_open_cb(NULL);
}

// from loop(): a trace that never came back
void UITask::traceTick() {
  if (_trace_tag != 0 && millis() > _trace_deadline) {
    _trace_tag = 0;
    if (!lv_obj_has_flag(trace_scr, LV_OBJ_FLAG_HIDDEN)) {
      lv_label_set_text(trace_lbl,
          "No reply - trace timed out.\n\nA trace returns through the same\nhops, so hop 1 must be a repeater\nthis node can hear directly. Check\nthe path with Set path, or a hop is\noffline.");
    }
  }
}
