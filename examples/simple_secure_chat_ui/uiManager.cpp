#include <Arduino.h>

#include "esp_log.h"
#include "uiDefines.h"
#include "uiVars.h"

#include "uiManager.h"
#include "messageStore.h"

#include "../src/fonts/fonts.h"

#include <helpers/ContactInfo.h>
#include <helpers/AdvertDataHelpers.h>

#if defined(LANG_GR)
const char *UIManager::days[7] = {"Κυρ", "Δευ", "Τρι", "Τετ", "Πεμ", "Παρ", "Σαβ"};
const char *UIManager::months[12] = {"Ιαν", "Φεβ", "Μαρ", "Απρ", "Μαι", "Ιουν",
                                     "Ιουλ", "Αυγ", "Σεπ", "Οκτ", "Νοε", "Δεκ"};
#elif defined(LANG_EN)
const char *UIManager::days[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *UIManager::months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
#endif

#define TAG "UIManager"

extern void handleCommand(char *msg);

namespace {
struct UIContactInfo {
    ContactInfo info;
    lv_obj_t* badge = nullptr;
    lv_obj_t* badge_label = nullptr;
    lv_obj_t* label_lastseen = nullptr;
    uint32_t unread = 0;
};

UIContactInfo* findContactByPubKey(lv_obj_t* list, const uint8_t* pub_key) {
    if (!list) return nullptr;
    uint32_t cnt = lv_obj_get_child_cnt(list);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* row = lv_obj_get_child(list, i);
        UIContactInfo* uic = (UIContactInfo*) lv_obj_get_user_data(row);
        if (uic && memcmp(uic->info.id.pub_key, pub_key, 32) == 0) return uic;
    }
    return nullptr;
}

void updateBadge(UIContactInfo* uic) {
    if (!uic || !uic->badge || !uic->badge_label) return;
    if (uic->unread == 0) {
        lv_obj_add_flag(uic->badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(uic->badge, LV_OBJ_FLAG_HIDDEN);
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", (unsigned)uic->unread);
        lv_label_set_text(uic->badge_label, buf);
    }
}
} // namespace

UIManager::UIManager() {

  tmp_buf = (char*)malloc(128);

  lv_disp_t * dispp = lv_disp_get_default();
  lv_theme_t * theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                              false, LV_FONT_DEFAULT);
  lv_disp_set_theme(dispp, theme);
  ui_Screen1_screen_init();
  ui____initial_actions0 = lv_obj_create(NULL);
  lv_disp_load_scr(ui_Screen1);

}

void UIManager::format_time(uint32_t ts, char *buf, size_t len)
{
    time_t t = ts;
    struct tm *tm_info = localtime(&t);
    strftime(buf, len, "%H:%M:%S", tm_info);
}

void UIManager::format_datetime(char *buf, size_t size, const struct tm *timeinfo) {
    char tmp[64];
    strftime(tmp, sizeof(tmp), "%a, %d %b %Y", timeinfo);

    int wday = timeinfo->tm_wday; // 0=Κυρ ... 6=Σαβ
    int mon  = timeinfo->tm_mon;  // 0=Ιαν ... 11=Δεκ

    // replace %a and %b with selected language
    snprintf(buf, size, "%s, %02d %s %d", days[wday], timeinfo->tm_mday, months[mon], 1900 + timeinfo->tm_year);
}

void UIManager::updateDateTime(const struct tm timeinfo) {
  // TODO: Add to settings "Date format"
  char date_str[50];
  format_datetime(date_str, sizeof(date_str), &timeinfo);
  lv_label_set_text(ui_ValueDate, date_str);

  // TODO: Add to settings "Hour format"
  strftime(tmp_buf, 50, "%H:%M", &timeinfo);      // 24h format
  //strftime(tmp_buf, 50, "%I:%M %p", &timeinfo); // 12h format
  lv_label_set_text(ui_ValueTime, tmp_buf);

  // TODO: Add to settings "dim at night"
  // TODO: Add to settings "dim hours"
  // TODO: Add to settings "dim percentage"
  if (timeinfo.tm_hour > 21 || timeinfo.tm_hour < 7) {
    setNightMode(true);
  } else {
    setNightMode(false);
  }
}

void UIManager::clearDateTime() {
  #if defined(LANG_EN)
    uiManager->updateInfo("Clock sync...", COLOR_WHITE);
  #elif defined(LANG_GR)
    uiManager->updateInfo("Συγχρονισμός ώρας...", COLOR_WHITE);
  #endif
  lv_label_set_text(ui_ValueDate, "--- --/--/----");
  lv_label_set_text(ui_ValueTime, "--:--");
}

void UIManager::timestampToTime(time_t timestamp, char *buffer, size_t buffer_size) {
    struct tm *time_info;
    time_info = localtime(&timestamp);
    strftime(buffer, buffer_size, "%H:%M", time_info);
}

const char* UIManager::convertDegreesToDirection(int degrees) {
    // Normalize degrees to [0, 360)
    degrees = degrees % 360;
    if (degrees < 0) degrees += 360;

#if defined(LANG_EN)
    static constexpr const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
#elif defined(LANG_GR)
    static constexpr const char* dirs[] = {"Β", "ΒΑ", "Α", "ΝΑ", "Ν", "ΝΔ", "Δ", "ΒΔ"};
#else
    #error "No Language defined!"
#endif

    // Each direction covers 45°, starting at N = 0°
    int index = static_cast<int>((degrees + 22.5) / 45.0) % 8;
    return dirs[index];
}


int UIManager::windSpeedToBeaufort(float speed) {
    static const float limits[] = {
        0.5, 1.5, 3.3, 5.5, 7.9, 10.7,
        13.8, 17.1, 20.7, 24.4, 28.4, 32.6
    };

    for (int i = 0; i < 12; ++i)
        if (speed < limits[i])
            return i;
    return 12;
}

void UIManager::updateValues() {
    lv_label_set_text(ui_ValueTime, "--:--");
}

void UIManager::updateInfo(const char *str, uint32_t color) {
  // lv_label_set_text(ui_ValueLastUpdate, str);  
  // lv_obj_set_style_text_color(ui_ValueLastUpdate, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
}

