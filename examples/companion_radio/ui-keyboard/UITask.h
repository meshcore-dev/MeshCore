#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/MomentaryButton.h>
#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>
#include "../AbstractUITask.h"
#include <M5Cardputer.h>
#include <Preferences.h>

class UIScreen;

enum class MenuScreen {
    CONTACTS,
    CHANNELS,
    CHAT,
    SETTINGS
};

// Settings categories
enum class SettingsCategory {
    MAIN_MENU,      // Main settings menu with categories
    THEME,          // Brightness, Main Color, Secondary Color
    PUBLIC_INFO,    // Public user info
    RADIO_SETUP,    // Radio configuration
    OTHER,          // Other settings
    DEVICE_INFO,    // Device information
    RADIO_PRESET    // Radio preset selection
};

// Radio preset structure
struct RadioPreset {
    const char* name;
    float freq_mhz;
    uint8_t sf;
    float bw_khz;
    uint8_t cr;
};

// Radio presets list
static const RadioPreset RADIO_PRESETS[] = {
    {"New Zealand", 917.375f, 11, 250.0f, 5},
    {"New Zealand (Nrrw)", 917.375f, 7, 62.5f, 5},
    {"Portugal 868", 869.618f, 7, 62.5f, 6},
    {"Switzerland", 869.618f, 8, 62.5f, 8},
    {"USA / Canada (Rec)", 910.525f, 7, 62.5f, 5},
    {"Vietnam", 920.250f, 11, 250.0f, 5},
    {"Australia", 915.800f, 10, 250.0f, 5},
    {"Australia (Narrow)", 916.575f, 7, 62.5f, 8},
    {"Australia: SA, WA", 923.125f, 8, 62.5f, 8},
    {"Australia: QLD", 923.125f, 8, 62.5f, 5},
    {"EU / UK (Narrow)", 869.618f, 8, 62.5f, 8},
    {"EU / UK (Long Rng)", 869.525f, 11, 250.0f, 5},
    {"EU / UK (Med Rng)", 869.525f, 10, 250.0f, 5},
    {"Czech Rep (Narrow)", 869.432f, 7, 62.5f, 5}
};
#define NUM_RADIO_PRESETS 14

// Message structure for chat history
struct ChatMessage {
    char text[128];
    char from_name[32];
    char contact_or_channel[32]; // Contact name or channel name this message belongs to
    bool is_outgoing;
    bool is_channel; // true if channel message, false if contact message
    uint32_t timestamp;
};

#define MAX_CHAT_MESSAGES 100 // Increased to store more messages

// Color definitions
struct ColorOption {
    const char* name;
    uint16_t rgb565;
};

// Available colors
static const ColorOption COLORS[] = {
    {"White", 0xFFFF},
    {"Black", 0x0000},
    {"Red", 0xF800},
    {"Green", 0x07E0},
    {"Blue", 0x001F},
    {"Yellow", 0xFFE0},
    {"Cyan", 0x07FF},
    {"Magenta", 0xF81F},
    {"Orange", 0xFD20},
    {"Pink", 0xFE19},
    {"Purple", 0x8010},
    {"Brown", 0x8200},
    {"Gray", 0x8410},
    {"Light Blue", 0x051D},
    {"Light Green", 0x07F0},
    {"Dark Blue", 0x0010},
    {"Dark Green", 0x0320},
    {"Dark Red", 0x7800}
};
#define NUM_COLORS 18

class UITask : public AbstractUITask {
private:
    DisplayDriver* _display;
    SensorManager* _sensors;
    NodePrefs* _node_prefs;
    MenuScreen _menu_state;
    
    unsigned long _next_refresh;
    unsigned long _auto_off;
    unsigned long _screen_timeout_millis;  // Current screen timeout in ms
    bool _screen_sleeping;  // True when in light sleep mode
    bool _ignore_next_keypress;  // True to ignore next keypress (used after wake)
    bool _need_refresh;
    char _alert[128];
    unsigned long _alert_expiry;
    
