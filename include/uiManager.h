#ifndef UI_MANAGER_h
#define UI_MANAGER_h

#include <Arduino.h>
#include "uiDefines.h"

#include "lvButton.h"
#include "lvDropDown.h"
#include "lvKeyboard.h"
#include "lvLabel.h"
#include "lvList.h"
#include "lvObj.h"
#include "lvTabView.h"
#include "lvTextArea.h"

#include <helpers/ContactInfo.h>

class UIManager {
  private:
    // functions 
    void format_datetime(char *buf, size_t size, const struct tm *timeinfo);
    void timestampToTime(time_t timestamp, char *buffer, size_t buffer_size);
    const char* convertDegreesToDirection(int degrees);
    int windSpeedToBeaufort(float speed);
    void getInitials(const char *name, char *out);
    void formatLastSeen(uint32_t ts, char *out, size_t len);
    void format_time(uint32_t ts, char *buf, size_t len);
    
    // Calendar days and months  
    static const char *days[7];
    static const char *months[12];

    // vars
    char time_str[9];
    String lastUpdate = "";
    char* tmp_buf;

    lv_obj_t *chat_items[MAX_CHAT_MESSAGES];
    int chat_count = 0;
    int channelInputBaseY = 185;
    int channelInputBaseKeybOnY = -15;
    void onShowKeyboard();    
    void onHideKeyboard();
    void ui_Screen1_screen_init(void);    

    lv_obj_t* ui_Screen1;
    lv_obj_t* ui_TabView1;
    lv_obj_t* ui_TabPageContacts;
    lv_obj_t* ui_Contacts;
    lv_obj_t* ui_ContactMessages;
    lv_obj_t* ui_TabPageChannels;
    lv_obj_t* ui_Channels;
    lv_obj_t* ui_ChannelMessages;
    lv_obj_t* ui_AutoLight;
    lv_obj_t* ui____initial_actions0;

    lv_obj_t* ui_DimOverlay;
    lv_obj_t* ui_TabPageHome;
    lv_obj_t* ui_ValueDate;
    lv_obj_t* ui_ValueTime;
    lv_obj_t* ui_TabPageSettings;
    lv_obj_t* ui_DayLight;
    lv_obj_t* ui_ChannelInput;
    lv_obj_t* ui_SendBtn;
    lv_obj_t* ui_Keyboard;
    lv_obj_t* iu_SendLabel;
    lv_obj_t* ui_ChannelDivider;

    lv_obj_t* ui_ContactName = nullptr;
    lv_obj_t* ui_ContactInput = nullptr;
    lv_obj_t* ui_ContactSendBtn = nullptr;
    lv_obj_t* ui_ContactSendLabel = nullptr;
    char currentContactName[32] = {0};
    uint8_t currentContactPubKey[32] = {0};
    bool hasCurrentContact = false;
    bool night_mode_active = false;
    char myNodeName[32] = {0};

    lv_obj_t* ui_ContactStatus = nullptr;

    lv_obj_t* ui_HomeNodeName = nullptr;
    lv_obj_t* ui_HomePubKey = nullptr;
    lv_obj_t* ui_HomeInfo = nullptr;
    lv_obj_t* ui_AdvertiseBtn = nullptr;

    lv_obj_t* ui_SettingsName = nullptr;
    lv_obj_t* ui_SettingsPreset = nullptr;
    lv_obj_t* ui_SettingsFreq = nullptr;
    lv_obj_t* ui_SettingsBw = nullptr;
    lv_obj_t* ui_SettingsSf = nullptr;
    lv_obj_t* ui_SettingsCr = nullptr;
    lv_obj_t* ui_SettingsTx = nullptr;
    lv_obj_t* ui_SettingsFw = nullptr;
    lv_obj_t* ui_SettingsSaveBtn = nullptr;
    lv_obj_t* ui_SettingsStatus = nullptr;

    // Whichever input/send button last got focus — used by keyboard show/hide
    lv_obj_t* activeInput = nullptr;
    lv_obj_t* activeSendBtn = nullptr;
    int activeInputBaseY = 0;

  public:
    UIManager();

    void onChannelInputFocus(lv_event_t* e);
    void onContactInputFocus(lv_event_t* e);
    void onContactSendClick(lv_event_t* e);
    void onSettingsInputFocus(lv_event_t* e);
    void onSettingsSaveClick(lv_event_t* e);
    void onAdvertiseClick(lv_event_t* e);
    void onDimOverlayClick(lv_event_t* e);
    void onSendClick(lv_event_t* e);
    void onKeyboardEvent(lv_event_t* e);
    void scroll_begin_event(lv_event_t* e);

    void onPresetChange(uint16_t idx);
    void populateSettings(const char* name, float freq, float bw, uint8_t sf, uint8_t cr,
                          uint8_t tx_power, const char* fw_ver, const char* build_date);
    void populateHome(const char* name, const char* pub_key_hex,
                      int contact_count, float freq);
    void setMyNodeName(const char* name);

    // Phase 6
    void routeIncomingDM(const uint8_t* from_pub_key, const char* from_name,
                         const char* time_str, const char* text);
    void setSendStatus(int state); // 0=sending, 1=delivered, 2=failed, -1=clear

    void updateDateTime(const struct tm timeinfo);
    void updateInfo(const char *str, uint32_t color);
    void clearDateTime();
    void updateValues();    
    void addPrivateChatBubble(const char *time_str, const char *msg, bool is_self, bool do_scroll = true);
    void addChatBubble(const char *time_str, const char *sender, const char *msg, bool is_self, bool do_scroll = true);
    void scrollPrivateChatToBottom();
    void scrollPublicChatToBottom();
    void beginPublicHistoryLoad();   // disable flex before bulk load
    void endPublicHistoryLoad();     // re-enable flex after bulk load
    void addContactToUI(ContactInfo c);
    void updateContactLastSeen(const uint8_t* pub_key, uint32_t lastmod);
    void refreshLastSeenLabels();
    void handleContactClick(lv_event_t *e);
    void setNightMode(bool night);
};

#endif