void UIManager::addChatBubble(const char *time_str, const char *sender, const char *msg, bool is_self, bool do_scroll)
{
    // Remove oldest
    if (chat_count >= MAX_CHAT_MESSAGES) {
        lv_obj_del(chat_items[0]);
        memmove(&chat_items[0], &chat_items[1], sizeof(lv_obj_t*) * (MAX_CHAT_MESSAGES - 1));
        chat_count--;
    }

    // Row container (align bubble left/right)
    lv_obj_t *row = lv_obj_create(ui_ChannelMessages);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
        is_self ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START);

    // Bubble container (COLUMN)
    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_width(bubble, lv_pct(85));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, 12, 0);
    lv_obj_set_style_pad_all(bubble, 10, 0);    
    lv_obj_set_style_bg_color(bubble,
        is_self ? lv_color_hex(0x1E88E5) : lv_color_hex(0x2C2C2C), 0);

    // Vertical layout inside bubble; cross-align follows bubble side
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bubble,
        LV_FLEX_ALIGN_START,
        is_self ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START);

    // Header row (sender + time)
    lv_obj_t *hdr = lv_obj_create(bubble);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_outline_width(hdr, 0, 0);

    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr,
        is_self ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_START);

    lv_obj_t *lbl_sender = lv_label_create(hdr);
    lv_label_set_text(lbl_sender, sender);
    lv_obj_set_style_text_color(lbl_sender,
        is_self ? lv_color_hex(0xE3F2FD) : lv_color_hex(0x90CAF9), 0);
    lv_obj_set_style_text_font(lbl_sender, &lv_font_arial_22, 0);

    lv_obj_t *lbl_time = lv_label_create(hdr);
    lv_label_set_text(lbl_time, time_str);
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xB0B0B0), 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_arial_20, 0);

    // Message body (below header)
    lv_obj_t *lbl_msg = lv_label_create(bubble);
    lv_label_set_text(lbl_msg, msg);
    lv_label_set_long_mode(lbl_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_msg, lv_pct(100));
    lv_obj_set_style_text_align(lbl_msg, is_self ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);

    lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_msg, &lv_font_arial_26, 0);

    // Spacing between header and text
    //lv_obj_set_style_margin_top(lbl_msg, 6, 0);
    lv_obj_set_style_pad_row(bubble, 6, 0);

    chat_items[chat_count++] = row;

    if (do_scroll) lv_obj_scroll_to_view(row, LV_ANIM_ON);
  }

void UIManager::addPrivateChatBubble(const char *time_str, const char *msg, bool is_self, bool do_scroll) {

  lv_obj_set_style_pad_bottom(ui_ContactMessages, 20, 0);

  // 1. Row container – pushes bubble to the correct side
  lv_obj_t* row = LvObj(ui_ContactMessages)
    .width(lv_pct(100))
    .height(LV_SIZE_CONTENT)
    .bgOpa(0)
    .border(0)
    .padAll(4)
    .scrollable(false)
    .flexFlow(LV_FLEX_FLOW_ROW)
    .flexAlign(
        is_self ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,  // sent=right, received=left
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );
  if (!row) { Serial.println("[ui] OOM: addPrivateChatBubble row alloc failed"); return; }

  // 2. Bubble column: message text + timestamp below
  lv_obj_t* aligner = LvObj(row)
    .width(LV_SIZE_CONTENT)
    .height(LV_SIZE_CONTENT)
    .bgOpa(0)
    .border(0)
    .padAll(0)
    .scrollable(false)
    .flexFlow(LV_FLEX_FLOW_COLUMN)
    .flexAlign(
        LV_FLEX_ALIGN_START,
        is_self ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );
  lv_obj_set_style_pad_row(aligner, 2, 0);

  // 3. Message bubble label
  lv_obj_t *lbl_msg = lv_label_create(aligner);
  lv_label_set_long_mode(lbl_msg, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_msg, msg);
  lv_obj_set_style_text_font(lbl_msg, &lv_font_arial_22, 0);
  lv_obj_set_width(lbl_msg, 175);           // max width – fits in 310px panel with room to shift
  lv_obj_set_height(lbl_msg, LV_SIZE_CONTENT);
  lv_obj_set_style_text_align(lbl_msg, is_self ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_bg_opa(lbl_msg, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(lbl_msg, 12, 0);
  lv_obj_set_style_pad_all(lbl_msg, 10, 0);

  if (is_self) {
    lv_obj_set_style_bg_color(lbl_msg, lv_color_hex(0x1E88E5), 0);
    lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0xFFFFFF), 0);
  } else {
    lv_obj_set_style_bg_color(lbl_msg, lv_color_hex(0x2C2C2C), 0);
    lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0xFFFFFF), 0);
  }

  // 4. Timestamp below the bubble
  lv_obj_t *lbl_time = lv_label_create(aligner);
  lv_label_set_text(lbl_time, time_str);
  lv_obj_set_style_text_color(lbl_time, lv_color_hex(0x808080), 0);
  lv_obj_set_style_text_font(lbl_time, &lv_font_arial_14, 0);

  if (do_scroll) lv_obj_scroll_to_view(row, LV_ANIM_OFF);
}

void UIManager::scrollPrivateChatToBottom() {
  if (ui_ContactMessages)
    lv_obj_scroll_to_y(ui_ContactMessages, LV_COORD_MAX, LV_ANIM_OFF);
}

void UIManager::scrollPublicChatToBottom() {
  if (ui_ChannelMessages)
    lv_obj_scroll_to_y(ui_ChannelMessages, LV_COORD_MAX, LV_ANIM_OFF);
}

void UIManager::beginPublicHistoryLoad() {
  if (ui_ChannelMessages) lv_obj_set_layout(ui_ChannelMessages, 0);
}

void UIManager::endPublicHistoryLoad() {
  if (ui_ChannelMessages) lv_obj_set_flex_flow(ui_ChannelMessages, LV_FLEX_FLOW_COLUMN);
}

void UIManager::getInitials(const char *name, char *out) {
    out[0] = 0;
    if (!name || !name[0]) return;

    const char *p = name;
    while (*p && !isalnum((unsigned char)*p)) {
        p++;
    }
    
    char first = (*p) ? *p : name[0];
    char second = 0;
    const char *space = strchr(name, ' ');
    
    if (space) {
        const char *s = space + 1;
        while (*s && !isalnum((unsigned char)*s)) {
            s++;
        }
        if (*s) {
            second = *s;
        }
    }

    out[0] = toupper((unsigned char)first);
    if (second) {
        out[1] = toupper((unsigned char)second);
        out[2] = 0;
    } else {
        out[1] = 0;
    }
}

void UIManager::formatLastSeen(uint32_t ts, char *out, size_t len) {
    if (ts == 0) {
        snprintf(out, len, "Never");
        return;
    }

    time_t t = (time_t)ts;
    struct tm *tm = localtime(&t);

    if (tm == nullptr) {
        snprintf(out, len, "Unknown");
        return;
    }

    snprintf(out, len, "%02d:%02d %02d/%02d/%02d",
        tm->tm_hour, 
        tm->tm_min,
        tm->tm_mday, 
        tm->tm_mon + 1, 
        (tm->tm_year + 1900) % 100); // Προσθέτουμε το 1900 και παίρνουμε τα τελευταία 2 ψηφία
}

static void onContactClick(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->handleContactClick(e);
}