    // Notification system
    char _notification_from[32];
    char _notification_text[128];
    unsigned long _notification_expiry;
    bool _has_notification;
    
    // Keyboard input buffer
    char _input_buffer[256];
    int _input_length;
    bool _input_mode;
    
    // Search filter for contact/channel lists
    char _search_filter[32];
    int _search_filter_length;
    
    // Backspace hold tracking
    unsigned long _backspace_hold_start;
    bool _backspace_was_held;
    unsigned long _last_backspace_delete; // For fast repeat deletion
    
    // Deletion tracking (to prevent repeated deletions on key hold)
    bool _delete_processed;
    
    // Settings icon selection
    bool _settings_selected;
    SettingsCategory _settings_category; // Current settings category
    int _settings_menu_idx; // 0 = Save, 1 = Back
    int _settings_item_idx; // Selected setting item (-1 = bottom menu, 0+ = items in category)
    int _settings_scroll_pos; // Scroll position for settings categories
    int _public_info_scroll_pos; // Scroll position for Public Info options
    int _radio_preset_scroll_pos; // Scroll position for Radio Preset list
    int _radio_setup_scroll_pos; // Scroll position for Radio Setup list
    
    // Public Info editing state
    bool _editing_name;
    bool _show_qr_code;
    char _edit_buffer[64]; // For editing name/radio params
    int _edit_buffer_length;
    
    // Radio parameter editing state
    bool _editing_frequency;
    bool _editing_bandwidth;
    bool _editing_spreading_factor;
    bool _editing_coding_rate;
    bool _editing_tx_power;
    int _manual_setup_step; // 0=frequency, 1=bandwidth, 2=SF, 3=CR, 4=TX power, -1=not in manual setup
    
    // Factory reset confirmation
    bool _show_factory_reset_confirm;
    
    // Settings values
    uint8_t _brightness; // 0-255
    int _main_color_idx; // Index into COLORS array (foreground)
    int _secondary_color_idx; // Index into COLORS array (background)
    
    // List scrolling
    int _scroll_pos;
    int _selected_idx;
    
    // Currently selected contact/channel for chat
    ContactInfo _chat_contact;
    ChannelDetails _chat_channel;
    bool _chat_is_channel;
    
    // Chat message history
    ChatMessage _chat_history[MAX_CHAT_MESSAGES];
    int _chat_history_count;
    int _chat_scroll;
    int _chat_msg_scroll_index; // Index of first message to display (0 = newest)
    
    // Unread channels tracking
    bool _channel_has_unread[MAX_GROUP_CHANNELS];
    char _last_read_channel[32];
    
    void renderContactList();
    void renderChannelList();
    void renderChatScreen();
    void renderSettingsMenu();
    void renderBottomBar();
    void renderNotification();
    
    void handleKeyPress(Keyboard_Class::KeysState& status);
    void handleNavigation(Keyboard_Class::KeysState& status);
    void sendMessage();
    void loadSettings();
    void saveSettings();
    void applyTheme();
    void addMessageToHistory(const char* from_name, const char* text, bool is_outgoing, 
                            const char* contact_or_channel, bool is_channel);
    void filterDisplayText(const char* input, char* output, int max_len);

public:
    UITask(mesh::MainBoard* board, BaseSerialInterface* serial_interface);
    
    void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);
    void loop() override;
    void msgRead(int msgcount) override;
    void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
    void notify(UIEventType t = UIEventType::none) override;
    
    // Chat history synchronization for BLE
    void syncChatHistoryToBLE(int max_messages = 10);
    void enterLightSleep();  // Power management: light sleep mode
    
    void showAlert(const char* msg);
    void gotoScreen(MenuScreen screen);
    void requestRefresh() { _need_refresh = true; }  // Force screen refresh
    
    uint16_t getBattMilliVolts();
    bool isButtonPressed();
    
    SensorManager* getSensors() { return _sensors; }
    NodePrefs* getNodePrefs() { return _node_prefs; }
};
