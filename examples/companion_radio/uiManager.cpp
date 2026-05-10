#include <Arduino.h>

#include "esp_log.h"
#include "uiDefines.h"
#include "uiVars.h"

#include "uiManager.h"

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

void UIManager::addChatBubble(const char *time_str, const char *sender, const char *msg,bool is_self)
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

    // IMPORTANT: vertical layout inside bubble
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bubble,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
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
        LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_START, //LV_FLEX_ALIGN_CENTER, // Ευθυγράμμιση ονόματος/ώρας στον κάθετο άξονα
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

    lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_msg, &lv_font_arial_26, 0);

    // Spacing between header and text
    //lv_obj_set_style_margin_top(lbl_msg, 6, 0);
    lv_obj_set_style_pad_row(bubble, 6, 0);

    chat_items[chat_count++] = row;

    lv_obj_scroll_to_view(row, LV_ANIM_ON);
  }

void UIManager::addPrivateChatBubble(const char *time_str, const char *msg, bool is_self) {
    
  lv_obj_set_style_pad_bottom(ui_ContactMessages, 20, 0);

  // 1. Row container
    lv_obj_t* row = LvObj(ui_ContactMessages)
    .width(lv_pct(100))
    .height(LV_SIZE_CONTENT)
    .bgOpa(0)
    .border(0)
    .padAll(4)
    .scrollable(false)
    .flexFlow(LV_FLEX_FLOW_ROW)
    .flexAlign(
        is_self ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );

  // 2. Aligner (Column) - label and time
    lv_obj_t* aligner = LvObj(row)
        .width(LV_SIZE_CONTENT)
        .height(LV_SIZE_CONTENT)
        .bgOpa(0)
        .border(0)
        .padAll(0)
        .scrollable(false)
        .flexFlow(LV_FLEX_FLOW_ROW)
        .flexAlign(
            is_self ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, 
            LV_FLEX_ALIGN_START, 
            LV_FLEX_ALIGN_START
        );
    lv_obj_set_style_pad_row(aligner, 4, 0);

  // 3. Το Label-Bubble (Ενοποιημένο)
  lv_obj_t *lbl_msg = lv_label_create(aligner);

  // Long mode για wrap
  lv_label_set_long_mode(lbl_msg, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_msg, msg);
  lv_obj_set_style_text_font(lbl_msg, &lv_font_arial_22, 0);

  // Fixed max width για wrap
  lv_obj_set_width(lbl_msg, 400);           // max πλάτος
  lv_obj_set_height(lbl_msg, LV_SIZE_CONTENT);  // αυτόματο ύψος

  // Bubble style
  lv_obj_set_style_bg_opa(lbl_msg, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(lbl_msg, 12, 0);
  lv_obj_set_style_pad_all(lbl_msg, 12, 0);

  if(is_self) {
      lv_obj_set_style_bg_color(lbl_msg, lv_color_hex(0x1E88E5), 0);
      lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0xFFFFFF), 0);
  } else {
      lv_obj_set_style_bg_color(lbl_msg, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0x000000), 0);
  }

  // 4. Η Ώρα
  lv_obj_t *lbl_time = lv_label_create(aligner);
  lv_label_set_text(lbl_time, time_str);
  lv_obj_set_style_text_color(lbl_time, lv_color_hex(0x808080), 0);
  lv_obj_set_style_text_font(lbl_time, &lv_font_arial_14, 0);

  lv_obj_scroll_to_view(row, LV_ANIM_ON);
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
    ContactInfo *c = (ContactInfo*) lv_obj_get_user_data(row);

    Serial.printf("Clicked: %s\n", c->name);
}

void UIManager::addContactToUI(ContactInfo c)
{
    const int ROW_W  = 200;
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

    // Store ContactInfo
    auto* store = new ContactInfo(c);
    lv_obj_set_user_data(btn, store);

    lv_obj_add_event_cb(btn, onContactClick, LV_EVENT_CLICKED, this);

    // ============================
    // Avatar container (hidden when SHOW_CONTACT_AVATAR is false)
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

    LvLabel(text_col)
        .text(lastSeen)
        .position(0, PAD + 32)
        .width(text_w)
        .font(&lv_font_arial_16)
        .textColor(0x888888)
        .wrap(false);

    // ============================
    // Disable child clicks
    // ============================
#ifdef SHOW_CONTACT_AVATAR
    LvObj(avatar, true).clickable(false);
#endif
    LvObj(text_col, true).clickable(false);
}

void UIManager::onShowKeyboard()
{
    LvKeyboard(ui_Keyboard, true).show(true);
    LvObj(ui_DimOverlay, true).clickable(true);
    LvObj(ui_ChannelInput, true).positionY(channelInputBaseKeybOnY);
    LvObj(ui_SendBtn, true).positionY(channelInputBaseKeybOnY);
}