void UIManager::handleContactClick(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    UIContactInfo *uic = (UIContactInfo*) lv_obj_get_user_data(row);
    if (!uic) return;

    ContactInfo* c = &uic->info;
    Serial.printf("Clicked: %s\n", c->name);

    strncpy(currentContactName, c->name, sizeof(currentContactName) - 1);
    currentContactName[sizeof(currentContactName) - 1] = 0;
    memcpy(currentContactPubKey, c->id.pub_key, sizeof(currentContactPubKey));
    hasCurrentContact = true;

    if (ui_ContactName) {
        lv_label_set_text(ui_ContactName, currentContactName);
    }

    if (ui_ContactMessages) {
        lv_obj_clean(ui_ContactMessages);
    }

    uic->unread = 0;
    updateBadge(uic);

    setSendStatus(-1);

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "to %s", currentContactName);
    handleCommand(cmd);

    // Disable flex layout before bulk-loading history messages.
    // Without this, each lv_obj_create() child triggers lv_obj_update_layout()
    // which recalculates ALL children's positions → O(n²) → watchdog timeout.
    // lv_obj_set_flex_flow() below re-enables flex and does ONE O(n) pass.
    if (ui_ContactMessages) lv_obj_set_layout(ui_ContactMessages, 0);

    msgstore_load_dm(currentContactPubKey);

    if (ui_ContactMessages) lv_obj_set_flex_flow(ui_ContactMessages, LV_FLEX_FLOW_COLUMN);
    scrollPrivateChatToBottom();  // after layout is recalculated
}

void UIManager::addContactToUI(ContactInfo c)
{
    const int ROW_W  = 130;
    const int ROW_H  = 64;
    const int AVATAR = 44;
    const int PAD    = 4;

    // ============================
    // List row button
    // ============================
    lv_obj_t* btn = lv_list_add_btn(ui_Contacts, nullptr, nullptr);

    LvObj(btn, true)
        .size(ROW_W, ROW_H)
        .padAll(0)
        .bgColor(0x000000)
        .bgOpa(LV_OPA_COVER)
        .noDecor()
        .border(1)
        .scrollable(false);

    lv_obj_set_layout(btn, 0);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x111111), LV_STATE_PRESSED);

    // Store wrapper (ContactInfo + badge state)
    auto* store = new UIContactInfo();
    store->info = c;
    lv_obj_set_user_data(btn, store);

    lv_obj_add_event_cb(btn, onContactClick, LV_EVENT_CLICKED, this);

    // ============================
    // Avatar container (hidden when SHOW_CONTACT_AVATAR is not defined)
    // ============================
#ifdef SHOW_CONTACT_AVATAR
    lv_obj_t* content = LvObj(btn)
        .size(ROW_H, ROW_H)
        .position(0, 0)
        .padAll(0)
        .bgOpa(LV_OPA_TRANSP)
        .border(0)
        .scrollable(false)
        .raw();

    // Avatar circle
    lv_obj_t* avatar = LvObj(content)
        .size(AVATAR, AVATAR)
        .position(0, 10)
        .radius(LV_RADIUS_CIRCLE)
        .bgColor(0x4A90E2)
        .border(0)
        .raw();

    // Initials
    char initials[4];
    if (c.type == ADV_TYPE_REPEATER) {
        strcpy(initials, "(R)");
    } else {
        getInitials(c.name, initials);
    }

    LvLabel(avatar)
        .text(initials)
        .font(&lv_font_arial_22)
        .textColor(0xFFFFFF)
        .align(LV_ALIGN_CENTER);
#endif

    // ============================
    // Text column
    // ============================
#ifdef SHOW_CONTACT_AVATAR
    int text_x = PAD + AVATAR + PAD;
#else
    int text_x = 0;
#endif
    int text_w = ROW_W - text_x - PAD;

    lv_obj_t* text_col = LvObj(btn)
        .position(text_x, 0)
        .size(text_w, ROW_H - PAD)
        .padAll(0)
        .bgOpa(LV_OPA_TRANSP)
        .border(0)
        .scrollable(false)
        .raw();

    // Name label
    LvLabel(text_col)
        .text(c.name)
        .position(0, PAD + 4)
        .width(text_w)
        .font(&lv_font_arial_20)
        .textColor(0xFFFFFF)
        .wrap(false);

    // Last seen
    char lastSeen[32];
    formatLastSeen(c.lastmod, lastSeen, sizeof(lastSeen));

    lv_obj_t* lbl_ls = LvLabel(text_col)
        .text(lastSeen)
        .position(0, PAD + 32)
        .width(text_w)
        .font(&lv_font_arial_16)
        .textColor(0x888888)
        .wrap(false)
        .raw();
    store->label_lastseen = lbl_ls;

    // ============================
    // Unread badge (top-right corner of row)
    // ============================
    lv_obj_t* badge = LvObj(btn)
        .size(22, 22)
        .position(ROW_W - 26, 4)
        .radius(LV_RADIUS_CIRCLE)
        .bgColor(0xE53935)
        .border(0)
        .scrollable(false)
        .raw();
    lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
    LvObj(badge, true).clickable(false);

    lv_obj_t* badge_label = LvLabel(badge)
        .text("0")
        .font(&lv_font_arial_14)
        .textColor(0xFFFFFF)
        .align(LV_ALIGN_CENTER)
        .raw();
    LvObj(badge_label, true).clickable(false);

    store->badge = badge;
    store->badge_label = badge_label;

    // ============================
    // Disable child clicks
    // ============================
#ifdef SHOW_CONTACT_AVATAR
    LvObj(avatar, true).clickable(false);
#endif
    LvObj(text_col, true).clickable(false);
}

void UIManager::updateContactLastSeen(const uint8_t* pub_key, uint32_t lastmod)
{
    // Called from Core 1 (mesh task). LVGL is NOT thread-safe, so we must NOT
    // call any lv_* write functions here. We only update the data field (atomic
    // 32-bit write on ESP32). The LVGL timer (Core 0) will refresh the label.
    UIContactInfo* uic = findContactByPubKey(ui_Contacts, pub_key);
    if (!uic) return;
    uic->info.lastmod = lastmod;  // atomic on ESP32, safe without mutex
}

// Called by an LVGL timer on Core 0 — safe to call lv_* here.
void UIManager::refreshLastSeenLabels()
{
    if (!ui_Contacts) return;
    uint32_t cnt = lv_obj_get_child_cnt(ui_Contacts);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* row = lv_obj_get_child(ui_Contacts, i);
        UIContactInfo* uic = (UIContactInfo*) lv_obj_get_user_data(row);
        if (!uic || !uic->label_lastseen) continue;
        char buf[32];
        formatLastSeen(uic->info.lastmod, buf, sizeof(buf));
        lv_label_set_text(uic->label_lastseen, buf);
    }
}

void UIManager::onShowKeyboard()
{
    LvKeyboard(ui_Keyboard, true).show(true);
    LvObj(ui_DimOverlay, true).clickable(true);
    if (activeInput)   LvObj(activeInput,   true).positionY(channelInputBaseKeybOnY);
    if (activeSendBtn) LvObj(activeSendBtn, true).positionY(channelInputBaseKeybOnY);
}

void UIManager::onHideKeyboard()
{
    LvKeyboard(ui_Keyboard, true).show(false);
    LvObj(ui_DimOverlay, true).clickable(false);
    if (activeInput)   LvObj(activeInput,   true).positionY(activeInputBaseY);
    if (activeSendBtn) LvObj(activeSendBtn, true).positionY(activeInputBaseY);
}

static void s_onRestartClick(lv_event_t *e)
{
    ESP.restart();
}

static void s_onAdvertiseClick(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->onAdvertiseClick(e);
}

void UIManager::onAdvertiseClick(lv_event_t* e)
{
    char cmd[16] = "advert";
    handleCommand(cmd);
}

static void s_onChannelInputFocus(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->onChannelInputFocus(e);
}

void UIManager::onChannelInputFocus(lv_event_t* e)
{
    lv_obj_t* ta = lv_event_get_target(e);
    if(!ui_Keyboard || !ta) return;

    activeInput      = ui_ChannelInput;
    activeSendBtn    = ui_SendBtn;
    activeInputBaseY = channelInputBaseY;

    lv_keyboard_set_textarea(ui_Keyboard, ta);
    onShowKeyboard();
}

static void s_onContactInputFocus(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->onContactInputFocus(e);
}

void UIManager::onContactInputFocus(lv_event_t* e)
{
    lv_obj_t* ta = lv_event_get_target(e);
    if(!ui_Keyboard || !ta) return;

    activeInput      = ui_ContactInput;
    activeSendBtn    = ui_ContactSendBtn;
    activeInputBaseY = channelInputBaseY;

    lv_keyboard_set_textarea(ui_Keyboard, ta);
    onShowKeyboard();
}

static void s_onContactSendClick(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->onContactSendClick(e);
}

void UIManager::onContactSendClick(lv_event_t* e)
{
    if (!hasCurrentContact) {
        Serial.println("[ui] no contact selected — DM ignored");
        return;
    }

    const char* msg = lv_textarea_get_text(ui_ContactInput);
    if (msg == nullptr || msg[0] == '\0') return;

    char msgCopy[200];
    strncpy(msgCopy, msg, sizeof(msgCopy) - 1);
    msgCopy[sizeof(msgCopy) - 1] = 0;

    lv_textarea_set_text(ui_ContactInput, "");

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "send %s", msgCopy);
    handleCommand(cmd);

    char time_buf[16];
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", t.tm_hour, t.tm_min);

    addPrivateChatBubble(time_buf, msgCopy, true);
    msgstore_append_dm(currentContactPubKey, (uint32_t)now, true, msgCopy);

    setSendStatus(0);

    onHideKeyboard();
}

static void s_onSendClick(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->onSendClick(e);
}

void UIManager::onSendClick(lv_event_t* e)
{
    char fullMessage[260];
    char msgCopy[200];

    const char* msg = lv_textarea_get_text(ui_ChannelInput);
    if(msg == NULL || msg[0] == '\0') return;

    strncpy(msgCopy, msg, sizeof(msgCopy) - 1);
    msgCopy[sizeof(msgCopy) - 1] = '\0';

    lv_textarea_set_text(ui_ChannelInput, "");

    snprintf(fullMessage, sizeof(fullMessage), "public %s", msgCopy);
    handleCommand(fullMessage);

    char time_buf[16];
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    sprintf(time_buf, "%02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
    const char* sender = (myNodeName[0] != 0) ? myNodeName : "Me";
    addChatBubble(time_buf, sender, msgCopy, true);
    msgstore_append_public((uint32_t)now, sender, true, msgCopy);

    onHideKeyboard();
}

void UIManager::setMyNodeName(const char* name) {
    if (!name) { myNodeName[0] = 0; return; }
    strncpy(myNodeName, name, sizeof(myNodeName) - 1);
    myNodeName[sizeof(myNodeName) - 1] = 0;
}

void UIManager::routeIncomingDM(const uint8_t* from_pub_key, const char* from_name,
                                const char* time_str, const char* text)
{
    if (hasCurrentContact && memcmp(from_pub_key, currentContactPubKey, 32) == 0) {
        addPrivateChatBubble(time_str, text, false);
        return;
    }

    UIContactInfo* uic = findContactByPubKey(ui_Contacts, from_pub_key);
    if (uic) {
        uic->unread++;
        updateBadge(uic);
    }
}

void UIManager::setSendStatus(int state)
{
    if (!ui_ContactStatus) return;

    switch (state) {
    case 0:
        lv_label_set_text(ui_ContactStatus,
            #if defined(LANG_GR)
            "Αποστολή...");
            #else
            "Sending...");
            #endif
        lv_obj_set_style_text_color(ui_ContactStatus, lv_color_hex(0xFFC107), 0);
        break;
    case 1:
        lv_label_set_text(ui_ContactStatus, LV_SYMBOL_OK
            #if defined(LANG_GR)
            "  Παραδόθηκε");
            #else
            "  Delivered");
            #endif
        lv_obj_set_style_text_color(ui_ContactStatus, lv_color_hex(0x4CAF50), 0);
        break;
    case 2:
        lv_label_set_text(ui_ContactStatus, LV_SYMBOL_CLOSE
            #if defined(LANG_GR)
            "  Δεν έγινε ack");
            #else
            "  No ack");
            #endif
        lv_obj_set_style_text_color(ui_ContactStatus, lv_color_hex(0xE53935), 0);
        break;
    default:
        lv_label_set_text(ui_ContactStatus, "");
        break;
    }
}

static void s_onSettingsInputFocus(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->onSettingsInputFocus(e);
}

void UIManager::onSettingsInputFocus(lv_event_t* e)
{
    lv_obj_t* ta = lv_event_get_target(e);
    if(!ui_Keyboard || !ta) return;

    // Settings inputs do not slide up — keyboard floats over the form.
    activeInput      = nullptr;
    activeSendBtn    = nullptr;

    lv_keyboard_set_textarea(ui_Keyboard, ta);
    LvKeyboard(ui_Keyboard, true).show(true);
    LvObj(ui_DimOverlay, true).clickable(true);
}

static void s_onSettingsSaveClick(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->onSettingsSaveClick(e);
}

static void s_onPresetChange(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->onPresetChange(lv_dropdown_get_selected(lv_event_get_target(e)));
}

void UIManager::onPresetChange(uint16_t idx)
{
    if (idx == 0) {  // Wide band
        if (ui_SettingsFreq) lv_textarea_set_text(ui_SettingsFreq, "869.525");
        if (ui_SettingsBw)   lv_textarea_set_text(ui_SettingsBw,   "250");
        if (ui_SettingsSf)   lv_textarea_set_text(ui_SettingsSf,   "11");
        if (ui_SettingsCr)   lv_textarea_set_text(ui_SettingsCr,   "5");
    } else {          // Narrow band
        if (ui_SettingsFreq) lv_textarea_set_text(ui_SettingsFreq, "869.618");
        if (ui_SettingsBw)   lv_textarea_set_text(ui_SettingsBw,   "62.5");
        if (ui_SettingsSf)   lv_textarea_set_text(ui_SettingsSf,   "8");
        if (ui_SettingsCr)   lv_textarea_set_text(ui_SettingsCr,   "8");
    }
}