void UIManager::onHideKeyboard()
{
    LvKeyboard(ui_Keyboard, true).show(false);
    LvObj(ui_DimOverlay, true).clickable(false);
    LvObj(ui_ChannelInput, true).positionY(channelInputBaseY);
    LvObj(ui_SendBtn, true).positionY(channelInputBaseY);
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

    lv_keyboard_set_textarea(ui_Keyboard, ta);
    onShowKeyboard();
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
    //handleCommand(fullMessage);
    handleCommand(msgCopy);

    char time_buf[16];
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    sprintf(time_buf, "%02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
    addChatBubble(time_buf, "Me", msgCopy, true);

    onHideKeyboard();
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

        LvObj(ui_ChannelInput, true).positionY(channelInputBaseY);
        LvObj(ui_SendBtn, true).positionY(channelInputBaseY);

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

    LvObj(ui_TabPageHome)
        .scrollable(false)
        .bgOpa(0)
        .bgColor(0x000000);
    LvObj(ui_TabPageContacts)
        .scrollable(false)
        .bgOpa(0)
        .bgColor(0x000000);
    LvObj(ui_TabPageChannels)
        .scrollable(false)
        .bgOpa(0)
        .bgColor(0x000000);
    LvObj(ui_TabPageSettings)
        .scrollable(false)
        .bgOpa(0)
        .bgColor(0x000000);

    ui_ValueDate = LvLabel(ui_TabPageHome)
        .text("--- --/--/----")
        .width(LV_SIZE_CONTENT)
        .height(LV_SIZE_CONTENT)
        .font(&lv_font_arial_40)
        .textColor(0xFFFFFF)
        .opa(255)
        .align(LV_ALIGN_CENTER)
        .position(0, -165);


    ui_ValueTime = LvLabel(ui_TabPageHome)
        .text("--:--")
        .width(LV_SIZE_CONTENT)
        .height(LV_SIZE_CONTENT)
        .font(&lv_font_arial_48)
        .textColor(0xFFFFFF)
        .opa(255)
        .align(LV_ALIGN_CENTER)
        .position(0, -100);

    ui_Contacts = LvList(ui_TabPageContacts)
        .width(250)
        .height(400)
        .align(LV_ALIGN_CENTER)
        .position(-274, 0)
        .transparent()
        .raw();

    lv_obj_set_style_bg_opa(ui_Contacts, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_Contacts, 0, 0);
    lv_obj_set_style_outline_width(ui_Contacts, 0, 0);
    lv_obj_set_style_shadow_width(ui_Contacts, 0, 0);
    //lv_obj_set_scrollbar_mode(ui_Contacts, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui_Contacts, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_border_width(ui_Contacts, 0, LV_PART_ITEMS);

    ui_ContactMessages = LvList(ui_TabPageContacts)
        .width(500)
        .height(400)
        .align(LV_ALIGN_CENTER)
        .position(124, 0)
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
    
    // LvObj(ui_TabPageContacts)
    //     .size(2, 400)
    //     .position(222, 0)
    //     .bgColor(0x444444)
    //     .border(0)
    //     .scrollable(false)
    //     .radius(0);

    ui_Channels = LvDropdown(ui_TabPageChannels)
        .options("Public")
        .width(291)
        .align(LV_ALIGN_CENTER)
        .position(-243, -182)
        .clickable(true)
        .raw();

    ui_ChannelMessages = LvList(ui_TabPageChannels)
        .width(780)
        .height(280)
        .align(LV_ALIGN_CENTER)
        .transparent()
        .padRow(10)
        .position(0, 0)
        .bgColor(0)
        .bgOpa(0)
        .border(0)
        .noDecor()
        .raw();

    //lv_obj_set_scrollbar_mode(ui_ChannelMessages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui_ChannelMessages, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_border_width(ui_ChannelMessages, 0, LV_PART_ITEMS);

    ui_ChannelDivider = LvObj(ui_TabPageChannels)
        .size(780, 1)
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
    lv_obj_remove_style_all(ui_DimOverlay);             // no border/padding
    lv_obj_clear_flag(ui_DimOverlay, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(ui_DimOverlay, LV_OBJ_FLAG_SCROLL_CHAIN_VER);


    ui_ChannelInput = LvTextArea(ui_TabPageChannels)
        .size(670, 40)
        .align(LV_ALIGN_CENTER)
        .position(-50, channelInputBaseY)
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
        .position(350, channelInputBaseY)
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

    ui_Keyboard = LvKeyboard(lv_layer_top())
        .size(480, 200)
        .align(LV_ALIGN_BOTTOM_MID)
        .show(false)
        .onEvent(s_onKeyboardEvent, this);
}

void UIManager::setNightMode(bool night) {

    if (!ui_DimOverlay) return;
    if (night) {
        lv_obj_set_style_bg_opa(ui_DimOverlay, 192, 0);  // 75% dark
    } else {
        lv_obj_set_style_bg_opa(ui_DimOverlay, 0, 0);    // none
    }
}