void UIManager::onSettingsSaveClick(lv_event_t* e)
{
    char cmd[96];

    const char* name = ui_SettingsName ? lv_textarea_get_text(ui_SettingsName) : "";
    const char* freq = ui_SettingsFreq ? lv_textarea_get_text(ui_SettingsFreq) : "";
    const char* bw   = ui_SettingsBw   ? lv_textarea_get_text(ui_SettingsBw)   : "";
    const char* sf   = ui_SettingsSf   ? lv_textarea_get_text(ui_SettingsSf)   : "";
    const char* cr   = ui_SettingsCr   ? lv_textarea_get_text(ui_SettingsCr)   : "";
    const char* tx   = ui_SettingsTx   ? lv_textarea_get_text(ui_SettingsTx)   : "";

    if (name && name[0]) {
        snprintf(cmd, sizeof(cmd), "set name %s", name);
        handleCommand(cmd);
    }
    if (freq && freq[0]) {
        snprintf(cmd, sizeof(cmd), "set freq %s", freq);
        handleCommand(cmd);
    }
    if (bw && bw[0]) {
        snprintf(cmd, sizeof(cmd), "set bw %s", bw);
        handleCommand(cmd);
    }
    if (sf && sf[0]) {
        snprintf(cmd, sizeof(cmd), "set sf %s", sf);
        handleCommand(cmd);
    }
    if (cr && cr[0]) {
        snprintf(cmd, sizeof(cmd), "set cr %s", cr);
        handleCommand(cmd);
    }
    if (tx && tx[0]) {
        snprintf(cmd, sizeof(cmd), "set tx %s", tx);
        handleCommand(cmd);
    }

    if (ui_SettingsStatus) {
        #if defined(LANG_GR)
        lv_label_set_text(ui_SettingsStatus, "Αποθηκεύτηκε. Επανεκκίνηση...");
        #else
        lv_label_set_text(ui_SettingsStatus, "Saved. Restarting...");
        #endif
    }

    onHideKeyboard();

    Serial.flush();
    delay(800);
    ESP.restart();
}

void UIManager::populateSettings(const char* name, float freq, float bw, uint8_t sf, uint8_t cr,
                                 uint8_t tx_power, const char* fw_ver, const char* build_date)
{
    if (ui_SettingsName && name) lv_textarea_set_text(ui_SettingsName, name);

    if (ui_SettingsFreq) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.3f", freq);
        lv_textarea_set_text(ui_SettingsFreq, buf);
    }
    if (ui_SettingsBw) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", bw);
        lv_textarea_set_text(ui_SettingsBw, buf);
    }
    if (ui_SettingsSf) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", (unsigned)sf);
        lv_textarea_set_text(ui_SettingsSf, buf);
    }
    if (ui_SettingsCr) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", (unsigned)cr);
        lv_textarea_set_text(ui_SettingsCr, buf);
    }
    if (ui_SettingsTx) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", (unsigned)tx_power);
        lv_textarea_set_text(ui_SettingsTx, buf);
    }
    if (ui_SettingsFw && fw_ver && build_date) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s  (%s)", fw_ver, build_date);
        lv_label_set_text(ui_SettingsFw, buf);
    }

    // Select the matching preset (does not fire the VALUE_CHANGED event)
    if (ui_SettingsPreset) {
        bool isNarrow = (fabsf(freq - 869.618f) < 0.01f && bw < 100.0f && sf == 8 && cr == 8);
        lv_dropdown_set_selected(ui_SettingsPreset, isNarrow ? 1 : 0);
    }
}

void UIManager::populateHome(const char* name, const char* pub_key_hex,
                             int contact_count, float freq)
{
    if (ui_HomeNodeName && name) lv_label_set_text(ui_HomeNodeName, name);
    if (ui_HomePubKey && pub_key_hex) lv_label_set_text(ui_HomePubKey, pub_key_hex);

    if (ui_HomeInfo) {
        char buf[64];
        #if defined(LANG_GR)
        snprintf(buf, sizeof(buf), "%.3f MHz  ·  %d επαφές", freq, contact_count);
        #else
        snprintf(buf, sizeof(buf), "%.3f MHz  ·  %d contacts", freq, contact_count);
        #endif
        lv_label_set_text(ui_HomeInfo, buf);
    }
}

static void s_onKeyboardEvent(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->onKeyboardEvent(e);
}

void UIManager::onKeyboardEvent(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
    {
        LvKeyboard(ui_Keyboard, true).show(false);

        if (activeInput)   LvObj(activeInput,   true).positionY(activeInputBaseY);
        if (activeSendBtn) LvObj(activeSendBtn, true).positionY(activeInputBaseY);

        LvObj(ui_DimOverlay, true)
            .bgOpa(0)
            .clickable(false);
    }
}


static void s_onDimOverlayClick(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->onDimOverlayClick(e);
}

void UIManager::onDimOverlayClick(lv_event_t* e)
{
    onHideKeyboard();
}

static void onScrollBeginEvent(lv_event_t *e)
{
    UIManager *self = (UIManager*) lv_event_get_user_data(e);
    if(self) self->scroll_begin_event(e);
}

void UIManager::scroll_begin_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCROLL_BEGIN) {
        lv_anim_t* a = (lv_anim_t*)lv_event_get_param(e);
        if (a)  a->time = 0;

    }
}

void UIManager::ui_Screen1_screen_init(void)
{
    //lv_disp_set_rotation(disp, LV_DISP_ROT_90);
    //ui_Screen1 = lv_obj_create(NULL);

    ui_Screen1 = LvObj(NULL)
        .scrollable(false)
        .bgColor(0x000000)
        .bgOpa(255);

    LvTabView tabView(ui_Screen1);
    tabView
        .size(480, 480)
        .align(LV_ALIGN_CENTER)
        .bgColor(0x000000)
        .contentNoScroll()
        .tabBtnBg(0x424242)
        .tabBtnText(0xFFFFFF, &lv_font_arial_18);

    ui_TabView1 = tabView.raw();
    
    #if defined(LANG_EN) 
        ui_TabPageHome = tabView.addTab("Home");           
        ui_TabPageContacts = tabView.addTab("Contacts");
        ui_TabPageChannels = tabView.addTab("Channels");
        ui_TabPageSettings = tabView.addTab("Settings");        
    #elif defined(LANG_GR)
        ui_TabPageHome = tabView.addTab("Αρχική");           
        ui_TabPageContacts = tabView.addTab("Επαφές");
        ui_TabPageChannels = tabView.addTab("Κανάλια");
        ui_TabPageSettings = tabView.addTab("Ρυθμίσεις");        
    #endif 

    lv_obj_clear_flag(ui_TabPageHome, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_TabPageContacts, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ui_TabPageChannels, LV_OBJ_FLAG_SCROLLABLE);

    // Node name (large)
    ui_HomeNodeName = LvLabel(ui_TabPageHome)
        .text("...")
        .width(LV_SIZE_CONTENT)
        .height(LV_SIZE_CONTENT)
        .font(&lv_font_arial_26)
        .textColor(0xFFFFFF)
        .align(LV_ALIGN_CENTER)
        .position(0, -200);

    // Public key (short hex)
    ui_HomePubKey = LvLabel(ui_TabPageHome)
        .text("")
        .width(LV_SIZE_CONTENT)
        .height(LV_SIZE_CONTENT)
        .font(&lv_font_arial_18)
        .textColor(0x888888)
        .align(LV_ALIGN_CENTER)
        .position(0, -170);

    // Freq + contacts count
    ui_HomeInfo = LvLabel(ui_TabPageHome)
        .text("")
        .width(LV_SIZE_CONTENT)
        .height(LV_SIZE_CONTENT)
        .font(&lv_font_arial_20)
        .textColor(0xAAAAAA)
        .align(LV_ALIGN_CENTER)
        .position(0, -140);

    ui_ValueDate = LvLabel(ui_TabPageHome)
        .text("--- --/--/----")
        .width(LV_SIZE_CONTENT)
        .height(LV_SIZE_CONTENT)
        .font(&lv_font_arial_40)
        .textColor(0xFFFFFF)
        .opa(255)
        .align(LV_ALIGN_CENTER)
        .position(0, -85);

    ui_ValueTime = LvLabel(ui_TabPageHome)
        .text("--:--")
        .width(LV_SIZE_CONTENT)
        .height(LV_SIZE_CONTENT)
        .font(&lv_font_arial_48)
        .textColor(0xFFFFFF)
        .opa(255)
        .align(LV_ALIGN_CENTER)
        .position(0, -25);

    // Advertise button
    ui_AdvertiseBtn = LvButton(ui_TabPageHome)
        .size(220, 56)
        .align(LV_ALIGN_CENTER)
        .position(0, 55)
        .bgColor(0x1565C0)
        .onClick(s_onAdvertiseClick, this)
        .raw();

    lv_obj_t* ui_AdvertiseLabel = LvLabel(ui_AdvertiseBtn)
        #if defined(LANG_GR)
        .text(LV_SYMBOL_WIFI "  Advertise")
        #else
        .text(LV_SYMBOL_WIFI "  Advertise")
        #endif
        .font(&lv_font_arial_22)
        .textColor(0xFFFFFF)
        .raw();
    lv_obj_center(ui_AdvertiseLabel);

    lv_obj_t* ui_RestartBtn = LvButton(ui_TabPageHome)
        .size(220, 56)
        .align(LV_ALIGN_CENTER)
        .position(0, 130)
        .bgColor(0xC62828)
        .onClick(s_onRestartClick, nullptr)
        .raw();

    lv_obj_t* ui_RestartLabel = LvLabel(ui_RestartBtn)
        #if defined(LANG_EN)
        .text(LV_SYMBOL_REFRESH "  Restart")
        #elif defined(LANG_GR)
        .text(LV_SYMBOL_REFRESH "  Επανεκκίνηση")
        #endif
        .font(&lv_font_arial_22)
        .textColor(0xFFFFFF)
        .raw();
    lv_obj_center(ui_RestartLabel);

    ui_Contacts = LvList(ui_TabPageContacts)
        .width(140)
        .height(350)
        .align(LV_ALIGN_CENTER)
        .position(-155, -25)
        .transparent()
        .raw();

    lv_obj_set_style_bg_opa(ui_Contacts, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_Contacts, 0, 0);
    lv_obj_set_style_outline_width(ui_Contacts, 0, 0);
    lv_obj_set_style_shadow_width(ui_Contacts, 0, 0);
    //lv_obj_set_scrollbar_mode(ui_Contacts, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui_Contacts, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_border_width(ui_Contacts, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(ui_Contacts, 8, 0);

    // Vertical divider between contact list and messages panel
    LvObj(ui_TabPageContacts)
        .size(1, 350)
        .align(LV_ALIGN_CENTER)
        .position(-80, -25)
        .bgColor(0x333333)
        .border(0)
        .scrollable(false)
        .radius(0);

    ui_ContactName = LvLabel(ui_TabPageContacts)
        .text("")
        .width(300)
        .height(LV_SIZE_CONTENT)
        .font(&lv_font_arial_22)
        .textColor(0xFFFFFF)
        .align(LV_ALIGN_CENTER)
        .position(80, -195);

    ui_ContactMessages = LvList(ui_TabPageContacts)
        .width(310)
        .height(290)
        .align(LV_ALIGN_CENTER)
        .position(80, -40)
        .transparent()
        .raw();

    lv_obj_set_style_bg_color(ui_ContactMessages, lv_color_hex(0), 0);
    lv_obj_set_style_bg_opa(ui_ContactMessages, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_ContactMessages, 0, 0);
    lv_obj_set_style_outline_width(ui_ContactMessages, 0, 0);
    lv_obj_set_style_shadow_width(ui_ContactMessages, 0, 0);
    //lv_obj_set_scrollbar_mode(ui_ContactMessages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui_ContactMessages, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_border_width(ui_ContactMessages, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(ui_ContactMessages, 8, 0);

    ui_ContactInput = LvTextArea(ui_TabPageContacts)
        .size(371, 40)
        .align(LV_ALIGN_CENTER)
        .position(-55, channelInputBaseY)
        .oneLine(true)
        #if defined(LANG_EN)
        .placeholder("Write message...")
        #elif defined(LANG_GR)
        .placeholder("Γράψε μήνυμα...")
        #endif
        .font(&lv_font_arial_20)
        .bgColor(0x111111)
        .textColor(0xFFFFFF)
        .borderColor(0x444444)
        .borderWidth(1)
        .radius(6)
        .onFocus(s_onContactInputFocus, this)
        .raw();

    ui_ContactSendBtn = LvButton(ui_TabPageContacts)
        .size(105, 42)
        .align(LV_ALIGN_CENTER)
        .position(188, channelInputBaseY)
        .bgColor(0x3A7AFE)
        .onClick(s_onContactSendClick, this)
        .raw();

    ui_ContactSendLabel = LvLabel(ui_ContactSendBtn)
        #if defined(LANG_EN)
        .text("Send")
        #elif defined(LANG_GR)
        .text("Αποστολή")
        #endif
        .font(&lv_font_arial_18);
    lv_obj_center(ui_ContactSendLabel);

    ui_ContactStatus = LvLabel(ui_TabPageContacts)
        .text("")
        .font(&lv_font_arial_14)
        .textColor(0xAAAAAA)
        .align(LV_ALIGN_CENTER)
        .position(80, channelInputBaseY + 30);
    
    // LvObj(ui_TabPageContacts)
    //     .size(2, 400)
    //     .position(222, 0)
    //     .bgColor(0x444444)
    //     .border(0)
    //     .scrollable(false)
    //     .radius(0);

    ui_Channels = LvDropdown(ui_TabPageChannels)
        .options("Public")
        .width(200)
        .align(LV_ALIGN_CENTER)
        .position(-135, -182)
        .clickable(true)
        .raw();

    ui_ChannelMessages = LvList(ui_TabPageChannels)
        .width(460)
        .height(250)
        .align(LV_ALIGN_CENTER)
        .transparent()
        .padRow(10)
        .position(0, -10)
        .bgColor(0)
        .bgOpa(0)
        .border(0)
        .noDecor()
        .raw();

    //lv_obj_set_scrollbar_mode(ui_ChannelMessages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui_ChannelMessages, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_border_width(ui_ChannelMessages, 0, LV_PART_ITEMS);

    ui_ChannelDivider = LvObj(ui_TabPageChannels)
        .size(460, 1)
        .align(LV_ALIGN_CENTER)
        .position(0, 150)
        .bgColor(0x444444)
        .border(0)
        .raw();

    ui_DimOverlay = LvObj(ui_Screen1)
        .size(lv_pct(100), lv_pct(100))
        .align(LV_ALIGN_CENTER)
        .bgColor(0x000000)
        .bgOpa(0)
        .bringToFront()
        .onClick(s_onDimOverlayClick, this)
        .scrollable(false)
        .clickable(true)
        .raw();
    // Remove only border/padding — do NOT use remove_style_all because it also
    // resets width/height (style props in LVGL8) back to 100×100 default,
    // which causes the white-square artifact when night mode activates.
    lv_obj_set_style_border_width(ui_DimOverlay, 0, 0);
    lv_obj_set_style_outline_width(ui_DimOverlay, 0, 0);
    lv_obj_set_style_shadow_width(ui_DimOverlay, 0, 0);
    lv_obj_set_style_pad_all(ui_DimOverlay, 0, 0);
    lv_obj_clear_flag(ui_DimOverlay, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(ui_DimOverlay, LV_OBJ_FLAG_SCROLL_CHAIN_VER);


    ui_ChannelInput = LvTextArea(ui_TabPageChannels)
        .size(360, 40)
        .align(LV_ALIGN_CENTER)
        .position(-55, channelInputBaseY)
        .oneLine(true)
        #if defined(LANG_EN)
        .placeholder("Write message...")
        #elif defined(LANG_GR)
        .placeholder("Γράψε μήνυμα...")
        #endif 
        .font(&lv_font_arial_20)
        .bgColor(0x111111)
        .textColor(0xFFFFFF)
        .borderColor(0x444444)
        .borderWidth(1)
        .radius(6)
        .onFocus(s_onChannelInputFocus, this)
        .raw();

    ui_SendBtn = LvButton(ui_TabPageChannels)
        .size(90, 42)
        .align(LV_ALIGN_CENTER)
        .position(180, channelInputBaseY)
        .bgColor(0x3A7AFE)
        .onClick(s_onSendClick, this)
        .raw();

    iu_SendLabel = LvLabel(ui_SendBtn)
        #if defined(LANG_EN)
        .text("Send")
        #elif defined(LANG_GR)
        .text("Αποστολή")        
        #endif
        .font(&lv_font_arial_18);
    lv_obj_center(iu_SendLabel);

    // ---- Settings tab (scrollable, horizontal label+input rows) ----
    lv_obj_add_flag(ui_TabPageSettings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ui_TabPageSettings, LV_SCROLLBAR_MODE_AUTO);

    // Layout constants: label on left, input on right, same row Y
    // Label: width=175, center at x=-115 from tab center  → left=40, right=212
    // Input: width=240, center at x=100 from tab center   → left=220, right=460
    const lv_coord_t LBL_X  = -115;
    const lv_coord_t LBL_W  = 175;
    const lv_coord_t INP_X  = 100;
    const lv_coord_t INP_W  = 240;
    const lv_coord_t INP_H  = 40;

    lv_coord_t rowY = -195;

    // ---- Row 1: Device name ----
    {
        lv_obj_t* lbl = LvLabel(ui_TabPageSettings)
            #if defined(LANG_GR)
            .text("Όνομα")
            #else
            .text("Device name")
            #endif
            .font(&lv_font_arial_20)
            .textColor(0xCCCCCC)
            .width(LBL_W)
            .align(LV_ALIGN_CENTER)
            .position(LBL_X, rowY)
            .raw();
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);

        ui_SettingsName = LvTextArea(ui_TabPageSettings)
            .size(INP_W, INP_H)
            .align(LV_ALIGN_CENTER)
            .position(INP_X, rowY)
            .oneLine(true)
            .font(&lv_font_arial_20)
            .bgColor(0x111111)
            .textColor(0xFFFFFF)
            .borderColor(0x444444)
            .borderWidth(1)
            .radius(6)
            .onFocus(s_onSettingsInputFocus, this)
            .raw();
    }
    rowY += 50;

    // ---- Row 2: Preset dropdown ----
    {
        lv_obj_t* lbl = LvLabel(ui_TabPageSettings)
            .text("Preset")
            .font(&lv_font_arial_20)
            .textColor(0xCCCCCC)
            .width(LBL_W)
            .align(LV_ALIGN_CENTER)
            .position(LBL_X, rowY)
            .raw();
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);

        ui_SettingsPreset = LvDropdown(ui_TabPageSettings)
            .options("Wide band\nNarrow band")
            .width(INP_W)
            .align(LV_ALIGN_CENTER)
            .position(INP_X, rowY)
            .raw();

        lv_obj_set_height(ui_SettingsPreset, INP_H);
        lv_obj_set_style_bg_color(ui_SettingsPreset, lv_color_hex(0x111111), LV_PART_MAIN);
        lv_obj_set_style_text_color(ui_SettingsPreset, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_border_color(ui_SettingsPreset, lv_color_hex(0x444444), LV_PART_MAIN);
        lv_obj_set_style_border_width(ui_SettingsPreset, 1, LV_PART_MAIN);
        lv_obj_set_style_text_font(ui_SettingsPreset, &lv_font_arial_20, LV_PART_MAIN);

        lv_obj_t* ddList = lv_dropdown_get_list(ui_SettingsPreset);
        if (ddList) {
            lv_obj_set_style_bg_color(ddList, lv_color_hex(0x222222), 0);
            lv_obj_set_style_text_color(ddList, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(ddList, &lv_font_arial_20, 0);
            lv_obj_set_style_border_color(ddList, lv_color_hex(0x444444), 0);
        }
        lv_obj_add_event_cb(ui_SettingsPreset, s_onPresetChange, LV_EVENT_VALUE_CHANGED, this);
    }
    rowY += 50;

    // ---- Row 3: Frequency ----
    {
        lv_obj_t* lbl = LvLabel(ui_TabPageSettings)
            #if defined(LANG_GR)
            .text("Συχνότητα (MHz)")
            #else
            .text("Frequency (MHz)")
            #endif
            .font(&lv_font_arial_20)
            .textColor(0xCCCCCC)
            .width(LBL_W)
            .align(LV_ALIGN_CENTER)
            .position(LBL_X, rowY)
            .raw();
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);

        ui_SettingsFreq = LvTextArea(ui_TabPageSettings)
            .size(INP_W, INP_H)
            .align(LV_ALIGN_CENTER)
            .position(INP_X, rowY)
            .oneLine(true)
            .font(&lv_font_arial_20)
            .bgColor(0x111111)
            .textColor(0xFFFFFF)
            .borderColor(0x444444)
            .borderWidth(1)
            .radius(6)
            .onFocus(s_onSettingsInputFocus, this)
            .raw();
    }
    rowY += 50;

    // ---- Row 4: Bandwidth ----
    {
        lv_obj_t* lbl = LvLabel(ui_TabPageSettings)
            #if defined(LANG_GR)
            .text("Bandwidth (kHz)")
            #else
            .text("Bandwidth (kHz)")
            #endif
            .font(&lv_font_arial_20)
            .textColor(0xCCCCCC)
            .width(LBL_W)
            .align(LV_ALIGN_CENTER)
            .position(LBL_X, rowY)
            .raw();
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);

        ui_SettingsBw = LvTextArea(ui_TabPageSettings)
            .size(INP_W, INP_H)
            .align(LV_ALIGN_CENTER)
            .position(INP_X, rowY)
            .oneLine(true)
            .font(&lv_font_arial_20)
            .bgColor(0x111111)
            .textColor(0xFFFFFF)
            .borderColor(0x444444)
            .borderWidth(1)
            .radius(6)
            .onFocus(s_onSettingsInputFocus, this)
            .raw();
    }
    rowY += 50;

    // ---- Row 5: Spreading Factor ----
    {
        lv_obj_t* lbl = LvLabel(ui_TabPageSettings)
            #if defined(LANG_GR)
            .text("Spreading Factor")
            #else
            .text("Spreading Factor")
            #endif
            .font(&lv_font_arial_20)
            .textColor(0xCCCCCC)
            .width(LBL_W)
            .align(LV_ALIGN_CENTER)
            .position(LBL_X, rowY)
            .raw();
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);

        ui_SettingsSf = LvTextArea(ui_TabPageSettings)
            .size(INP_W, INP_H)
            .align(LV_ALIGN_CENTER)
            .position(INP_X, rowY)
            .oneLine(true)
            .font(&lv_font_arial_20)
            .bgColor(0x111111)
            .textColor(0xFFFFFF)
            .borderColor(0x444444)
            .borderWidth(1)
            .radius(6)
            .onFocus(s_onSettingsInputFocus, this)
            .raw();
    }
    rowY += 50;

    // ---- Row 6: Coding Rate ----
    {
        lv_obj_t* lbl = LvLabel(ui_TabPageSettings)
            #if defined(LANG_GR)
            .text("Coding Rate")
            #else
            .text("Coding Rate")
            #endif
            .font(&lv_font_arial_20)
            .textColor(0xCCCCCC)
            .width(LBL_W)
            .align(LV_ALIGN_CENTER)
            .position(LBL_X, rowY)
            .raw();
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);

        ui_SettingsCr = LvTextArea(ui_TabPageSettings)
            .size(INP_W, INP_H)
            .align(LV_ALIGN_CENTER)
            .position(INP_X, rowY)
            .oneLine(true)
            .font(&lv_font_arial_20)
            .bgColor(0x111111)
            .textColor(0xFFFFFF)
            .borderColor(0x444444)
            .borderWidth(1)
            .radius(6)
            .onFocus(s_onSettingsInputFocus, this)
            .raw();
    }
    rowY += 50;

    // ---- Row 7: TX power ----
    {
        lv_obj_t* lbl = LvLabel(ui_TabPageSettings)
            #if defined(LANG_GR)
            .text("Ισχύς (dBm)")
            #else
            .text("TX power (dBm)")
            #endif
            .font(&lv_font_arial_20)
            .textColor(0xCCCCCC)
            .width(LBL_W)
            .align(LV_ALIGN_CENTER)
            .position(LBL_X, rowY)
            .raw();
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);

        ui_SettingsTx = LvTextArea(ui_TabPageSettings)
            .size(INP_W, INP_H)
            .align(LV_ALIGN_CENTER)
            .position(INP_X, rowY)
            .oneLine(true)
            .font(&lv_font_arial_20)
            .bgColor(0x111111)
            .textColor(0xFFFFFF)
            .borderColor(0x444444)
            .borderWidth(1)
            .radius(6)
            .onFocus(s_onSettingsInputFocus, this)
            .raw();
    }
    rowY += 43;

    // Firmware (read-only, centered)
    ui_SettingsFw = LvLabel(ui_TabPageSettings)
        .text("...")
        .font(&lv_font_arial_18)
        .textColor(0x888888)
        .align(LV_ALIGN_CENTER)
        .position(0, rowY);
    rowY += 42;

    // Save button
    ui_SettingsSaveBtn = LvButton(ui_TabPageSettings)
        .size(270, 50)
        .align(LV_ALIGN_CENTER)
        .position(0, rowY)
        .bgColor(0x2E7D32)
        .onClick(s_onSettingsSaveClick, this)
        .raw();

    lv_obj_t* saveLabel = LvLabel(ui_SettingsSaveBtn)
        #if defined(LANG_GR)
        .text(LV_SYMBOL_SAVE "  Αποθήκευση & Επανεκκίνηση")
        #else
        .text(LV_SYMBOL_SAVE "  Save & Restart")
        #endif
        .font(&lv_font_arial_18)
        .textColor(0xFFFFFF)
        .raw();
    lv_obj_center(saveLabel);
    rowY += 56;

    // Status label
    ui_SettingsStatus = LvLabel(ui_TabPageSettings)
        .text("")
        .font(&lv_font_arial_18)
        .textColor(0xFFC107)
        .align(LV_ALIGN_CENTER)
        .position(0, rowY);

    ui_Keyboard = LvKeyboard(lv_layer_top())
        .size(480, 200)
        .align(LV_ALIGN_BOTTOM_MID)
        .show(false)
        .onEvent(s_onKeyboardEvent, this);

    // Refresh last-seen labels from Core 0 (LVGL task) every 30 s.
    // updateContactLastSeen() only writes the data (Core 1, atomic), this
    // timer does the actual lv_label_set_text() safely on Core 0.
    lv_timer_create([](lv_timer_t* t) {
        UIManager* self = (UIManager*) t->user_data;
        self->refreshLastSeenLabels();
    }, 30000, this);
}

void UIManager::setNightMode(bool night) {
    if (!ui_DimOverlay) return;
    if (night == night_mode_active) return;   // debounce — avoids LVGL redraw every second
    night_mode_active = night;
    if (night) {
        lv_obj_set_style_bg_color(ui_DimOverlay, lv_color_black(), 0);  // explicit black
        lv_obj_set_style_bg_opa(ui_DimOverlay, 192, 0);   // 75% dark
    } else {
        lv_obj_set_style_bg_opa(ui_DimOverlay, 0, 0);     // invisible
    }
}

