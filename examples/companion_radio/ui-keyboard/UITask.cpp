#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#include <M5Cardputer.h>
#include <qrcodedisplay.h>
#include <esp_sleep.h>  // For ESP32 light sleep functionality

// QRcode implementation for M5GFX
class QRcode_M5GFX : public QRcodeDisplay {
private:
    M5GFX* _display;
public:
    QRcode_M5GFX(M5GFX* display) : _display(display) {}
    
    void init() override {
        QRcodeDisplay::init();
        // Set multiply factor for QR code that fits on 240x135 screen
        multiply = 3;
        // Center the QR code on screen
        offsetsX = 55;
        offsetsY = 0;
    }
    
    void screenwhite() override {
        _display->fillScreen(0xFFFF); // White background
    }
    
    void screenupdate() override {
        // M5GFX updates immediately, no buffer to flush
    }
    
protected:
    void drawPixel(int x, int y, int color) override {
        uint16_t gfx_color = (color == 1) ? 0x0000 : 0xFFFF; // 1=black, 0=white
        // Draw a square of multiply x multiply pixels for each QR module
        _display->fillRect(x, y, multiply, multiply, gfx_color);
    }
};

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS 300000  // 5 minutes
#endif



extern MyMesh the_mesh;

UITask::UITask(mesh::MainBoard* board, BaseSerialInterface* serial_interface)
    : AbstractUITask(board, serial_interface), _display(nullptr),
      _menu_state(MenuScreen::CONTACTS), _next_refresh(0), _auto_off(0),
      _screen_timeout_millis(300000), _screen_sleeping(false), _ignore_next_keypress(false),
      _need_refresh(false), _alert_expiry(0), _input_length(0), _input_mode(false),
      _scroll_pos(0), _selected_idx(0), _chat_is_channel(false),
      _chat_history_count(0), _chat_scroll(0), _notification_expiry(0), _has_notification(false),
      _chat_msg_scroll_index(0), _search_filter_length(0), _backspace_hold_start(0), _backspace_was_held(false),
      _last_backspace_delete(0), _delete_processed(false),
      _settings_selected(false), _settings_category(SettingsCategory::MAIN_MENU), _settings_menu_idx(0), _settings_item_idx(0), _settings_scroll_pos(0), _public_info_scroll_pos(0), _radio_preset_scroll_pos(0), _radio_setup_scroll_pos(0),
      _editing_name(false), _show_qr_code(false), _edit_buffer_length(0),
      _editing_frequency(false), _editing_bandwidth(false), _editing_spreading_factor(false), _editing_coding_rate(false), _editing_tx_power(false), _manual_setup_step(-1),
      _show_factory_reset_confirm(false),
      _brightness(128), _main_color_idx(0), _secondary_color_idx(1) {
    _alert[0] = '\0';
    _input_buffer[0] = '\0';
    _search_filter[0] = '\0';
    _edit_buffer[0] = '\0';
    _notification_from[0] = '\0';
    _notification_text[0] = '\0';
    _last_read_channel[0] = '\0';
    memset(&_chat_contact, 0, sizeof(_chat_contact));
    memset(&_chat_channel, 0, sizeof(_chat_channel));
    memset(_chat_history, 0, sizeof(_chat_history));
    memset(_channel_has_unread, 0, sizeof(_channel_has_unread));
}

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
    _display = display;
    _sensors = sensors;
    _node_prefs = node_prefs;
    
    if (_display) {
        _display->turnOn();
    }
    
    // Load settings from Preferences (safe, doesn't affect mesh memory)
    loadSettings();
    
    // Restore screen timeout from NodePrefs
    if (_node_prefs) {
        // On first boot after update, screen_timeout_seconds will be 0
        // MyMesh.cpp now sets default to 300, but handle legacy case
        if (_node_prefs->screen_timeout_seconds == 0) {
            // This will be 0 either on first boot or if user selected "Never"
            // Since MyMesh sets default to 300, reaching 0 means user chose "Never"
            _screen_timeout_millis = 0; // Never timeout
        } else {
            _screen_timeout_millis = (unsigned long)_node_prefs->screen_timeout_seconds * 1000UL;
        }
        
        if (_node_prefs->screen_timeout_seconds == 0) {
            Serial.println("[Screen] Timeout: Never");
        } else {
            Serial.printf("[Screen] Timeout set to %u seconds\n", _node_prefs->screen_timeout_seconds);
        }
    }
    
    // Set initial auto-off time
    if (_screen_timeout_millis > 0) {
        _auto_off = millis() + _screen_timeout_millis;
    } else {
        _auto_off = 0; // Never timeout
    }
    
    // Restore GPS state from NodePrefs
#ifdef HAS_GPS
    if (_sensors && _node_prefs) {
        Serial.printf("[GPS] Restoring GPS state from NodePrefs: %s\n", 
                      _node_prefs->gps_enabled ? "ENABLED" : "DISABLED");
        
        // Set NodePrefs pointer in sensor manager for future syncing
        extern CardputerSensorManager sensors;
        sensors.setNodePrefs(_node_prefs);
        
        // Restore GPS state
        if (_node_prefs->gps_enabled) {
            _sensors->setSettingValue("gps", "1");
        }
    }
#endif
    
    _menu_state = MenuScreen::CONTACTS;
    _scroll_pos = 0;
    _selected_idx = 0;
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _need_refresh = true;
}

void UITask::loop() {
    M5Cardputer.update();
    
    // Handle keyboard input
    if (M5Cardputer.Keyboard.isChange()) {
        if (M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
            
            // If notification is active, any key dismisses it
            if (_has_notification) {
                _has_notification = false;
                _need_refresh = true;
            } else {
                // Check if we need to wake from sleep first
                bool was_sleeping = _screen_sleeping;
                bool was_off = (_display && !_display->isOn());
                
                // Wake from sleep if sleeping
                if (_screen_sleeping) {
                    _screen_sleeping = false;
                    _ignore_next_keypress = true;  // Ignore this wake keypress
                    Serial.println("[Sleep] Waking from light sleep (key press)");
                }
                
                // Turn on display if off
                if (_display && !_display->isOn()) {
                    _display->turnOn();
                    _ignore_next_keypress = true;  // Ignore this wake keypress
                    Serial.println("[Screen] Display turned on (key press)");
                }
                
                // Only handle keypress if we weren't sleeping/off
                if (!_ignore_next_keypress) {
                    handleKeyPress(status);
                } else {
                    // This was the wake keypress, ignore it
                    _ignore_next_keypress = false;
                    Serial.println("[Input] Ignoring wake keypress");
                }
            }
            _need_refresh = true;
            
            // Reset auto-off timer (using configured timeout)
            if (_screen_timeout_millis > 0) {
                _auto_off = millis() + _screen_timeout_millis;
            } else {
                _auto_off = 0; // Never timeout
            }
        }
    }
    
    // Check for backspace hold (continuous checking while key is pressed)
    if (_backspace_hold_start > 0) {
        // Check if ANY key is still pressed - if not, stop deletion
        if (!M5Cardputer.Keyboard.isPressed()) {
            // Key released - stop deletion immediately
            _backspace_hold_start = 0;
            _backspace_was_held = false;
            _last_backspace_delete = 0;
        } else if (millis() - _backspace_hold_start > 500) {
            // Held for 500ms+ - start fast deletion (one char every 100ms)
            if (!_backspace_was_held) {
                _backspace_was_held = true;
                _last_backspace_delete = millis();
            }
            
            // Delete one character every 100ms while holding
            if (millis() - _last_backspace_delete >= 100) {
                _last_backspace_delete = millis();
                
                if (_menu_state == MenuScreen::CHAT && _input_mode && _input_length > 0) {
                    _input_length--;
                    _input_buffer[_input_length] = '\0';
                    _need_refresh = true;
                } else if ((_menu_state == MenuScreen::CONTACTS || _menu_state == MenuScreen::CHANNELS) && _search_filter_length > 0) {
                    _search_filter_length--;
                    _search_filter[_search_filter_length] = '\0';
                    _scroll_pos = 0;
                    _selected_idx = 0;
                    _need_refresh = true;
                } else if (_menu_state == MenuScreen::SETTINGS && _editing_name && _edit_buffer_length > 0) {
                    _edit_buffer_length--;
                    _edit_buffer[_edit_buffer_length] = '\0';
                    _need_refresh = true;
                } else if (_menu_state == MenuScreen::SETTINGS && (_editing_frequency || _editing_bandwidth || _editing_spreading_factor || _editing_coding_rate || _editing_tx_power) && _edit_buffer_length > 0) {
                    _edit_buffer_length--;
                    _edit_buffer[_edit_buffer_length] = '\0';
                    _need_refresh = true;
                }
            }
        }
    }
    
    // Refresh display only when needed
    if (_display && _display->isOn() && _need_refresh) {
        _need_refresh = false;
        
        // Don't clear entire screen - reduces flicker!
        // Each UI element will draw its own background
        _display->startFrame();
        
        switch (_menu_state) {
            case MenuScreen::CONTACTS:
                renderContactList();
                break;
                
            case MenuScreen::CHANNELS:
                renderChannelList();
                break;
                
            case MenuScreen::CHAT:
                renderChatScreen();
                break;
                
            case MenuScreen::SETTINGS:
                renderSettingsMenu();
                break;
        }
        
        // Don't show bottom bar in Settings (it has its own)
        if (_menu_state != MenuScreen::SETTINGS) {
            renderBottomBar();
        }
        
        // Show notification popup if active
        if (_has_notification && millis() < _notification_expiry) {
            renderNotification();
        } else if (_has_notification && millis() >= _notification_expiry) {
            _has_notification = false;
        }
        
        _display->endFrame();
    }
    
    // Auto-off display for battery optimization
    if (_display && _display->isOn() && _screen_timeout_millis > 0 && _auto_off > 0 && millis() > _auto_off) {
        Serial.println("[Screen] Timeout - turning off display");
        _display->turnOff();
        _screen_sleeping = true;
        // Note: CPU stays active to receive LoRa packets and keyboard input
        // Light sleep was too deep and prevented proper operation
    }
    
    // Check for notification timeout
    if (_has_notification && millis() >= _notification_expiry) {
        _has_notification = false;
        _need_refresh = true;
    }
}

void UITask::renderContactList() {
    // Clear screen background (reduces flicker vs clearing every frame)
    _display->setColor(DisplayDriver::DARK);
    _display->fillRect(0, 0, 240, 135);
    
    // Header bar (0, 0, 240, 28)
    _display->setColor(DisplayDriver::LIGHT);
    _display->drawRect(0, 0, 240, 28);
    
    // Settings icon box (left)
    _display->drawRect(0, 0, 30, 28);
    
    // Draw hamburger menu icon (3 horizontal lines)
    uint16_t main_color = COLORS[_main_color_idx].rgb565;
    uint16_t secondary_color = COLORS[_secondary_color_idx].rgb565;
    
    if (_settings_selected) {
        // Selected: fill background with main color
        M5Cardputer.Display.fillRect(0, 0, 30, 28, main_color);
        // Draw 3 lines in secondary color
        M5Cardputer.Display.fillRect(6, 7, 18, 3, secondary_color);
        M5Cardputer.Display.fillRect(6, 13, 18, 3, secondary_color);
        M5Cardputer.Display.fillRect(6, 19, 18, 3, secondary_color);
    } else {
        // Normal: draw 3 lines in main color
        M5Cardputer.Display.fillRect(6, 7, 18, 3, main_color);
        M5Cardputer.Display.fillRect(6, 13, 18, 3, main_color);
        M5Cardputer.Display.fillRect(6, 19, 18, 3, main_color);
    }
    
    // MeshCore title (center)
    _display->setTextSize(2);
    _display->setCursor(73, 7);
    _display->print("MeshCore");
    
    // BLE PIN (right)
    uint32_t ble_pin = the_mesh.getBLEPin();
    if (ble_pin != 0 && ble_pin != 123456) {
        _display->setTextSize(1);
        char pin[16];
        sprintf(pin, "%lu", ble_pin);
        _display->setCursor(189, 11);
        _display->print(pin);
    }
    
    int num_contacts = the_mesh.getNumContacts();
    
    // Filter contacts by search term
    int filtered_indices[64];
    int filtered_count = 0;
    
    if (_search_filter_length > 0) {
        for (int i = 0; i < num_contacts; i++) {
            ContactInfo contact;
            if (the_mesh.getContactByIdx(i, contact)) {
                // Case-insensitive search
                char lower_name[32];
                char lower_filter[32];
                for (int j = 0; j < 32 && contact.name[j]; j++) {
                    lower_name[j] = tolower(contact.name[j]);
                    lower_name[j+1] = '\0';
                }
                for (int j = 0; j < 32 && _search_filter[j]; j++) {
                    lower_filter[j] = tolower(_search_filter[j]);
                    lower_filter[j+1] = '\0';
                }
                if (strstr(lower_name, lower_filter) != nullptr) {
                    filtered_indices[filtered_count++] = i;
                }
            }
        }
        num_contacts = filtered_count;
    } else {
        // No filter - show all
        for (int i = 0; i < num_contacts; i++) {
            filtered_indices[i] = i;
        }
        filtered_count = num_contacts;
    }
    
    if (num_contacts == 0) {
        _display->setTextSize(2);
        _display->setColor(DisplayDriver::LIGHT);
        const char* msg = "No contacts";
        int msg_width = _display->getTextWidth(msg);
        int msg_x = (240 - msg_width) / 2;
        _display->setCursor(msg_x, 60);
        _display->print(msg);
    } else {
        // Render 3 contact items (y: 27, 54, 81)
        int y_positions[3] = {27, 54, 81};
        
        for (int i = 0; i < 3; i++) {
            int contact_idx = _scroll_pos + i;
            if (contact_idx >= num_contacts) break;
            
            int real_idx = filtered_indices[contact_idx];
            ContactInfo contact;
            if (the_mesh.getContactByIdx(real_idx, contact)) {
                int y = y_positions[i];
                
                // Draw border
                _display->setColor(DisplayDriver::LIGHT);
                _display->drawRect(0, y, 240, 28);
                
                // Fill white if selected (don't select if _selected_idx == -1, which means hamburger is selected)
                if (contact_idx == _selected_idx && _selected_idx != -1) {
                    _display->fillRect(0, y, 240, 28);
                    _display->setColor(DisplayDriver::DARK);  // Black text
                    // Arrow indicator
                    _display->setCursor(2, y + 7);
                    _display->setTextSize(2);
                    _display->print(">");
                } else {
                    _display->setColor(DisplayDriver::LIGHT);  // White text
                }
                
                _display->setTextSize(2);
                _display->setCursor(16, y + 6);
                
                // Filter and truncate long names - max 18 chars to prevent wrapping
                char filtered_name[32];
                filterDisplayText(contact.name, filtered_name, sizeof(filtered_name));
                char display_name[19];
                strncpy(display_name, filtered_name, 18);
                display_name[18] = '\0';
                _display->print(display_name);
            }
        }
    }
}

void UITask::renderChannelList() {
    // Clear screen background (reduces flicker vs clearing every frame)
    _display->setColor(DisplayDriver::DARK);
    _display->fillRect(0, 0, 240, 135);
    
    // Header bar (0, 0, 240, 28)
    _display->setColor(DisplayDriver::LIGHT);
    _display->drawRect(0, 0, 240, 28);
    
    // Settings icon box (left)
    _display->drawRect(0, 0, 30, 28);
    
    // Draw hamburger menu icon (3 horizontal lines)
    uint16_t main_color = COLORS[_main_color_idx].rgb565;
    uint16_t secondary_color = COLORS[_secondary_color_idx].rgb565;
    
    if (_settings_selected) {
        // Selected: fill background with main color
        M5Cardputer.Display.fillRect(0, 0, 30, 28, main_color);
        // Draw 3 lines in secondary color
        M5Cardputer.Display.fillRect(6, 7, 18, 3, secondary_color);
        M5Cardputer.Display.fillRect(6, 13, 18, 3, secondary_color);
        M5Cardputer.Display.fillRect(6, 19, 18, 3, secondary_color);
    } else {
        // Normal: draw 3 lines in main color
        M5Cardputer.Display.fillRect(6, 7, 18, 3, main_color);
        M5Cardputer.Display.fillRect(6, 13, 18, 3, main_color);
        M5Cardputer.Display.fillRect(6, 19, 18, 3, main_color);
    }
    _display->setTextSize(2);
    _display->setCursor(73, 7);
    _display->print("MeshCore");
    
    // BLE PIN (right)
    uint32_t ble_pin = the_mesh.getBLEPin();
    if (ble_pin != 0 && ble_pin != 123456) {
        _display->setTextSize(1);
        char pin[16];
        sprintf(pin, "%lu", ble_pin);
        _display->setCursor(189, 11);
        _display->print(pin);
    }
    
    // Count and collect channels
    int num_channels = 0;
    ChannelDetails channels[MAX_GROUP_CHANNELS];
    int channel_mesh_idx[MAX_GROUP_CHANNELS]; // Track original mesh indices
    
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        if (the_mesh.getChannel(i, channels[num_channels]) && channels[num_channels].name[0] != '\0') {
            channel_mesh_idx[num_channels] = i;
            num_channels++;
        }
    }
    
    // Filter channels by search term
    int filtered_indices[MAX_GROUP_CHANNELS];
    int filtered_count = 0;
    
    if (_search_filter_length > 0) {
        for (int i = 0; i < num_channels; i++) {
            // Case-insensitive search
            char lower_name[32];
            char lower_filter[32];
            for (int j = 0; j < 32 && channels[i].name[j]; j++) {
                lower_name[j] = tolower(channels[i].name[j]);
                lower_name[j+1] = '\0';
            }
            for (int j = 0; j < 32 && _search_filter[j]; j++) {
                lower_filter[j] = tolower(_search_filter[j]);
                lower_filter[j+1] = '\0';
            }
            if (strstr(lower_name, lower_filter) != nullptr) {
                filtered_indices[filtered_count++] = i;
            }
        }
        num_channels = filtered_count;
    } else {
        // No filter - show all
        for (int i = 0; i < num_channels; i++) {
            filtered_indices[i] = i;
        }
        filtered_count = num_channels;
    }
    
    if (num_channels == 0) {
        _display->setTextSize(2);
        _display->setColor(DisplayDriver::LIGHT);
        const char* msg = "No channels";
        int msg_width = _display->getTextWidth(msg);
        int msg_x = (240 - msg_width) / 2;
        _display->setCursor(msg_x, 60);
        _display->print(msg);
    } else {
        // Render 3 channel items (y: 27, 54, 81)
        int y_positions[3] = {27, 54, 81};
        int visible_count = min(3, num_channels);
        
        for (int i = 0; i < visible_count; i++) {
            int channel_idx = _scroll_pos + i;
            if (channel_idx >= num_channels) break;
            
            int real_idx = filtered_indices[channel_idx];
            ChannelDetails& channel = channels[real_idx];
            int y = y_positions[i];
            
            // Draw border
            _display->setColor(DisplayDriver::LIGHT);
            _display->drawRect(0, y, 240, 28);
            
            // Fill white if selected (don't select if _selected_idx == -1, which means hamburger is selected)
            if (channel_idx == _selected_idx && _selected_idx != -1) {
                _display->fillRect(0, y, 240, 28);
                _display->setColor(DisplayDriver::DARK);  // Black text
                // Arrow indicator
                _display->setCursor(2, y + 7);
                _display->setTextSize(2);
                //_display->print("#");
            } else {
                _display->setColor(DisplayDriver::LIGHT);  // White text
            }
            
            _display->setTextSize(2);
            _display->setCursor(16, y + 6);
            
            // Filter and truncate long names - max 18 chars to prevent wrapping
            char filtered_name[32];
            filterDisplayText(channel.name, filtered_name, sizeof(filtered_name));
            char display_name[19];
            strncpy(display_name, filtered_name, 18);
            display_name[18] = '\0';
            _display->print(display_name);
            
            // Show unread indicator (dot) if channel has unread messages
            // Use the original mesh index for this channel
            int original_mesh_idx = channel_mesh_idx[real_idx];
            if (_channel_has_unread[original_mesh_idx]) {
                // Draw yellow dot indicator
                _display->setColor(DisplayDriver::YELLOW);
                _display->fillRect(220, y + 11, 6, 6);
            }
        }
    }
}

void UITask::renderChatScreen() {
    // Clear screen background (reduces flicker vs clearing every frame)
    _display->setColor(DisplayDriver::DARK);
    _display->fillRect(0, 0, 240, 135);
    
    // === HEADER BAR === (0, 0, 240, 28)
    _display->setColor(DisplayDriver::LIGHT);
    _display->drawRect(0, 0, 240, 28);
    
    // Back arrow
    _display->setTextSize(2);
    _display->setCursor(4, 7);
    _display->print("<");
    
    // Chat name - centered
    char filtered_name[32];
    char full_name[15];
    if (_chat_is_channel) {
        filterDisplayText(_chat_channel.name, filtered_name, sizeof(filtered_name));
        char name[13];
        strncpy(name, filtered_name, 12);
        name[12] = '\0';
        snprintf(full_name, 15, "%s", name);
    } else {
        filterDisplayText(_chat_contact.name, filtered_name, sizeof(filtered_name));
        char name[13];
        strncpy(name, filtered_name, 12);
        name[12] = '\0';
        snprintf(full_name, 15, "@%s", name);
    }
    
    int name_width = _display->getTextWidth(full_name);
    int center_x = (240 - name_width) / 2;
    _display->setCursor(center_x, 7);
    _display->print(full_name);
    
    // === SCROLLABLE MESSAGE AREA === (y=30 to y=106)
    // This area must be fully above the input bar
    int msg_area_top = 30;
    int msg_area_bottom = 106;
    int msg_area_height = msg_area_bottom - msg_area_top;
    
    if (_chat_history_count == 0) {
        // No messages - just show empty space
    } else {
        // Get current contact/channel name for filtering
        const char* current_name = _chat_is_channel ? _chat_channel.name : _chat_contact.name;
        
        // First, collect messages for this conversation
        int filtered_indices[MAX_CHAT_MESSAGES];
        int filtered_count = 0;
        for (int i = _chat_history_count - 1; i >= 0; i--) {
            ChatMessage& msg = _chat_history[i];
            if (msg.is_channel == _chat_is_channel && strcmp(msg.contact_or_channel, current_name) == 0) {
                filtered_indices[filtered_count++] = i;
            }
        }
        
        if (filtered_count == 0) {
            _display->setTextSize(1);
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(80, 60);
            _display->print("No messages");
        } else {
            // Clamp scroll index
            if (_chat_msg_scroll_index >= filtered_count) _chat_msg_scroll_index = filtered_count - 1;
            if (_chat_msg_scroll_index < 0) _chat_msg_scroll_index = 0;
            
            // Try to fit messages starting from scroll index
            // scroll_index=0 means show newest messages, scroll_index=N means skip N newest
            int messages_to_show[10]; // Max 10 messages on screen
            int show_count = 0;
            int available_height = msg_area_height; // Full 76px available
            
            // Start from scroll_index and go toward older messages (higher indices)
            for (int idx = _chat_msg_scroll_index; idx < filtered_count && show_count < 10; idx++) {
                int hist_idx = filtered_indices[idx];
                ChatMessage& msg = _chat_history[hist_idx];
                
                // Calculate message height
                char filtered_text[128];
                filterDisplayText(msg.text, filtered_text, sizeof(filtered_text));
                
                // Extract sender name for channel messages
                const char* message_text = filtered_text;
                bool has_name = false;
                char sender_name[32] = "";
                
                if (_chat_is_channel && !msg.is_outgoing) {
                    char* colon_pos = strchr(filtered_text, ':');
                    if (colon_pos != nullptr && (colon_pos - filtered_text) < 30) {
                        int name_len = colon_pos - filtered_text;
                        strncpy(sender_name, filtered_text, name_len);
                        sender_name[name_len] = '\0';
                        message_text = colon_pos + 1;
                        while (*message_text == ' ') message_text++;
                        has_name = true;
                    }
                }
                
                int text_len = strlen(message_text);
                int text_size = (text_len <= 48) ? 2 : 1;
                int chars_per_line = (text_size == 2) ? 16 : 35;
                int line_height = (text_size == 2) ? 16 : 9;
                int padding = (text_size == 2) ? 20 : 12;
                int num_lines = (text_len + chars_per_line - 1) / chars_per_line;
                int bubble_height = (num_lines * line_height) + padding;
                int name_height = has_name ? 10 : 0;
                int total_height = bubble_height + name_height + 2; // +2 for gap
                
                // Check if this message fits
                if (available_height - total_height < 0 && show_count > 0) {
                    // Doesn't fit, stop here (but allow first message even if too big)
                    break;
                }
                
                messages_to_show[show_count++] = hist_idx;
                available_height -= total_height;
            }
            
            // Now render the selected messages from bottom up
            int y = msg_area_bottom - 2;
            
            for (int i = 0; i < show_count; i++) {
                ChatMessage& msg = _chat_history[messages_to_show[i]];
                
                // Filter emojis and non-ASCII characters for display
                char filtered_text[128];
                filterDisplayText(msg.text, filtered_text, sizeof(filtered_text));
            
                // For INCOMING channel messages, extract sender name and message text
                char sender_name[32] = "";
                const char* message_text = filtered_text; // By default, use full text
                
                if (_chat_is_channel && !msg.is_outgoing) {
                    // Look for "Name: Message" pattern
                    char* colon_pos = strchr(filtered_text, ':');
                    if (colon_pos != nullptr && (colon_pos - filtered_text) < 30) {
                        // Extract sender name (before colon)
                        int name_len = colon_pos - filtered_text;
                        strncpy(sender_name, filtered_text, name_len);
                        sender_name[name_len] = '\0';
                        
                        // Point to message text (after colon and space)
                        message_text = colon_pos + 1;
                        while (*message_text == ' ') message_text++; // Skip spaces
                    }
                }
                
                // Calculate bubble dimensions with dynamic text sizing
                int text_len = strlen(message_text);
                
                // Use size 2 for short messages (<=48 chars), size 1 for longer ones
                int text_size;
                int chars_per_line;
                int line_height;
                int padding;
                
                if (text_len <= 48) {
                    text_size = 2;
                    chars_per_line = 16;
                    line_height = 16;
                    padding = 20; // 10px top + 10px bottom
                } else {
                    text_size = 1;
                    chars_per_line = 35;
                    line_height = 9;
                    padding = 12; // 6px top + 6px bottom
                }
                
                int num_lines = (text_len + chars_per_line - 1) / chars_per_line;
                int bubble_height = (num_lines * line_height) + padding;
                int bubble_width = 220;
                int bubble_x = 10;
                
                // Calculate starting position (from bottom up)
                int bubble_y = y - bubble_height;
                
                // For channel messages, add space for sender name above bubble
                int name_y = 0;
                bool has_name = (sender_name[0] != '\0');
                
                if (has_name) {
                    name_y = bubble_y - 10; // Name is 10px above bubble (9px text + 1px gap)
                }
                
                // Draw sender name above bubble if it exists (for channel messages)
                if (has_name) {
                    _display->setTextSize(1);
                    _display->setColor(DisplayDriver::LIGHT);
                    _display->setCursor(bubble_x, name_y);
                    _display->print(sender_name);
                }
                
                if (msg.is_outgoing) {
                    // === OUTGOING - WHITE FILLED ROUNDED BUBBLE ===
                    _display->setColor(DisplayDriver::LIGHT);
                    _display->fillRect(bubble_x, bubble_y, bubble_width, bubble_height);
                    
                    // Draw rounded corners with better effect
                    _display->drawRect(bubble_x, bubble_y, bubble_width, bubble_height);
                    _display->drawRect(bubble_x + 1, bubble_y + 1, bubble_width - 2, bubble_height - 2);
                    _display->drawRect(bubble_x + 2, bubble_y + 2, bubble_width - 4, bubble_height - 4);
                    
                    // Clear corner pixels for rounded effect
                    _display->setColor(DisplayDriver::DARK);
                    _display->fillRect(bubble_x, bubble_y, 3, 3); // Top-left
                    _display->fillRect(bubble_x + bubble_width - 3, bubble_y, 3, 3); // Top-right
                    _display->fillRect(bubble_x, bubble_y + bubble_height - 3, 3, 3); // Bottom-left
                    _display->fillRect(bubble_x + bubble_width - 3, bubble_y + bubble_height - 3, 3, 3); // Bottom-right
                    
                    // Black text with wrapping (dynamic size)
                    _display->setColor(DisplayDriver::DARK);
                    _display->setTextSize(text_size);
                    
                    int text_x = bubble_x + (text_size == 2 ? 10 : 6);
                    int text_y = bubble_y + (text_size == 2 ? 10 : 6);
                    
                    // Draw text with wrapping
                    for (int line = 0; line < num_lines; line++) {
                        int start = line * chars_per_line;
                        int len = min(chars_per_line, text_len - start);
                        if (len <= 0) break;
                        
                        char line_text[40];
                        strncpy(line_text, message_text + start, len);
                        line_text[len] = '\0';
                        
                        _display->setCursor(text_x, text_y + (line * line_height));
                        _display->print(line_text);
                    }
                } else {
                    // === INCOMING - WHITE OUTLINE ROUNDED BUBBLE ===
                    _display->setColor(DisplayDriver::LIGHT);
                    
                    // Draw rounded corners with better effect
                    _display->drawRect(bubble_x, bubble_y, bubble_width, bubble_height);
                    _display->drawRect(bubble_x + 1, bubble_y + 1, bubble_width - 2, bubble_height - 2);
                    _display->drawRect(bubble_x + 2, bubble_y + 2, bubble_width - 4, bubble_height - 4);
                    
                    // Clear corner pixels for rounded effect
                    _display->setColor(DisplayDriver::DARK);
                    _display->fillRect(bubble_x, bubble_y, 3, 3); // Top-left
                    _display->fillRect(bubble_x + bubble_width - 3, bubble_y, 3, 3); // Top-right
                    _display->fillRect(bubble_x, bubble_y + bubble_height - 3, 3, 3); // Bottom-left
                    _display->fillRect(bubble_x + bubble_width - 3, bubble_y + bubble_height - 3, 3, 3); // Bottom-right
                    
                    // White text with wrapping (dynamic size)
                    _display->setColor(DisplayDriver::LIGHT);
                    _display->setTextSize(text_size);
                    
                    int text_x = bubble_x + (text_size == 2 ? 10 : 6);
                    int text_y = bubble_y + (text_size == 2 ? 10 : 6);
                    
                    // Draw text with wrapping
                    for (int line = 0; line < num_lines; line++) {
                        int start = line * chars_per_line;
                        int len = min(chars_per_line, text_len - start);
                        if (len <= 0) break;
                        
                        char line_text[40];
                        strncpy(line_text, message_text + start, len);
                        line_text[len] = '\0';
                        
                        _display->setCursor(text_x, text_y + (line * line_height));
                        _display->print(line_text);
                    }
                }
                
                // Move up for next bubble (with 2px gap)
                if (has_name) {
                    y = name_y - 2;
                } else {
                    y = bubble_y - 2;
                }
            } // End of for loop iterating through messages_to_show
        }
    }
    
    // === FIXED INPUT BAR AT BOTTOM === (107-135)
    // This is ALWAYS at the bottom, messages never overlap it
    _display->setColor(DisplayDriver::LIGHT);
    _display->drawRect(0, 107, 240, 28);
    
    _display->setTextSize(2);
    _display->setColor(DisplayDriver::LIGHT);
    
    if (_input_mode) {
        // Show input text (max 150 chars enforced)
        _display->setCursor(6, 112);
        
        // Show last 15 chars that fit on screen (leave space for counter on right)
        char display_buf[20];
        int visible_chars = min(15, _input_length);
        int start = max(0, _input_length - 15);
        strncpy(display_buf, _input_buffer + start, visible_chars);
        display_buf[visible_chars] = '\0';
        
        _display->print(display_buf);
        
        // Blinking cursor
        if ((millis() / 350) % 2 == 0) {
            int cursor_x = 6 + _display->getTextWidth(display_buf);
            if (cursor_x < 180) { // Leave space for counter
                _display->fillRect(cursor_x, 112, 3, 14);
            }
        }
        
        // Character counter in bottom-right corner (small text)
        _display->setTextSize(1);
        char counter[12];
        snprintf(counter, 12, "%d/150", _input_length);
        int counter_width = _display->getTextWidth(counter);
        _display->setCursor(234 - counter_width, 122); // Right-aligned, lower position
        _display->print(counter);
        _display->setTextSize(2); // Restore size
    } else {
        // Placeholder
        _display->setCursor(6, 112);
        _display->print("_");
    }
}

void UITask::renderBottomBar() {
    int bar_y = 108;
    
    // Only show tabs in list views (but not when searching)
    if (_menu_state == MenuScreen::CHAT) {
        // Show back hint
        _display->setTextSize(1);
        _display->setColor(DisplayDriver::YELLOW);
        _display->setCursor(4, bar_y + 4);
        return;
    }
    
    // When searching, show input bar instead of tabs
    if (_search_filter_length > 0) {
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, 107, 240, 28);
        
        _display->setTextSize(2);
        _display->setCursor(4, 112);
        
        // Display search filter text
        char filtered_text[32];
        filterDisplayText(_search_filter, filtered_text, sizeof(filtered_text));
        _display->print(filtered_text);
        
        // Cursor
        int cursor_x = 4 + (_search_filter_length * 12);
        if (cursor_x < 235) {
            _display->fillRect(cursor_x, 112, 3, 14);
        }
        return;
    }
    
    // Draw tab bar border (0, 108, 240, 27)
    _display->setColor(DisplayDriver::LIGHT);
    _display->drawRect(0, bar_y, 240, 27);
    
    _display->setTextSize(2);
    
    // Contacts tab (left half: 0-120)
    if (_menu_state == MenuScreen::CONTACTS) {
        // Active - white fill, black text
        _display->setColor(DisplayDriver::LIGHT);
        _display->fillRect(0, bar_y, 120, 27);
        _display->setColor(DisplayDriver::DARK);
        _display->setCursor(13, bar_y + 7);
        _display->print("Contacts");
    } else {
        // Inactive - white text only
        _display->setColor(DisplayDriver::LIGHT);
        _display->setCursor(13, bar_y + 7);
        _display->print("Contacts");
    }
    
    // Channels tab (right half: 120-240)
    if (_menu_state == MenuScreen::CHANNELS) {
        // Active - white fill, black text
        _display->setColor(DisplayDriver::LIGHT);
        _display->fillRect(120, bar_y, 120, 27);
        _display->setColor(DisplayDriver::DARK);
        _display->setCursor(133, bar_y + 7);
        _display->print("Channels");
    } else {
        // Inactive - white text only
        _display->setColor(DisplayDriver::LIGHT);
        _display->setCursor(133, bar_y + 7);
        _display->print("Channels");
    }
}

void UITask::renderSettingsMenu() {
    // Clear screen background (reduces flicker vs clearing every frame)
    _display->setColor(DisplayDriver::DARK);
    _display->fillRect(0, 0, 240, 135);
    
    // Header bar (0, 0, 240, 28) - similar to main menu
    _display->setColor(DisplayDriver::LIGHT);
    _display->drawRect(0, 0, 240, 28);
    
    // Empty settings icon box (left)
    _display->drawRect(0, 0, 30, 28);
    
    // Settings title (center)
    _display->setTextSize(2);
    
    if (_settings_category == SettingsCategory::MAIN_MENU) {
        _display->setCursor(75, 7);
        _display->print("Settings");
        
        // Show categories list (max 3 visible at once, like contact/channel lists)
        const char* categories[] = {"Public Info", "Radio Setup", "Theme", "Other", "Device Info"};
        int num_categories = 5;
        
        // Render 3 category items (y: 27, 54, 81)
        int y_positions[3] = {27, 54, 81};
        
        for (int i = 0; i < 3; i++) {
            int category_idx = _settings_scroll_pos + i;
            if (category_idx >= num_categories) break;
            
            int y = y_positions[i];
            
            // Draw border
            _display->setColor(DisplayDriver::LIGHT);
            _display->drawRect(0, y, 240, 28);
            
            // Fill white if selected (only if _settings_item_idx matches and is not -1)
            if (category_idx == _settings_item_idx && _settings_item_idx != -1) {
                _display->fillRect(0, y, 240, 28);
                _display->setColor(DisplayDriver::DARK);  // Black text
                // Arrow indicator
                _display->setCursor(2, y + 7);
                _display->setTextSize(2);
                _display->print(">");
            } else {
                _display->setColor(DisplayDriver::LIGHT);  // White text
            }
            
            _display->setTextSize(2);
            _display->setCursor(16, y + 6);
            _display->print(categories[category_idx]);
        }
        
    } else if (_settings_category == SettingsCategory::THEME) {
        _display->setCursor(87, 7);
        _display->print("Theme");
        
        // Show theme settings
        _display->setTextSize(2);
        int y_start = 35;
        int line_height = 18;
        
        // Brightness setting
        _display->setColor(DisplayDriver::LIGHT);
        if (_settings_item_idx == 0 && _settings_menu_idx != 1) {
            _display->fillRect(0, y_start - 2, 240, line_height);
            _display->setColor(DisplayDriver::DARK);
        }
        _display->setCursor(10, y_start);
        _display->print("Brightness: ");
        if (_settings_item_idx == 0 && _settings_menu_idx != 1) {
            _display->print("< ");
        }
        char brightness_str[8];
        sprintf(brightness_str, "%d%%", (_brightness * 100) / 255);
        _display->print(brightness_str);
        if (_settings_item_idx == 0 && _settings_menu_idx != 1) {
            _display->print(" >");
        }
        
        // Main Color setting
        y_start += line_height + 5;
        _display->setColor(DisplayDriver::LIGHT);
        if (_settings_item_idx == 1 && _settings_menu_idx != 1) {
            _display->fillRect(0, y_start - 2, 240, line_height);
            _display->setColor(DisplayDriver::DARK);
        }
        _display->setCursor(10, y_start);
        _display->print("Main: ");
        if (_settings_item_idx == 1 && _settings_menu_idx != 1) {
            _display->print("< ");
        }
        _display->print(COLORS[_main_color_idx].name);
        if (_settings_item_idx == 1 && _settings_menu_idx != 1) {
            _display->print(" >");
        }
        
        // Secondary Color setting
        y_start += line_height + 5;
        _display->setColor(DisplayDriver::LIGHT);
        if (_settings_item_idx == 2 && _settings_menu_idx != 1) {
            _display->fillRect(0, y_start - 2, 240, line_height);
            _display->setColor(DisplayDriver::DARK);
        }
        _display->setCursor(10, y_start);
        _display->print("Secondary: ");
        if (_settings_item_idx == 2 && _settings_menu_idx != 1) {
            _display->print("< ");
        }
        _display->print(COLORS[_secondary_color_idx].name);
        if (_settings_item_idx == 2 && _settings_menu_idx != 1) {
            _display->print(" >");
        }
        
    } else if (_settings_category == SettingsCategory::PUBLIC_INFO) {
        // Skip rendering menu if we're in edit mode (prevents flicker)
        if (_editing_name || _show_qr_code) {
            // Edit overlay will be rendered below
        } else {
            _display->setCursor(67, 7);
            _display->print("Public Info");
            
            // Show Public Info options (3 options, 3 visible with scrolling)
            const char* options[] = {"Change name", "Share key", "Share Position"};
            int num_options = 3;
        
        // Render 3 option items (y: 27, 54, 81)
        int y_positions[3] = {27, 54, 81};
        
        for (int i = 0; i < 3; i++) {
            int option_idx = _public_info_scroll_pos + i;
            if (option_idx >= num_options) break;
            
            int y = y_positions[i];
            
            // Draw border
            _display->setColor(DisplayDriver::LIGHT);
            _display->drawRect(0, y, 240, 28);
            
            // Fill white if selected (but not when Back button is selected)
            if (option_idx == _settings_item_idx && _settings_menu_idx != 1) {
                _display->fillRect(0, y, 240, 28);
                _display->setColor(DisplayDriver::DARK);  // Black text
                // Arrow indicator
                _display->setCursor(2, y + 7);
                _display->setTextSize(2);
                _display->print(">");
            } else {
                _display->setColor(DisplayDriver::LIGHT);  // White text
            }
            
            _display->setTextSize(2);
            _display->setCursor(16, y + 6);
            _display->print(options[option_idx]);
            
            // Show checkbox for "Share Position" (now at index 2)
            if (option_idx == 2) {
                int checkbox_x = 200;
                int checkbox_y = y + 7;
                // Restore proper color for checkbox
                if (option_idx == _settings_item_idx) {
                    _display->setColor(DisplayDriver::DARK);
                } else {
                    _display->setColor(DisplayDriver::LIGHT);
                }
                // Draw checkbox outline
                _display->drawRect(checkbox_x, checkbox_y, 14, 14);
                // Fill if enabled
                if (_node_prefs && _node_prefs->advert_loc_policy == ADVERT_LOC_SHARE) {
                    _display->fillRect(checkbox_x + 2, checkbox_y + 2, 10, 10);
                }
            }
        }
        }
        
    } else if (_settings_category == SettingsCategory::RADIO_SETUP) {
        _display->setCursor(67, 7);
        _display->print("Radio Setup");
        
        // Show Radio Setup options with scrolling (up to 3 options)
#ifdef HAS_GPS
        const char* options[] = {"GPS", "Choose preset", "Manual setup"};
        int num_options = 3;
#else
        const char* options[] = {"Choose preset", "Manual setup"};
        int num_options = 2;
#endif
        
        // Constrain scroll position
        if (_radio_setup_scroll_pos > num_options - 3) {
            _radio_setup_scroll_pos = max(0, num_options - 3);
        }
        
        // Render option items (y: 27, 54, 81)
        int y_positions[3] = {27, 54, 81};
        
        for (int i = 0; i < 3; i++) {
            int option_idx = _radio_setup_scroll_pos + i;
            if (option_idx >= num_options) break;
            
            int y = y_positions[i];
            
            // Draw border
            _display->setColor(DisplayDriver::LIGHT);
            _display->drawRect(0, y, 240, 28);
            
            // Fill white if selected (but not when Back button is selected)
            if (option_idx == _settings_item_idx && _settings_menu_idx != 1) {
                _display->fillRect(0, y, 240, 28);
                _display->setColor(DisplayDriver::DARK);  // Black text
                // Arrow indicator
                _display->setCursor(2, y + 7);
                _display->setTextSize(2);
                _display->print(">");
            } else {
                _display->setColor(DisplayDriver::LIGHT);  // White text
            }
            
            _display->setTextSize(2);
            _display->setCursor(16, y + 6);
            _display->print(options[option_idx]);
            
            // Show checkbox for GPS option (index 0 if HAS_GPS)
#ifdef HAS_GPS
            if (option_idx == 0) {
#else
            if (false) { // No GPS, no checkbox
#endif
                int checkbox_x = 200;
                int checkbox_y = y + 7;
                // Restore proper color for checkbox
                if (option_idx == _settings_item_idx) {
                    _display->setColor(DisplayDriver::DARK);
                } else {
                    _display->setColor(DisplayDriver::LIGHT);
                }
                // Draw checkbox outline
                _display->drawRect(checkbox_x, checkbox_y, 14, 14);
                // Fill if enabled
                if (_node_prefs && _node_prefs->gps_enabled) {
                    _display->fillRect(checkbox_x + 2, checkbox_y + 2, 10, 10);
                }
            }
            
            // No current values shown for Manual setup option
        }
        
        // Scroll indicators
        _display->setTextSize(1);
        _display->setColor(DisplayDriver::LIGHT);
        if (_radio_setup_scroll_pos > 0) {
            // Up arrow
            _display->setCursor(117, 112);
            _display->print("^");
        }
        if (_radio_setup_scroll_pos < num_options - 3) {
            // Down arrow
            _display->setCursor(117, 120);
            _display->print("v");
        }
        
    } else if (_settings_category == SettingsCategory::OTHER) {
        _display->setCursor(94, 7);
        _display->print("Other");
        
        // Show Other options (3 options: Sleep timeout, Factory Reset, Support)
        const char* options[] = {"Sleep timeout", "Factory Reset", "Spark the project"};
        int num_options = 3;
        
        // Render 3 option items (y: 27, 54, 81)
        int y_positions[3] = {27, 54, 81};
        
        for (int i = 0; i < 3; i++) {
            int option_idx = i; // No scrolling needed for 3 items
            if (option_idx >= num_options) break;
            
            int y = y_positions[i];
            
            // Draw border
            _display->setColor(DisplayDriver::LIGHT);
            _display->drawRect(0, y, 240, 28);
            
            // Fill white if selected (but not when Back button is selected)
            if (option_idx == _settings_item_idx && _settings_menu_idx != 1) {
                _display->fillRect(0, y, 240, 28);
                _display->setColor(DisplayDriver::DARK);  // Black text
                // Arrow indicator
                _display->setCursor(2, y + 7);
                _display->setTextSize(2);
                _display->print(">");
            } else {
                // Special gold color for "Spark the project" option (index 2)
                if (option_idx == 2) {
                    M5Cardputer.Display.setTextColor(0xFEA0);  // Gold color (RGB: 255, 215, 0)
                } else {
                    _display->setColor(DisplayDriver::LIGHT);  // White text
                }
            }
            
            _display->setTextSize(2);
            _display->setCursor(16, y + 6);
            _display->print(options[option_idx]);
            
            // Show current value for Sleep timeout (option 0)
            if (option_idx == 0 && _node_prefs) {
                _display->setTextSize(2);
                int value_x = 190;
                int value_y = y + 6;
                
                // Restore proper color (but not when Back is selected)
                if (option_idx == _settings_item_idx && _settings_menu_idx != 1) {
                    _display->setColor(DisplayDriver::DARK);
                } else {
                    _display->setColor(DisplayDriver::LIGHT);
                }
                
                _display->setCursor(value_x, value_y);
                if (_node_prefs->screen_timeout_seconds == 0) {
                    _display->print("Never");
                } else if (_node_prefs->screen_timeout_seconds == 10) {
                    _display->print("10s");
                } else if (_node_prefs->screen_timeout_seconds == 30) {
                    _display->print("30s");
                } else if (_node_prefs->screen_timeout_seconds == 60) {
                    _display->print("1m");
                } else if (_node_prefs->screen_timeout_seconds == 120) {
                    _display->print("2m");
                } else if (_node_prefs->screen_timeout_seconds == 300) {
                    _display->print("5m");
                } else {
                    char timeout_str[10];
                    sprintf(timeout_str, "%us", _node_prefs->screen_timeout_seconds);
                    _display->print(timeout_str);
                }
            }
        }
        
    } else if (_settings_category == SettingsCategory::RADIO_PRESET) {
        _display->setCursor(50, 7);
        _display->print("Choose Preset");
        
        // Show preset list (3 visible items with scrolling)
        int y_positions[3] = {27, 54, 81};
        
        for (int i = 0; i < 3; i++) {
            int preset_idx = _radio_preset_scroll_pos + i;
            if (preset_idx >= NUM_RADIO_PRESETS) break;
            
            int y = y_positions[i];
            
            // Draw border
            _display->setColor(DisplayDriver::LIGHT);
            _display->drawRect(0, y, 240, 28);
            
            // Fill white if selected (but not when Back button is selected)
            if (preset_idx == _settings_item_idx && _settings_menu_idx != 1) {
                _display->fillRect(0, y, 240, 28);
                _display->setColor(DisplayDriver::DARK);
                _display->setCursor(2, y + 7);
                _display->setTextSize(2);
                _display->print(">");
            } else {
                _display->setColor(DisplayDriver::LIGHT);
            }
            
            // Display preset name
            _display->setTextSize(2);
            _display->setCursor(16, y + 6);
            _display->print(RADIO_PRESETS[preset_idx].name);
        }
        
    } else if (_settings_category == SettingsCategory::DEVICE_INFO) {
        _display->setCursor(67, 7);
        _display->print("Device Info");
        
        // Show device information
        _display->setTextSize(1);
        _display->setColor(DisplayDriver::LIGHT);
        int y = 30;
        int line_height = 12;
        
        // Device name
        if (_node_prefs) {
            _display->setCursor(5, y);
            _display->print("Name: ");
            _display->print(_node_prefs->node_name);
            y += line_height;
        }
        
        // Battery voltage and percentage
        uint16_t battery_mv = getBattMilliVolts();
        if (battery_mv > 0) {
            const int minMilliVolts = 3000;
            const int maxMilliVolts = 4200;
            int battery_percent = ((battery_mv - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
            if (battery_percent < 0) battery_percent = 0;
            if (battery_percent > 100) battery_percent = 100;
            
            _display->setCursor(5, y);
            _display->print("Battery: ");
            char battery_str[32];
            snprintf(battery_str, sizeof(battery_str), "%umV (%d%%)", battery_mv, battery_percent);
            _display->print(battery_str);
            y += line_height;
        }
        
        // GPS Position
        #ifdef HAS_GPS
        if (_node_prefs && _node_prefs->gps_enabled && _sensors) {
            double lat = _sensors->node_lat;
            double lon = _sensors->node_lon;
            
            if (lat != 0 || lon != 0) {
                _display->setCursor(5, y);
                _display->print("Lat: ");
                char lat_str[16];
                snprintf(lat_str, sizeof(lat_str), "%.6f", lat);
                _display->print(lat_str);
                y += line_height;
                
                _display->setCursor(5, y);
                _display->print("Lon: ");
                char lon_str[16];
                snprintf(lon_str, sizeof(lon_str), "%.6f", lon);
                _display->print(lon_str);
                y += line_height;
            } else {
                _display->setCursor(5, y);
                _display->print("GPS: No fix");
                y += line_height;
            }
        } else {
            _display->setCursor(5, y);
            _display->print("GPS: Disabled");
            y += line_height;
        }
        #endif
        
        // Radio settings
        if (_node_prefs) {
            _display->setCursor(5, y);
            _display->print("Freq: ");
            char freq_str[16];
            snprintf(freq_str, sizeof(freq_str), "%.3f", _node_prefs->freq);
            _display->print(freq_str);
            _display->print(" MHz");
            y += line_height;
            
            _display->setCursor(5, y);
            _display->print("SF: ");
            char radio_str[32];
            snprintf(radio_str, sizeof(radio_str), "%u  BW: %.1f kHz", _node_prefs->sf, _node_prefs->bw);
            _display->print(radio_str);
            y += line_height;
            
            _display->setCursor(5, y);
            _display->print("TX Power: ");
            char power_str[16];
            snprintf(power_str, sizeof(power_str), "%u dBm", _node_prefs->tx_power_dbm);
            _display->print(power_str);
            y += line_height;
        }
        
        // Uptime
        _display->setCursor(5, y);
        _display->print("Uptime: ");
        unsigned long uptime_sec = millis() / 1000;
        unsigned long hours = uptime_sec / 3600;
        unsigned long minutes = (uptime_sec % 3600) / 60;
        unsigned long seconds = uptime_sec % 60;
        char uptime_str[20];
        snprintf(uptime_str, sizeof(uptime_str), "%luh %lum %lus", hours, minutes, seconds);
        _display->print(uptime_str);
        
    } else {
        // Other categories (empty for now)
        const char* title = "";
        switch (_settings_category) {
            case SettingsCategory::PUBLIC_INFO: title = "Public Info"; break;
            case SettingsCategory::RADIO_SETUP: title = "Radio Setup"; break;
            case SettingsCategory::OTHER: title = "Other"; break;
            default: title = "Settings"; break;
        }
        
        int title_width = _display->getTextWidth(title);
        int title_x = (240 - title_width) / 2;
        _display->setCursor(title_x, 7);
        _display->print(title);
        
        // Empty message
        _display->setTextSize(1);
        _display->setColor(DisplayDriver::LIGHT);
        _display->setCursor(70, 60);
        _display->print("(Empty)");
    }
    
    // Special overlays for editing/QR
    if (_editing_name) {
        // Dark overlay
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(0, 0, 240, 135);
        
        // Header bar (0, 0, 240, 28)
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, 0, 240, 28);
        
        _display->setTextSize(2);
        const char* title = "Change name";
        int title_width = _display->getTextWidth(title);
        int title_x = (240 - title_width) / 2;
        _display->setCursor(title_x, 7);
        _display->print(title);
        
        // Input box (y: 40-70)
        _display->drawRect(10, 40, 220, 30);
        
        // Show input with cursor
        _display->setTextSize(2);
        _display->setCursor(15, 47);
        
        // Show edit buffer with scrolling for long text (max ~20 chars visible at size 2)
        char display_buf[32];
        int max_visible = 20; // Characters that fit in input box at size 2
        int visible_chars = min(max_visible, _edit_buffer_length);
        int start = max(0, _edit_buffer_length - max_visible);
        strncpy(display_buf, _edit_buffer + start, visible_chars);
        display_buf[visible_chars] = '\0';
        _display->print(display_buf);
        
        // Blinking cursor
        if ((millis() / 350) % 2 == 0) {
            int cursor_x = 15 + _display->getTextWidth(display_buf);
            if (cursor_x < 225) {
                _display->fillRect(cursor_x, 47, 3, 14);
            }
        }
        
        // Bottom bar with Save/Back
        int bar_y = 108;
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, bar_y, 240, 27);
        
        _display->setTextSize(2);
        
        // Save tab (left half: 0-120)
        if (_settings_menu_idx == 0) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(0, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        }
        
        // Back tab (right half: 120-240)
        if (_settings_menu_idx == 1) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(120, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        }
        
        return; // Skip normal bottom bar
    }
    
    // Editing overlays for radio parameters
    if (_editing_frequency) {
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(0, 0, 240, 135);
        
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, 0, 240, 28);
        
        _display->setTextSize(2);
        // Show progress if in manual setup mode
        char title[32];
        if (_manual_setup_step >= 0) {
            snprintf(title, sizeof(title), "1/5: Frequency");
        } else {
            snprintf(title, sizeof(title), "Frequency (MHz)");
        }
        int title_width = _display->getTextWidth(title);
        int title_x = (240 - title_width) / 2;
        _display->setCursor(title_x, 7);
        _display->print(title);
        
        _display->drawRect(10, 40, 220, 30);
        
        _display->setTextSize(2);
        _display->setCursor(15, 47);
        _display->print(_edit_buffer);
        
        if ((millis() / 350) % 2 == 0) {
            int cursor_x = 15 + _display->getTextWidth(_edit_buffer);
            if (cursor_x < 225) {
                _display->fillRect(cursor_x, 47, 3, 14);
            }
        }
        
        
        // Bottom bar with Save/Back
        int bar_y = 108;
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, bar_y, 240, 27);
        
        _display->setTextSize(2);
        
        if (_settings_menu_idx == 0) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(0, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        }
        
        if (_settings_menu_idx == 1) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(120, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        }
        
        return;
    }
    
    if (_editing_bandwidth) {
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(0, 0, 240, 135);
        
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, 0, 240, 28);
        
        _display->setTextSize(2);
        // Show progress if in manual setup mode
        char title[32];
        if (_manual_setup_step >= 0) {
            snprintf(title, sizeof(title), "2/5: Bandwidth");
        } else {
            snprintf(title, sizeof(title), "Bandwidth (kHz)");
        }
        int title_width = _display->getTextWidth(title);
        int title_x = (240 - title_width) / 2;
        _display->setCursor(title_x, 7);
        _display->print(title);
        
        _display->drawRect(10, 40, 220, 30);
        
        _display->setTextSize(2);
        _display->setCursor(15, 47);
        _display->print(_edit_buffer);
        
        if ((millis() / 350) % 2 == 0) {
            int cursor_x = 15 + _display->getTextWidth(_edit_buffer);
            if (cursor_x < 225) {
                _display->fillRect(cursor_x, 47, 3, 14);
            }
        }
        
        int bar_y = 108;
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, bar_y, 240, 27);
        
        _display->setTextSize(2);
        
        if (_settings_menu_idx == 0) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(0, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        }
        
        if (_settings_menu_idx == 1) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(120, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        }
        
        return;
    }
    
    if (_editing_spreading_factor) {
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(0, 0, 240, 135);
        
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, 0, 240, 28);
        
        _display->setTextSize(2);
        // Show progress if in manual setup mode
        char title[32];
        if (_manual_setup_step >= 0) {
            snprintf(title, sizeof(title), "3/5: SF");
        } else {
            snprintf(title, sizeof(title), "Spreading Factor");
        }
        int title_width = _display->getTextWidth(title);
        int title_x = (240 - title_width) / 2;
        _display->setCursor(title_x, 7);
        _display->print(title);
        
        _display->drawRect(10, 40, 220, 30);
        
        _display->setTextSize(2);
        _display->setCursor(15, 47);
        _display->print(_edit_buffer);
        
        if ((millis() / 350) % 2 == 0) {
            int cursor_x = 15 + _display->getTextWidth(_edit_buffer);
            if (cursor_x < 225) {
                _display->fillRect(cursor_x, 47, 3, 14);
            }
        }
        
    
        
        int bar_y = 108;
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, bar_y, 240, 27);
        
        _display->setTextSize(2);
        
        if (_settings_menu_idx == 0) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(0, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        }
        
        if (_settings_menu_idx == 1) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(120, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        }
        
        return;
    }
    
    if (_editing_coding_rate) {
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(0, 0, 240, 135);
        
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, 0, 240, 28);
        
        _display->setTextSize(2);
        // Show progress if in manual setup mode
        char title[32];
        if (_manual_setup_step >= 0) {
            snprintf(title, sizeof(title), "4/5: CR");
        } else {
            snprintf(title, sizeof(title), "Coding Rate");
        }
        int title_width = _display->getTextWidth(title);
        int title_x = (240 - title_width) / 2;
        _display->setCursor(title_x, 7);
        _display->print(title);
        
        _display->drawRect(10, 40, 220, 30);
        
        _display->setTextSize(2);
        _display->setCursor(15, 47);
        _display->print(_edit_buffer);
        
        if ((millis() / 350) % 2 == 0) {
            int cursor_x = 15 + _display->getTextWidth(_edit_buffer);
            if (cursor_x < 225) {
                _display->fillRect(cursor_x, 47, 3, 14);
            }
        }
        
    
        
        int bar_y = 108;
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, bar_y, 240, 27);
        
        _display->setTextSize(2);
        
        if (_settings_menu_idx == 0) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(0, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        }
        
        if (_settings_menu_idx == 1) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(120, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        }
        
        return;
    }
    
    if (_editing_tx_power) {
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(0, 0, 240, 135);
        
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, 0, 240, 28);
        
        _display->setTextSize(2);
        // Show progress if in manual setup mode
        char title[32];
        if (_manual_setup_step >= 0) {
            snprintf(title, sizeof(title), "5/5: TX Power");
        } else {
            snprintf(title, sizeof(title), "TX Power (dBm)");
        }
        int title_width = _display->getTextWidth(title);
        int title_x = (240 - title_width) / 2;
        _display->setCursor(title_x, 7);
        _display->print(title);
        
        _display->drawRect(10, 40, 220, 30);
        
        _display->setTextSize(2);
        _display->setCursor(15, 47);
        _display->print(_edit_buffer);
        
        if ((millis() / 350) % 2 == 0) {
            int cursor_x = 15 + _display->getTextWidth(_edit_buffer);
            if (cursor_x < 225) {
                _display->fillRect(cursor_x, 47, 3, 14);
            }
        }
        
        
        int bar_y = 108;
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, bar_y, 240, 27);
        
        _display->setTextSize(2);
        
        if (_settings_menu_idx == 0) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(0, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(35, bar_y + 7);
            _display->print("Save");
        }
        
        if (_settings_menu_idx == 1) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(120, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(155, bar_y + 7);
            _display->print("Back");
        }
        
        return;
    }
    
    // Factory Reset confirmation dialog
    if (_show_factory_reset_confirm) {
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(0, 0, 240, 135);
        
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, 0, 240, 28);
        
        _display->setTextSize(2);
        const char* title = "Factory Reset";
        int title_width = _display->getTextWidth(title);
        int title_x = (240 - title_width) / 2;
        _display->setCursor(title_x, 7);
        _display->print(title);
        
        // Warning message
        _display->setTextSize(1);
        _display->setCursor(15, 45);
        _display->print("All data will be deleted!");
        _display->setCursor(15, 60);
        _display->print("Device will restart.");
        _display->setCursor(15, 75);
        _display->print("Are you sure?");
        
        // Bottom bar with Yes/No
        int bar_y = 108;
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(0, bar_y, 240, 27);
        
        _display->setTextSize(2);
        
        // Yes tab (left half: 0-120)
        if (_settings_menu_idx == 0) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(0, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(40, bar_y + 7);
            _display->print("Yes");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(40, bar_y + 7);
            _display->print("Yes");
        }
        
        // No tab (right half: 120-240)
        if (_settings_menu_idx == 1) {
            _display->setColor(DisplayDriver::LIGHT);
            _display->fillRect(120, bar_y, 120, 27);
            _display->setColor(DisplayDriver::DARK);
            _display->setCursor(165, bar_y + 7);
            _display->print("No");
        } else {
            _display->setColor(DisplayDriver::LIGHT);
            _display->setCursor(165, bar_y + 7);
            _display->print("No");
        }
        
        return;
    }
    
    if (_show_qr_code) {
        // QR Code display
        // Set brightness to 9% for better camera readability
        M5Cardputer.Display.setBrightness(23);
        
        // Clear screen to black
        M5.Display.fillScreen(0x0000);
        
        char qr_data[256];
        
        // Check if we're in Other menu (Support the project)
        if (_settings_category == SettingsCategory::OTHER) {
            // Show Buy Me a Coffee QR code
            snprintf(qr_data, sizeof(qr_data), "https://buymeacoffee.com/stachu");
        } else {
            // Show device contact info in meshcore:// URI format (Public Info -> Share key)
            // Convert public key to hex string
            char pub_key_hex[65]; // 32 bytes * 2 + null terminator
            mesh::Utils::toHex(pub_key_hex, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
            pub_key_hex[64] = '\0';
            
            // Build meshcore:// URI with contact info
            const char* device_name = _node_prefs ? _node_prefs->node_name : "Device";
            snprintf(qr_data, sizeof(qr_data), 
                     "meshcore://contact/add?name=%s&public_key=%s&type=1",
                     device_name, pub_key_hex);
        }
        
        // Create QR code using custom M5GFX implementation
        QRcode_M5GFX qrcode(&M5.Display);
        qrcode.init();
        
        // Create and render QR code
        qrcode.create(String(qr_data));
        
        return; // Skip bottom bar
    }
    
    // Don't show bottom bar in Device Info (any key press returns to menu)
    if (_settings_category == SettingsCategory::DEVICE_INFO) {
        return; // Skip bottom bar
    }
    
    // Bottom bar with Save/Back options
    int bar_y = 108;
    _display->setColor(DisplayDriver::LIGHT);
    _display->drawRect(0, bar_y, 240, 27);
    
    _display->setTextSize(2);
    
    // Back button - full width for all categories
    // Highlight when:
    // - In MAIN_MENU when _settings_item_idx == -1
    // - In other categories when _settings_menu_idx == 1
    bool should_highlight = false;
    if (_settings_category == SettingsCategory::MAIN_MENU) {
        // Highlight when _settings_item_idx == -1 (Back is selected)
        should_highlight = (_settings_item_idx == -1);
    } else {
        // In other categories, highlight when _settings_menu_idx == 1
        should_highlight = (_settings_menu_idx == 1);
    }
    
    if (should_highlight) {
        _display->setColor(DisplayDriver::LIGHT);
        _display->fillRect(0, bar_y, 240, 27);
        _display->setColor(DisplayDriver::DARK);
        int back_width = _display->getTextWidth("Back");
        int back_x = (240 - back_width) / 2;
        _display->setCursor(back_x, bar_y + 7);
        _display->print("Back");
    } else {
        _display->setColor(DisplayDriver::LIGHT);
        int back_width = _display->getTextWidth("Back");
        int back_x = (240 - back_width) / 2;
        _display->setCursor(back_x, bar_y + 7);
        _display->print("Back");
    }
}

void UITask::renderNotification() {
    // Full screen notification - black background, white text
    _display->setColor(DisplayDriver::DARK);
    _display->fillRect(0, 0, 240, 135); // Fill entire screen with black
    
    // Filter emojis from notification text and sender name
    char filtered_from[32];
    char filtered_text[128];
    filterDisplayText(_notification_from, filtered_from, sizeof(filtered_from));
    filterDisplayText(_notification_text, filtered_text, sizeof(filtered_text));
    
    // Show sender name (size 2, white, centered)
    _display->setColor(DisplayDriver::LIGHT);
    _display->setTextSize(2);
    int from_width = _display->getTextWidth(filtered_from);
    int from_x = (240 - from_width) / 2;
    _display->setCursor(from_x, 30);
    _display->print(filtered_from);
    
    // Show message text (size 2, white, wrapped, centered)
    _display->setTextSize(2);
    int text_len = strlen(filtered_text);
    int chars_per_line = 19; // Size 2 text fits ~19 chars per line
    int y_offset = 55; // Start below sender name
    
    // Calculate number of lines
    int num_lines = (text_len + chars_per_line - 1) / chars_per_line;
    if (num_lines > 4) num_lines = 4; // Max 4 lines
    
    // Draw text lines
    for (int line = 0; line < num_lines; line++) {
        int start = line * chars_per_line;
        int len = min(chars_per_line, text_len - start);
        if (len <= 0) break;
        
        char line_text[20];
        strncpy(line_text, filtered_text + start, len);
        line_text[len] = '\0';
        
        int line_width = _display->getTextWidth(line_text);
        int line_x = (240 - line_width) / 2;
        _display->setCursor(line_x, y_offset + (line * 18));
        _display->print(line_text);
    }
    
    // Show dismiss hint at bottom (size 1)
    _display->setTextSize(1);
    const char* hint = "Press any key";
    int hint_width = _display->getTextWidth(hint);
    int hint_x = (240 - hint_width) / 2;
    _display->setCursor(hint_x, 120);
    _display->print(hint);
}
void UITask::handleKeyPress(Keyboard_Class::KeysState& status) {
    // In chat mode with input active
    if (_menu_state == MenuScreen::CHAT && _input_mode) {
        if (status.enter) {
            sendMessage();
            _input_mode = false;
            _input_buffer[0] = '\0';
            _input_length = 0;
        } else if (status.fn) {
            // Check for FN+` (escape)
            bool has_backtick = false;
            for (auto key : status.word) {
                if (key == '`') has_backtick = true;
            }
            if (has_backtick) {
                // If input is empty, go back to list immediately
                if (_input_length == 0) {
                    _menu_state = _chat_is_channel ? MenuScreen::CHANNELS : MenuScreen::CONTACTS;
                    _input_mode = false;
                    _chat_msg_scroll_index = 0;
                    _search_filter_length = 0;
                    _search_filter[0] = '\0';
                } else {
                    // Otherwise just exit input mode
                    _input_mode = false;
                    _input_buffer[0] = '\0';
                    _input_length = 0;
                }
                return;
            }
        } else if (status.opt) {
            // OPT button also acts as escape
            // If input is empty, go back to list immediately
            if (_input_length == 0) {
                _menu_state = _chat_is_channel ? MenuScreen::CHANNELS : MenuScreen::CONTACTS;
                _input_mode = false;
                _chat_msg_scroll_index = 0;
                _search_filter_length = 0;
                _search_filter[0] = '\0';
            } else {
                // Otherwise just exit input mode
                _input_mode = false;
                _input_buffer[0] = '\0';
                _input_length = 0;
            }
        } else if (status.del) {
            // Backspace - start hold timer and delete one char
            if (_backspace_hold_start == 0) {
                _backspace_hold_start = millis();
                _backspace_was_held = false;
            }
            if (_input_length > 0) {
                _input_length--;
                _input_buffer[_input_length] = '\0';
            }
        } else {
            // Reset backspace hold timer when other keys pressed
            _backspace_hold_start = 0;
            _backspace_was_held = false;
            
            if (status.space) {
                // Enforce 150 character limit
                if (_input_length < 150) {
                    _input_buffer[_input_length++] = ' ';
                    _input_buffer[_input_length] = '\0';
                }
            } else {
                // Add characters from word vector (with 150 char limit)
                for (auto key : status.word) {
                    if (_input_length < 150) {
                        _input_buffer[_input_length++] = key;
                        _input_buffer[_input_length] = '\0';
                    }
                }
            }
        }
    }
    
    // In chat mode but not typing - start typing on any key
    if (_menu_state == MenuScreen::CHAT && !_input_mode) {
        // Check for FN combinations first
        if (status.fn) {
            // FN+` = back to list
            bool has_backtick = false;
            bool has_semicolon = false;
            bool has_period = false;
            
            for (auto key : status.word) {
                if (key == '`') has_backtick = true;
                if (key == ';') has_semicolon = true;
                if (key == '.') has_period = true;
            }
            
            if (has_backtick) {
                _menu_state = _chat_is_channel ? MenuScreen::CHANNELS : MenuScreen::CONTACTS;
                _input_mode = false;
                _input_buffer[0] = '\0';
                _input_length = 0;
                _chat_msg_scroll_index = 0;
                _search_filter_length = 0;
                _search_filter[0] = '\0';
                return;
            } else if (has_semicolon) {
                // FN+; = scroll to show older messages (move view up)
                _chat_msg_scroll_index += 1;
                if (_chat_msg_scroll_index > _chat_history_count - 1) _chat_msg_scroll_index = _chat_history_count - 1;
                _need_refresh = true;
                return;
            } else if (has_period) {
                // FN+. = scroll to show newer messages (move view down)
                _chat_msg_scroll_index -= 1;
                if (_chat_msg_scroll_index < 0) _chat_msg_scroll_index = 0;
                _need_refresh = true;
                return;
            }
        }
        
        // OPT button = back to list
        if (status.opt) {
            _menu_state = _chat_is_channel ? MenuScreen::CHANNELS : MenuScreen::CONTACTS;
            _input_mode = false;
            _input_buffer[0] = '\0';
            _input_length = 0;
            _chat_msg_scroll_index = 0;
            _search_filter_length = 0;
            _search_filter[0] = '\0';
            return;
        }
        
        // Any other key starts input mode
        if (status.word.size() > 0 || status.space) {
            _input_mode = true;
            _input_buffer[0] = '\0';
            _input_length = 0;
            
            // Process the key that started input
            if (status.space) {
                _input_buffer[_input_length++] = ' ';
                _input_buffer[_input_length] = '\0';
            } else {
                for (auto key : status.word) {
                    if (_input_length < 255) {
                        _input_buffer[_input_length++] = key;
                        _input_buffer[_input_length] = '\0';
                    }
                }
            }
        }
        return;
    }
    
    // In Settings editing mode (name editing)
    if (_menu_state == MenuScreen::SETTINGS && _editing_name) {
        // Check for navigation keys first
        bool left = false, right = false, select = false;
        for (auto key : status.word) {
            if (key == ',') left = true;
            if (key == '/') right = true;
        }
        // Only Enter triggers select (not space - space adds a space character)
        if (status.enter) select = true;
        
        if (left || right) {
            // Toggle between Save (0) and Back (1)
            _settings_menu_idx = (_settings_menu_idx == 0) ? 1 : 0;
        } else if (select) {
            if (_settings_menu_idx == 0) {
                // Save name
                if (_node_prefs) {
                    strncpy(_node_prefs->node_name, _edit_buffer, 31);
                    _node_prefs->node_name[31] = '\0';
                    // Save NodePrefs to persistent storage
                    the_mesh.savePrefs();
                    _editing_name = false;
                    _edit_buffer[0] = '\0';
                    _edit_buffer_length = 0;
                }
            } else {
                // Back - cancel editing
                _editing_name = false;
                _edit_buffer[0] = '\0';
                _edit_buffer_length = 0;
            }
        } else if (status.fn) {
            // Check for FN+` (escape)
            bool has_backtick = false;
            for (auto key : status.word) {
                if (key == '`') has_backtick = true;
            }
            if (has_backtick) {
                // Cancel editing
                _editing_name = false;
                _edit_buffer[0] = '\0';
                _edit_buffer_length = 0;
            }
        } else if (status.opt) {
            // Cancel editing
            _editing_name = false;
            _edit_buffer[0] = '\0';
            _edit_buffer_length = 0;
        } else if (status.del) {
            // Backspace
            if (_backspace_hold_start == 0) {
                _backspace_hold_start = millis();
                _backspace_was_held = false;
            }
            if (_edit_buffer_length > 0) {
                _edit_buffer_length--;
                _edit_buffer[_edit_buffer_length] = '\0';
            }
        } else {
            // Reset backspace hold timer
            _backspace_hold_start = 0;
            _backspace_was_held = false;
            
            if (status.space) {
                // Only allow space in name editing
                if (_editing_name && _edit_buffer_length < 31) {
                    _edit_buffer[_edit_buffer_length++] = ' ';
                    _edit_buffer[_edit_buffer_length] = '\0';
                }
            } else {
                for (auto key : status.word) {
                    // Skip navigation keys
                    if (key != ',' && key != '/' && key != ';' && key != '.') {
                        if (_edit_buffer_length < 31) {
                            _edit_buffer[_edit_buffer_length++] = key;
                            _edit_buffer[_edit_buffer_length] = '\0';
                        }
                    }
                }
            }
        }
        return;
    }
    
    // In Settings editing radio parameters (frequency, bandwidth, SF, CR, TX power)
    if (_menu_state == MenuScreen::SETTINGS && (_editing_frequency || _editing_bandwidth || _editing_spreading_factor || _editing_coding_rate || _editing_tx_power)) {
        // Check for navigation keys
        bool left = false, right = false, select = false;
        for (auto key : status.word) {
            if (key == ',') left = true;
            if (key == '/') right = true;
        }
        if (status.enter) select = true;
        
        if (left || right) {
            // Toggle between Save (0) and Back (1)
            _settings_menu_idx = (_settings_menu_idx == 0) ? 1 : 0;
        } else if (select) {
            if (_settings_menu_idx == 0) {
                // Save parameter
                bool valid = false;
                bool restart_needed = false;
                
                if (_editing_frequency) {
                    float freq = atof(_edit_buffer);
                    if (freq >= 400.0f && freq <= 2500.0f) {
                        _node_prefs->freq = freq;
                        valid = true;
                        restart_needed = true;
                    } else {
                        strncpy(_notification_text, "Invalid frequency", 127);
                        _notification_text[127] = '\0';
                        _notification_expiry = millis() + 2000;
                        _has_notification = true;
                    }
                } else if (_editing_bandwidth) {
                    float bw = atof(_edit_buffer);
                    if (bw >= 7.8f && bw <= 500.0f) {
                        _node_prefs->bw = bw;
                        valid = true;
                        restart_needed = true;
                    } else {
                        strncpy(_notification_text, "Invalid bandwidth", 127);
                        _notification_text[127] = '\0';
                        _notification_expiry = millis() + 2000;
                        _has_notification = true;
                    }
                } else if (_editing_spreading_factor) {
                    int sf = atoi(_edit_buffer);
                    if (sf >= 5 && sf <= 12) {
                        _node_prefs->sf = (uint8_t)sf;
                        valid = true;
                        restart_needed = true;
                    } else {
                        strncpy(_notification_text, "Invalid SF", 127);
                        _notification_text[127] = '\0';
                        _notification_expiry = millis() + 2000;
                        _has_notification = true;
                    }
                } else if (_editing_coding_rate) {
                    int cr = atoi(_edit_buffer);
                    if (cr >= 5 && cr <= 8) {
                        _node_prefs->cr = (uint8_t)cr;
                        valid = true;
                        restart_needed = true;
                    } else {
                        strncpy(_notification_text, "Invalid CR", 127);
                        _notification_text[127] = '\0';
                        _notification_expiry = millis() + 2000;
                        _has_notification = true;
                    }
                } else if (_editing_tx_power) {
                    int power = atoi(_edit_buffer);
                    if (power >= 1 && power <= 22) {
                        _node_prefs->tx_power_dbm = (uint8_t)power;
                        valid = true;
                        restart_needed = true;
                    } else {
                        strncpy(_notification_text, "Invalid TX power", 127);
                        _notification_text[127] = '\0';
                        _notification_expiry = millis() + 2000;
                        _has_notification = true;
                    }
                }
                
                if (valid) {
                    // Check if we're in manual setup mode
                    if (_manual_setup_step >= 0) {
                        // Manual setup mode - proceed to next parameter
                        _manual_setup_step++;
                        
                        // Close current editor
                        _editing_frequency = false;
                        _editing_bandwidth = false;
                        _editing_spreading_factor = false;
                        _editing_coding_rate = false;
                        _editing_tx_power = false;
                        _edit_buffer[0] = '\0';
                        _edit_buffer_length = 0;
                        
                        // Open next parameter editor
                        if (_manual_setup_step == 1) {
                            // Bandwidth
                            _editing_bandwidth = true;
                            _edit_buffer_length = snprintf(_edit_buffer, sizeof(_edit_buffer), "%.1f", _node_prefs->bw);
                            _settings_menu_idx = 0;
                        } else if (_manual_setup_step == 2) {
                            // Spreading Factor
                            _editing_spreading_factor = true;
                            _edit_buffer_length = snprintf(_edit_buffer, sizeof(_edit_buffer), "%d", _node_prefs->sf);
                            _settings_menu_idx = 0;
                        } else if (_manual_setup_step == 3) {
                            // Coding Rate
                            _editing_coding_rate = true;
                            _edit_buffer_length = snprintf(_edit_buffer, sizeof(_edit_buffer), "%d", _node_prefs->cr);
                            _settings_menu_idx = 0;
                        } else if (_manual_setup_step == 4) {
                            // TX Power
                            _editing_tx_power = true;
                            _edit_buffer_length = snprintf(_edit_buffer, sizeof(_edit_buffer), "%d", _node_prefs->tx_power_dbm);
                            _settings_menu_idx = 0;
                        } else {
                            // Finished all parameters - save and restart
                            _manual_setup_step = -1;
                            the_mesh.savePrefs();
                            
                            strncpy(_notification_text, "Restarting", 127);
                            _notification_text[127] = '\0';
                            _notification_expiry = millis() + 2000;
                            _has_notification = true;
                            // Force render notification
                            _display->startFrame();
                            renderSettingsMenu();
                            renderNotification();
                            _display->endFrame();
                            delay(2000);
                            ESP.restart();
                        }
                    } else {
                        // Not in manual setup - save and restart immediately (shouldn't happen now)
                        the_mesh.savePrefs();
                        _editing_frequency = false;
                        _editing_bandwidth = false;
                        _editing_spreading_factor = false;
                        _editing_coding_rate = false;
                        _editing_tx_power = false;
                        _edit_buffer[0] = '\0';
                        _edit_buffer_length = 0;
                        
                        if (restart_needed) {
                            strncpy(_notification_text, "Restarting", 127);
                            _notification_text[127] = '\0';
                            _notification_expiry = millis() + 2000;
                            _has_notification = true;
                            // Force render notification
                            _display->startFrame();
                            renderSettingsMenu();
                            renderNotification();
                            _display->endFrame();
                            delay(2000);
                            ESP.restart();
                        }
                    }
                }
            } else {
                // Back - cancel editing and exit manual setup
                _editing_frequency = false;
                _editing_bandwidth = false;
                _editing_spreading_factor = false;
                _editing_coding_rate = false;
                _editing_tx_power = false;
                _edit_buffer[0] = '\0';
                _edit_buffer_length = 0;
                _manual_setup_step = -1;
            }
        } else if (status.fn) {
            // Check for FN+` (escape)
            bool has_backtick = false;
            for (auto key : status.word) {
                if (key == '`') has_backtick = true;
            }
            if (has_backtick) {
                // Cancel editing and exit manual setup
                _editing_frequency = false;
                _editing_bandwidth = false;
                _editing_spreading_factor = false;
                _editing_coding_rate = false;
                _editing_tx_power = false;
                _edit_buffer[0] = '\0';
                _edit_buffer_length = 0;
                _manual_setup_step = -1;
            }
        } else if (status.opt) {
            // Cancel editing and exit manual setup
            _editing_frequency = false;
            _editing_bandwidth = false;
            _editing_spreading_factor = false;
            _editing_coding_rate = false;
            _editing_tx_power = false;
            _edit_buffer[0] = '\0';
            _edit_buffer_length = 0;
            _manual_setup_step = -1;
        } else if (status.del) {
            // Backspace
            if (_backspace_hold_start == 0) {
                _backspace_hold_start = millis();
                _backspace_was_held = false;
            }
            if (_edit_buffer_length > 0) {
                _edit_buffer_length--;
                _edit_buffer[_edit_buffer_length] = '\0';
            }
        } else {
            // Reset backspace hold timer
            _backspace_hold_start = 0;
            _backspace_was_held = false;
            
            // Allow only digits and decimal point for frequency/bandwidth
            for (auto key : status.word) {
                // Skip navigation keys
                if (key != ',' && key != '/' && key != ';' && key != '.') {
                    bool allow_char = false;
                    
                    // Digits always allowed
                    if (key >= '0' && key <= '9') {
                        allow_char = true;
                    }
                    // Decimal point only for frequency and bandwidth
                    else if (key == '.' && (_editing_frequency || _editing_bandwidth)) {
                        // Check if there's already a decimal point
                        bool has_decimal = false;
                        for (int i = 0; i < _edit_buffer_length; i++) {
                            if (_edit_buffer[i] == '.') {
                                has_decimal = true;
                                break;
                            }
                        }
                        if (!has_decimal) {
                            allow_char = true;
                        }
                    }
                    
                    if (allow_char && _edit_buffer_length < 15) {
                        _edit_buffer[_edit_buffer_length++] = key;
                        _edit_buffer[_edit_buffer_length] = '\0';
                    }
                }
            }
        }
        return;
    }
    
    // In Settings QR code display
    if (_menu_state == MenuScreen::SETTINGS && _show_qr_code) {
        // Any key closes QR code
        _show_qr_code = false;
        // Restore brightness
        M5Cardputer.Display.setBrightness(_brightness);
        return;
    }
    
    // In Settings Factory Reset confirmation
    if (_menu_state == MenuScreen::SETTINGS && _show_factory_reset_confirm) {
        // Check for navigation keys
        bool left = false, right = false, select = false;
        for (auto key : status.word) {
            if (key == ',') left = true;
            if (key == '/') right = true;
        }
        if (status.enter) select = true;
        
        if (left || right) {
            // Toggle between Yes (0) and No (1)
            _settings_menu_idx = (_settings_menu_idx == 0) ? 1 : 0;
        } else if (select) {
            if (_settings_menu_idx == 0) {
                // Yes - perform factory reset
                _show_factory_reset_confirm = false;
                
                // Show notification
                strncpy(_notification_text, "Resetting...", 127);
                _notification_text[127] = '\0';
                _notification_expiry = millis() + 3000;
                _has_notification = true;
                
                // Force render notification
                _display->startFrame();
                renderSettingsMenu();
                renderNotification();
                _display->endFrame();
                delay(1000);
                
                // Delete all data - format filesystem
                Serial.println("[Factory Reset] Formatting filesystem...");
                
                // Format entire filesystem to factory defaults
                the_mesh.factoryReset();
                
                Serial.println("[Factory Reset] Complete. Restarting...");
                delay(1000);
                ESP.restart();
            } else {
                // No - cancel
                _show_factory_reset_confirm = false;
            }
        } else if (status.fn) {
            // Check for FN+` (escape)
            bool has_backtick = false;
            for (auto key : status.word) {
                if (key == '`') has_backtick = true;
            }
            if (has_backtick) {
                // Cancel
                _show_factory_reset_confirm = false;
            }
        } else if (status.opt) {
            // Cancel
            _show_factory_reset_confirm = false;
        }
        return;
    }
    
    // In SETTINGS menu - handle ESC to go back
    if (_menu_state == MenuScreen::SETTINGS) {
        // Check for FN+` (escape) or OPT button
        bool has_escape = false;
        if (status.fn) {
            for (auto key : status.word) {
                if (key == '`') has_escape = true;
            }
        }
        if (status.opt) has_escape = true;
        
        if (has_escape) {
            if (_settings_category == SettingsCategory::MAIN_MENU) {
                // Go back to contacts
                _menu_state = MenuScreen::CONTACTS;
                _settings_category = SettingsCategory::MAIN_MENU;
                _settings_item_idx = 0;
                _settings_scroll_pos = 0;
                _settings_menu_idx = 0;
            } else if (_settings_category == SettingsCategory::PUBLIC_INFO) {
                // Go back to main settings menu
                _settings_category = SettingsCategory::MAIN_MENU;
                _settings_item_idx = 0;
                _settings_scroll_pos = 0;
                _public_info_scroll_pos = 0;
                _settings_menu_idx = 0;
            } else if (_settings_category == SettingsCategory::THEME) {
                // Go back to main settings menu (don't save)
                loadSettings();
                _settings_category = SettingsCategory::MAIN_MENU;
                _settings_item_idx = 0;
                _settings_scroll_pos = 0;
                _settings_menu_idx = 0;
            } else if (_settings_category == SettingsCategory::RADIO_SETUP) {
                // Go back to main settings menu
                _settings_category = SettingsCategory::MAIN_MENU;
                _settings_item_idx = 0;
                _settings_scroll_pos = 0;
                _settings_menu_idx = 0;
            } else {
                // Other categories - go back to main settings menu
                _settings_category = SettingsCategory::MAIN_MENU;
                _settings_item_idx = 0;
                _settings_scroll_pos = 0;
                _settings_menu_idx = 0;
            }
            return;
        }
    }
    
    // In CONTACTS or CHANNELS menu - check for navigation first, then filter
    if (_menu_state == MenuScreen::CONTACTS || _menu_state == MenuScreen::CHANNELS) {
        // Check for FN+DEL combination first (delete contact/channel)
        // FN modifier is in status.fn, DEL key (BACKSPACE) is in status.del
        if (status.fn && status.del) {
            // Only process deletion once per key press (not continuously on hold)
            if (!_delete_processed) {
                _delete_processed = true;
                
                if (_menu_state == MenuScreen::CONTACTS) {
                // Get current selected contact (account for filtering)
                int num_contacts = the_mesh.getNumContacts();
                int contact_to_delete_idx = -1;
                
                // Build filtered list if search is active
                if (_search_filter_length > 0) {
                    int filtered_indices[64];
                    int filtered_count = 0;
                    for (int i = 0; i < num_contacts; i++) {
                        ContactInfo contact;
                        if (the_mesh.getContactByIdx(i, contact)) {
                            char lower_name[32];
                            char lower_filter[32];
                            for (int j = 0; j < 32 && contact.name[j]; j++) {
                                lower_name[j] = tolower(contact.name[j]);
                                lower_name[j+1] = '\0';
                            }
                            for (int j = 0; j < 32 && _search_filter[j]; j++) {
                                lower_filter[j] = tolower(_search_filter[j]);
                                lower_filter[j+1] = '\0';
                            }
                            if (strstr(lower_name, lower_filter) != nullptr) {
                                filtered_indices[filtered_count++] = i;
                            }
                        }
                    }
                    if (filtered_count > 0 && _selected_idx < filtered_count) {
                        contact_to_delete_idx = filtered_indices[_selected_idx];
                    }
                } else {
                    // No filter - use direct index
                    if (num_contacts > 0 && _selected_idx < num_contacts) {
                        contact_to_delete_idx = _selected_idx;
                    }
                }
                
                // Delete contact immediately
                if (contact_to_delete_idx >= 0) {
                    ContactInfo contact_to_del;
                    if (the_mesh.getContactByIdx(contact_to_delete_idx, contact_to_del)) {
                        the_mesh.removeContact(contact_to_del);
                        the_mesh.saveContacts();
                        
                        // Show notification
                        char notif_msg[64];
                        snprintf(notif_msg, sizeof(notif_msg), "Deleted: %s", contact_to_del.name);
                        strncpy(_notification_text, notif_msg, 127);
                        _notification_text[127] = '\0';
                        _notification_expiry = millis() + 1500;
                        _has_notification = true;
                        
                        // Adjust selection
                        num_contacts = the_mesh.getNumContacts();
                        if (_selected_idx >= num_contacts && num_contacts > 0) {
                            _selected_idx = num_contacts - 1;
                        }
                        if (_selected_idx < _scroll_pos) {
                            _scroll_pos = _selected_idx;
                        }
                    }
                }
                } else if (_menu_state == MenuScreen::CHANNELS) {
                    // Get current selected channel (account for filtering)
                    int num_channels = 0;
                    ChannelDetails channels[MAX_GROUP_CHANNELS];
                    int channel_mesh_idx[MAX_GROUP_CHANNELS];
                    
                    // Build channel list
                    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
                        if (the_mesh.getChannel(i, channels[num_channels]) && channels[num_channels].name[0] != '\0') {
                            channel_mesh_idx[num_channels] = i;
                            num_channels++;
                        }
                    }
                    
                    int channel_to_delete_idx = -1;
                    
                    // Build filtered list if search is active
                    if (_search_filter_length > 0) {
                        int filtered_indices[MAX_GROUP_CHANNELS];
                        int filtered_count = 0;
                        for (int i = 0; i < num_channels; i++) {
                            char lower_name[32];
                            char lower_filter[32];
                            for (int j = 0; j < 32 && channels[i].name[j]; j++) {
                                lower_name[j] = tolower(channels[i].name[j]);
                                lower_name[j+1] = '\0';
                            }
                            for (int j = 0; j < 32 && _search_filter[j]; j++) {
                                lower_filter[j] = tolower(_search_filter[j]);
                                lower_filter[j+1] = '\0';
                            }
                            if (strstr(lower_name, lower_filter) != nullptr) {
                                filtered_indices[filtered_count++] = i;
                            }
                        }
                        if (filtered_count > 0 && _selected_idx < filtered_count) {
                            channel_to_delete_idx = filtered_indices[_selected_idx];
                        }
                    } else {
                        // No filter - use direct index
                        if (num_channels > 0 && _selected_idx < num_channels) {
                            channel_to_delete_idx = _selected_idx;
                        }
                    }
                    
                    // Delete channel immediately
                    if (channel_to_delete_idx >= 0) {
                        ChannelDetails channel_to_del = channels[channel_to_delete_idx];
                        int mesh_idx = channel_mesh_idx[channel_to_delete_idx];
                        
                        // Clear the channel by setting empty name
                        ChannelDetails empty_channel;
                        empty_channel.name[0] = '\0';
                        memset(&empty_channel.channel, 0, sizeof(empty_channel.channel));
                        the_mesh.setChannel(mesh_idx, empty_channel);
                        the_mesh.saveChannels();
                        
                        // Show notification
                        char notif_msg[64];
                        snprintf(notif_msg, sizeof(notif_msg), "Deleted: %s", channel_to_del.name);
                        strncpy(_notification_text, notif_msg, 127);
                        _notification_text[127] = '\0';
                        _notification_expiry = millis() + 1500;
                        _has_notification = true;
                        
                        // Recount channels after deletion
                        num_channels = 0;
                        for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
                            ChannelDetails temp_ch;
                            if (the_mesh.getChannel(i, temp_ch) && temp_ch.name[0] != '\0') {
                                num_channels++;
                            }
                        }
                        
                        // Adjust selection
                        if (_selected_idx >= num_channels && num_channels > 0) {
                            _selected_idx = num_channels - 1;
                        }
                        if (_selected_idx < _scroll_pos) {
                            _scroll_pos = _selected_idx;
                        }
                    }
                }
            }
            return;
        } else {
            // Reset flag when keys are released
            _delete_processed = false;
        }
        
        // Check if this is navigation input (FN + keys or direct navigation keys)
        bool has_nav_keys = false;
        if (status.fn) {
            has_nav_keys = true; // FN pressed - this is navigation
        } else {
            // Check for direct navigation keys
            for (auto key : status.word) {
                if (key == ',' || key == '.' || key == '/' || key == ';') {
                    has_nav_keys = true;
                    break;
                }
            }
        }
        
        // If navigation keys detected, skip to navigation handling
        if (has_nav_keys) {
            // Fall through to handleNavigation
        } else if (status.del) {
            // Backspace - start hold timer and delete one char
            if (_backspace_hold_start == 0) {
                _backspace_hold_start = millis();
                _backspace_was_held = false;
            }
            if (_search_filter_length > 0) {
                _search_filter_length--;
                _search_filter[_search_filter_length] = '\0';
                _scroll_pos = 0;
                _selected_idx = 0;
            }
            return;
        } else {
            // Reset backspace hold timer when other keys pressed
            _backspace_hold_start = 0;
            _backspace_was_held = false;
            
            if (status.word.size() > 0) {
                // Add typed characters to filter (limit 30 chars)
                // Ignore navigation keys: , . / ;
                for (auto key : status.word) {
                    if (key != ',' && key != '.' && key != '/' && key != ';') {
                        if (_search_filter_length < 30) {
                            _search_filter[_search_filter_length++] = key;
                            _search_filter[_search_filter_length] = '\0';
                            _scroll_pos = 0;
                            _selected_idx = 0;
                        }
                    }
                }
                return;
            }
        }
    }
    
    // Navigation mode
    handleNavigation(status);
}

void UITask::handleNavigation(Keyboard_Class::KeysState& status) {
    bool up = false, down = false, left = false, right = false, select = false;
    
    for (auto key : status.word) {
        char c = key;
        if (c == ';') up = true;
        if (c == '.') down = true;
        if (c == ',') left = true;
        if (c == '/') right = true;
    }
    
    if (status.enter || status.space) select = true;
    
    switch (_menu_state) {
        case MenuScreen::CONTACTS: {
            int num_contacts = the_mesh.getNumContacts();
            
            // Build filtered list for navigation
            int filtered_indices[64];
            int filtered_count = 0;
            
            if (_search_filter_length > 0) {
                for (int i = 0; i < num_contacts; i++) {
                    ContactInfo contact;
                    if (the_mesh.getContactByIdx(i, contact)) {
                        char lower_name[32];
                        char lower_filter[32];
                        for (int j = 0; j < 32 && contact.name[j]; j++) {
                            lower_name[j] = tolower(contact.name[j]);
                            lower_name[j+1] = '\0';
                        }
                        for (int j = 0; j < 32 && _search_filter[j]; j++) {
                            lower_filter[j] = tolower(_search_filter[j]);
                            lower_filter[j+1] = '\0';
                        }
                        if (strstr(lower_name, lower_filter) != nullptr) {
                            filtered_indices[filtered_count++] = i;
                        }
                    }
                }
                num_contacts = filtered_count;
            } else {
                for (int i = 0; i < num_contacts; i++) {
                    filtered_indices[i] = i;
                }
                filtered_count = num_contacts;
            }
            
            if (left || right) {
                // Switch to channels, deselect settings icon
                _menu_state = MenuScreen::CHANNELS;
                _scroll_pos = 0;
                _selected_idx = 0;
                _settings_selected = false;
                _search_filter_length = 0;
                _search_filter[0] = '\0';
            } else if (up) {
                if (_settings_selected) {
                    // Already on settings icon, wrap to last contact
                    if (num_contacts > 0) {
                        _settings_selected = false;
                        _selected_idx = num_contacts - 1;
                        _scroll_pos = (_selected_idx > 2) ? _selected_idx - 2 : 0;
                    }
                    // If no contacts, stay on settings icon
                } else if (_selected_idx == 0 && num_contacts > 0) {
                    // At first contact, go to settings icon and deselect contact
                    _settings_selected = true;
                    _selected_idx = -1; // Deselect contact
                } else if (num_contacts > 0) {
                    // Navigate up in list
                    _selected_idx = (_selected_idx > 0) ? _selected_idx - 1 : num_contacts - 1;
                    if (_selected_idx < _scroll_pos) {
                        _scroll_pos = _selected_idx;
                    }
                } else if (num_contacts == 0 && !_settings_selected) {
                    // No contacts but not on settings - go to settings
                    _settings_selected = true;
                }
            } else if (down) {
                if (_settings_selected) {
                    // From settings icon, go to first contact
                    if (num_contacts > 0) {
                        _settings_selected = false;
                        _selected_idx = 0;
                        _scroll_pos = 0;
                    }
                    // If no contacts, stay on settings icon
                } else if (num_contacts > 0) {
                    // If _selected_idx is -1 (hamburger selected), go to first contact
                    if (_selected_idx == -1) {
                        _selected_idx = 0;
                        _scroll_pos = 0;
                    } else {
                        // Navigate down in list
                        _selected_idx = (_selected_idx < num_contacts - 1) ? _selected_idx + 1 : 0;
                        if (_selected_idx >= _scroll_pos + 3) {
                            _scroll_pos = _selected_idx - 2;
                        }
                        // If wrapped to 0, deselect settings
                        if (_selected_idx == 0 && _selected_idx < _scroll_pos) {
                            _scroll_pos = 0;
                        }
                    }
                } else if (num_contacts == 0 && !_settings_selected) {
                    // No contacts but not on settings - go to settings
                    _settings_selected = true;
                }
            } else if (select) {
                if (_settings_selected) {
                    // Open settings menu
                    _menu_state = MenuScreen::SETTINGS;
                    _settings_category = SettingsCategory::MAIN_MENU;
                    _settings_menu_idx = 0;
                    _settings_item_idx = 0; // Start on first category
                    _settings_scroll_pos = 0; // Reset scroll
                    _settings_selected = false;
                } else if (num_contacts > 0 && _selected_idx >= 0) {
                    // Open chat with selected contact (use filtered index)
                    int real_idx = filtered_indices[_selected_idx];
                    if (the_mesh.getContactByIdx(real_idx, _chat_contact)) {
                        _menu_state = MenuScreen::CHAT;
                        _chat_is_channel = false;
                        _input_mode = false;
                        _input_buffer[0] = '\0';
                        _input_length = 0;
                        _chat_scroll = 0;
                        _chat_msg_scroll_index = 0; // Reset scroll to newest
                        _search_filter_length = 0;
                        _search_filter[0] = '\0';
                        // Messages are already stored in global history, just filter them
                    }
                }
            }
            break;
        }
            
        case MenuScreen::CHANNELS: {
            // Count and collect channels
            int num_channels = 0;
            ChannelDetails channels[MAX_GROUP_CHANNELS];
            int channel_mesh_idx[MAX_GROUP_CHANNELS];
            
            for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
                if (the_mesh.getChannel(i, channels[num_channels]) && channels[num_channels].name[0] != '\0') {
                    channel_mesh_idx[num_channels] = i;
                    num_channels++;
                }
            }
            
            // Filter channels by search term
            int filtered_indices[MAX_GROUP_CHANNELS];
            int filtered_count = 0;
            
            if (_search_filter_length > 0) {
                for (int i = 0; i < num_channels; i++) {
                    char lower_name[32];
                    char lower_filter[32];
                    for (int j = 0; j < 32 && channels[i].name[j]; j++) {
                        lower_name[j] = tolower(channels[i].name[j]);
                        lower_name[j+1] = '\0';
                    }
                    for (int j = 0; j < 32 && _search_filter[j]; j++) {
                        lower_filter[j] = tolower(_search_filter[j]);
                        lower_filter[j+1] = '\0';
                    }
                    if (strstr(lower_name, lower_filter) != nullptr) {
                        filtered_indices[filtered_count++] = i;
                    }
                }
                num_channels = filtered_count;
            } else {
                for (int i = 0; i < num_channels; i++) {
                    filtered_indices[i] = i;
                }
                filtered_count = num_channels;
            }
            
            if (left || right) {
                // Switch to contacts, deselect settings icon
                _menu_state = MenuScreen::CONTACTS;
                _scroll_pos = 0;
                _selected_idx = 0;
                _settings_selected = false;
                _search_filter_length = 0;
                _search_filter[0] = '\0';
            } else if (up) {
                if (_settings_selected) {
                    // Already on settings icon, wrap to last channel
                    if (num_channels > 0) {
                        _settings_selected = false;
                        _selected_idx = num_channels - 1;
                        _scroll_pos = (_selected_idx > 2) ? _selected_idx - 2 : 0;
                    }
                    // If no channels, stay on settings icon
                } else if (_selected_idx == 0 && num_channels > 0) {
                    // At first channel, go to settings icon and deselect channel
                    _settings_selected = true;
                    _selected_idx = -1; // Deselect channel
                } else if (num_channels > 0) {
                    // Navigate up in list
                    _selected_idx = (_selected_idx > 0) ? _selected_idx - 1 : num_channels - 1;
                    if (_selected_idx < _scroll_pos) {
                        _scroll_pos = _selected_idx;
                    }
                } else if (num_channels == 0 && !_settings_selected) {
                    // No channels but not on settings - go to settings
                    _settings_selected = true;
                }
            } else if (down) {
                if (_settings_selected) {
                    // From settings icon, go to first channel
                    if (num_channels > 0) {
                        _settings_selected = false;
                        _selected_idx = 0;
                        _scroll_pos = 0;
                    }
                    // If no channels, stay on settings icon
                } else if (num_channels > 0) {
                    // If _selected_idx is -1 (hamburger selected), go to first channel
                    if (_selected_idx == -1) {
                        _selected_idx = 0;
                        _scroll_pos = 0;
                    } else {
                        // Navigate down in list
                        _selected_idx = (_selected_idx < num_channels - 1) ? _selected_idx + 1 : 0;
                        if (_selected_idx >= _scroll_pos + 3) {
                            _scroll_pos = _selected_idx - 2;
                        }
                        if (_selected_idx == 0 && _selected_idx < _scroll_pos) {
                            _scroll_pos = 0;
                        }
                    }
                } else if (num_channels == 0 && !_settings_selected) {
                    // No channels but not on settings - go to settings
                    _settings_selected = true;
                }
            } else if (select) {
                if (_settings_selected) {
                    // Open settings menu
                    _menu_state = MenuScreen::SETTINGS;
                    _settings_category = SettingsCategory::MAIN_MENU;
                    _settings_menu_idx = 0;
                    _settings_item_idx = 0; // Start on first category
                    _settings_scroll_pos = 0; // Reset scroll
                    _settings_selected = false;
                } else if (num_channels > 0 && _selected_idx >= 0) {
                    // Open chat with selected channel (use filtered index)
                    int real_idx = filtered_indices[_selected_idx];
                    _chat_channel = channels[real_idx];
                    _menu_state = MenuScreen::CHAT;
                    _chat_is_channel = true;
                    _input_mode = false;
                    _input_buffer[0] = '\0';
                    _input_length = 0;
                    _chat_scroll = 0;
                    _chat_msg_scroll_index = 0;
                    _search_filter_length = 0;
                    _search_filter[0] = '\0'; // Reset scroll to newest
                    // Messages are already stored in global history, just filter them
                    // Mark channel as read
                    int original_mesh_idx = channel_mesh_idx[real_idx];
                    _channel_has_unread[original_mesh_idx] = false;
                    strncpy(_last_read_channel, _chat_channel.name, 31);
                    _last_read_channel[31] = '\0';
                }
            }
            break;
        }
            
        case MenuScreen::SETTINGS: {
            if (_settings_category == SettingsCategory::MAIN_MENU) {
                // Main menu navigation with scrolling (5 categories, 3 visible)
                int num_categories = 5;
                
                if (up || down) {
                    if (_settings_item_idx == -1) {
                        // In bottom bar, go back to category list (last item)
                        _settings_item_idx = num_categories - 1;
                        _settings_scroll_pos = (_settings_item_idx > 2) ? _settings_item_idx - 2 : 0;
                    } else {
                        // Navigate between categories with scrolling
                        if (up && _settings_item_idx > 0) {
                            _settings_item_idx--;
                            if (_settings_item_idx < _settings_scroll_pos) {
                                _settings_scroll_pos = _settings_item_idx;
                            }
                        } else if (down && _settings_item_idx < num_categories - 1) {
                            _settings_item_idx++;
                            if (_settings_item_idx >= _settings_scroll_pos + 3) {
                                _settings_scroll_pos = _settings_item_idx - 2;
                            }
                        } else if (down && _settings_item_idx == num_categories - 1) {
                            // Go down from last category to bottom bar
                            _settings_item_idx = -1;
                        } else if (up && _settings_item_idx == 0) {
                            // Wrap to bottom bar
                            _settings_item_idx = -1;
                        } else if (down && _settings_item_idx == -1) {
                            // Wrap from bottom bar back to first category
                            _settings_item_idx = 0;
                            _settings_scroll_pos = 0;
                        }
                    }
                } else if (select) {
                    if (_settings_item_idx >= 0) {
                        // Enter selected category
                        switch (_settings_item_idx) {
                            case 0: _settings_category = SettingsCategory::PUBLIC_INFO; break;
                            case 1: _settings_category = SettingsCategory::RADIO_SETUP; break;
                            case 2: _settings_category = SettingsCategory::THEME; break;
                            case 3: _settings_category = SettingsCategory::OTHER; break;
                            case 4: _settings_category = SettingsCategory::DEVICE_INFO; break;
                        }
                        _settings_item_idx = 0;
                        _settings_menu_idx = 0;
                    } else {
                        // Back button - return to contacts with first contact selected
                        _menu_state = MenuScreen::CONTACTS;
                        _settings_category = SettingsCategory::MAIN_MENU;
                        _settings_item_idx = 0;
                        _settings_menu_idx = 0;
                        _selected_idx = 0; // Select first contact
                        _settings_selected = false;
                    }
                }
                
            } else if (_settings_category == SettingsCategory::THEME) {
                // Theme category navigation
                if (up || down) {
                    if (_settings_menu_idx == 1) {
                        // In bottom bar (Back button), go back to settings items
                        _settings_item_idx = 2; // Start on Secondary Color (bottom item)
                        _settings_menu_idx = 0;
                    } else {
                        // Navigate between settings items (0 = Brightness, 1 = Main Color, 2 = Secondary Color)
                        if (up && _settings_item_idx > 0) {
                            _settings_item_idx--;
                        } else if (down && _settings_item_idx < 2) {
                            _settings_item_idx++;
                        } else if (down && _settings_item_idx == 2) {
                            // Go down to bottom bar (Back button)
                            _settings_menu_idx = 1;
                        } else if (up && _settings_item_idx == 0) {
                            // Can't go up from Brightness
                        }
                    }
                } else if (left || right) {
                    if (_settings_item_idx == 0) {
                        // Adjust brightness
                        int step = 15; // ~6% steps
                        if (left && _brightness > step) {
                            _brightness -= step;
                        } else if (left && _brightness <= step) {
                            _brightness = 0;
                        } else if (right && _brightness < (255 - step)) {
                            _brightness += step;
                        } else if (right) {
                            _brightness = 255;
                        }
                        M5Cardputer.Display.setBrightness(_brightness);
                    } else if (_settings_item_idx == 1) {
                        // Change main color
                        if (left && _main_color_idx > 0) {
                            _main_color_idx--;
                        } else if (left) {
                            _main_color_idx = NUM_COLORS - 1;
                        } else if (right && _main_color_idx < NUM_COLORS - 1) {
                            _main_color_idx++;
                        } else if (right) {
                            _main_color_idx = 0;
                        }
                        applyTheme();
                    } else if (_settings_item_idx == 2) {
                        // Change secondary color
                        if (left && _secondary_color_idx > 0) {
                            _secondary_color_idx--;
                        } else if (left) {
                            _secondary_color_idx = NUM_COLORS - 1;
                        } else if (right && _secondary_color_idx < NUM_COLORS - 1) {
                            _secondary_color_idx++;
                        } else if (right) {
                            _secondary_color_idx = 0;
                        }
                        applyTheme();
                    }
                } else if (select) {
                    if (_settings_menu_idx == 1) {
                        // Back button - save settings and return to main menu
                        saveSettings();
                        _settings_category = SettingsCategory::MAIN_MENU;
                        _settings_menu_idx = 0;
                        _settings_item_idx = 0;
                    }
                }
                
            } else if (_settings_category == SettingsCategory::PUBLIC_INFO) {
                // Public Info navigation with scrolling (3 options, 3 visible)
                int num_options = 3;
                
                if (up || down) {
                    if (_settings_menu_idx == 1) {
                        // In bottom bar, go back to options
                        _settings_item_idx = num_options - 1;
                        _public_info_scroll_pos = (_settings_item_idx > 2) ? _settings_item_idx - 2 : 0;
                        _settings_menu_idx = 0;
                    } else {
                        // Navigate between options with scrolling
                        if (up && _settings_item_idx > 0) {
                            _settings_item_idx--;
                            if (_settings_item_idx < _public_info_scroll_pos) {
                                _public_info_scroll_pos = _settings_item_idx;
                            }
                        } else if (down && _settings_item_idx < num_options - 1) {
                            _settings_item_idx++;
                            if (_settings_item_idx >= _public_info_scroll_pos + 3) {
                                _public_info_scroll_pos = _settings_item_idx - 2;
                            }
                        } else if (down && _settings_item_idx == num_options - 1) {
                            // Go down to bottom bar
                            _settings_menu_idx = 1;
                        } else if (up && _settings_item_idx == 0) {
                            // Wrap to bottom bar
                            _settings_menu_idx = 1;
                        }
                    }
                } else if (select) {
                    if (_settings_menu_idx == 1) {
                        // Back button - return to main menu
                        _settings_category = SettingsCategory::MAIN_MENU;
                        _settings_item_idx = 0;
                        _settings_menu_idx = 0;
                        _public_info_scroll_pos = 0;
                    } else if (_settings_item_idx >= 0) {
                        // Handle option selection
                        switch (_settings_item_idx) {
                            case 0: // Change name
                                _editing_name = true;
                                _settings_menu_idx = 0; // Start on Save
                                strncpy(_edit_buffer, _node_prefs->node_name, 31);
                                _edit_buffer[31] = '\0';
                                _edit_buffer_length = strlen(_edit_buffer);
                                break;
                            case 1: // Share key (QR code)
                                _show_qr_code = true;
                                break;
                            case 2: // Toggle Share Position checkbox
                                if (_node_prefs) {
                                    _node_prefs->advert_loc_policy = 
                                        (_node_prefs->advert_loc_policy == ADVERT_LOC_SHARE) ? 
                                        ADVERT_LOC_NONE : ADVERT_LOC_SHARE;
                                    // Save NodePrefs to persistent storage
                                    the_mesh.savePrefs();
                                }
                                break;
                        }
                    }
                }
                
            } else if (_settings_category == SettingsCategory::RADIO_SETUP) {
#ifdef HAS_GPS
                // Radio Setup navigation (3 options: GPS, Choose preset, Manual setup)
                int num_options = 3;
#else
                // Radio Setup navigation (2 options: Choose preset, Manual setup)
                int num_options = 2;
#endif
                
                if (up || down) {
                    if (_settings_menu_idx == 1) {
                        // In bottom bar, go back to options
                        _settings_item_idx = num_options - 1;
                        // Adjust scroll position
                        if (_settings_item_idx >= 3) {
                            _radio_setup_scroll_pos = _settings_item_idx - 2;
                        }
                        _settings_menu_idx = 0;
                    } else {
                        // Navigate between options with scrolling
                        if (up && _settings_item_idx > 0) {
                            _settings_item_idx--;
                            if (_settings_item_idx < _radio_setup_scroll_pos) {
                                _radio_setup_scroll_pos = _settings_item_idx;
                            }
                        } else if (down && _settings_item_idx < num_options - 1) {
                            _settings_item_idx++;
                            if (_settings_item_idx >= _radio_setup_scroll_pos + 3) {
                                _radio_setup_scroll_pos = _settings_item_idx - 2;
                            }
                        } else if (down && _settings_item_idx == num_options - 1) {
                            // Go down to bottom bar
                            _settings_menu_idx = 1;
                        } else if (up && _settings_item_idx == 0) {
                            // Wrap to bottom bar
                            _settings_menu_idx = 1;
                        }
                    }
                } else if (select) {
                    if (_settings_menu_idx == 1) {
                        // Back button - return to main menu
                        _settings_category = SettingsCategory::MAIN_MENU;
                        _settings_item_idx = 1; // Return to Radio Setup in main menu
                        _settings_menu_idx = 0;
                        _radio_setup_scroll_pos = 0;
                    } else if (_settings_item_idx >= 0) {
                        // Handle option selection
#ifdef HAS_GPS
                        if (_settings_item_idx == 0) {
                            // Toggle GPS checkbox
                            if (_node_prefs && _sensors) {
                                _node_prefs->gps_enabled = !_node_prefs->gps_enabled;
                                _sensors->setSettingValue("gps", _node_prefs->gps_enabled ? "1" : "0");
                                the_mesh.savePrefs();
                            }
                        } else if (_settings_item_idx == 1) {
                            // Choose preset
                            _settings_category = SettingsCategory::RADIO_PRESET;
                            _settings_item_idx = 0;
                            _radio_preset_scroll_pos = 0;
                        } else if (_settings_item_idx == 2) {
                            // Manual setup - start with frequency
                            _manual_setup_step = 0;
                            _editing_frequency = true;
                            _edit_buffer_length = snprintf(_edit_buffer, sizeof(_edit_buffer), "%.2f", _node_prefs->freq);
                            _settings_menu_idx = 0;
                        }
#else
                        if (_settings_item_idx == 0) {
                            // Choose preset
                            _settings_category = SettingsCategory::RADIO_PRESET;
                            _settings_item_idx = 0;
                            _radio_preset_scroll_pos = 0;
                        } else if (_settings_item_idx == 1) {
                            // Manual setup - start with frequency
                            _manual_setup_step = 0;
                            _editing_frequency = true;
                            _edit_buffer_length = snprintf(_edit_buffer, sizeof(_edit_buffer), "%.2f", _node_prefs->freq);
                            _settings_menu_idx = 0;
                        }
#endif
                    }
                }
                
            } else if (_settings_category == SettingsCategory::RADIO_PRESET) {
                // Radio Preset selection (14 presets with scrolling)
                int num_presets = NUM_RADIO_PRESETS;
                
                if (up || down) {
                    if (_settings_menu_idx == 1) {
                        // In bottom bar (Back button), go back to presets
                        _settings_item_idx = num_presets - 1;
                        _radio_preset_scroll_pos = (_settings_item_idx > 2) ? _settings_item_idx - 2 : 0;
                        _settings_menu_idx = 0;
                    } else {
                        // Navigate between presets with scrolling
                        if (up && _settings_item_idx > 0) {
                            _settings_item_idx--;
                            if (_settings_item_idx < _radio_preset_scroll_pos) {
                                _radio_preset_scroll_pos = _settings_item_idx;
                            }
                        } else if (down && _settings_item_idx < num_presets - 1) {
                            _settings_item_idx++;
                            if (_settings_item_idx >= _radio_preset_scroll_pos + 3) {
                                _radio_preset_scroll_pos = _settings_item_idx - 2;
                            }
                        } else if (down && _settings_item_idx == num_presets - 1) {
                            // Go down to bottom bar (Back button)
                            _settings_menu_idx = 1;
                        } else if (up && _settings_item_idx == 0) {
                            // Wrap to bottom bar (Back button)
                            _settings_menu_idx = 1;
                        }
                    }
                } else if (select) {
                    if (_settings_menu_idx == 1) {
                        // Back button - return to Radio Setup
                        _settings_category = SettingsCategory::RADIO_SETUP;
                        _settings_item_idx = 1; // Return to "Choose preset" in Radio Setup
                        _settings_menu_idx = 0;
                    } else if (_settings_item_idx >= 0 && _settings_item_idx < num_presets) {
                        // Apply selected preset
                        const RadioPreset& preset = RADIO_PRESETS[_settings_item_idx];
                        
                        if (_node_prefs) {
                            // NodePrefs stores freq as float MHz, not Hz
                            _node_prefs->freq = preset.freq_mhz;
                            _node_prefs->sf = preset.sf;
                            _node_prefs->bw = preset.bw_khz;
                            _node_prefs->cr = preset.cr;
                            
                            // Save to persistent storage
                            the_mesh.savePrefs();
                            
                            Serial.printf("[Radio] Applied preset: %s (%.3f MHz, SF%u, BW%.1f, CR%u)\n",
                                preset.name, preset.freq_mhz, preset.sf, preset.bw_khz, preset.cr);
                            
                            // Show notification
                            _notification_from[31] = '\0';
                            strncpy(_notification_text, "Restarting", 127);
                            _notification_text[127] = '\0';
                            _notification_expiry = millis() + 2000;
                            _has_notification = true;
                            
                            // Force immediate screen refresh to show notification
                            if (_display && _display->isOn()) {
                                _display->startFrame();
                                renderSettingsMenu();
                                if (_has_notification && millis() < _notification_expiry) {
                                    renderNotification();
                                }
                                _display->endFrame();
                            }
                            
                            // Restart device to apply radio settings
                            Serial.println("[Radio] Restarting device to apply preset...");
                            delay(2000); // Show notification for 2 seconds
                            ESP.restart();
                        }
                    }
                }
                
            } else if (_settings_category == SettingsCategory::OTHER) {
                // Other settings navigation (3 options: Sleep timeout, Factory Reset, Support)
                const uint16_t timeout_values[] = {10, 30, 60, 120, 300, 0}; // 10s, 30s, 1min, 2min, 5min, Never
                
                if (up || down) {
                    if (_settings_menu_idx == 0) {
                        // Navigate between 3 options (0-2) and bottom bar
                        if (up && _settings_item_idx > 0) {
                            _settings_item_idx--;
                        } else if (down && _settings_item_idx < 2) {
                            _settings_item_idx++;
                        } else if (down && _settings_item_idx == 2) {
                            // Move to bottom bar (Back button)
                            _settings_menu_idx = 1;
                        }
                    } else {
                        // On bottom bar
                        if (up) {
                            // Move back to options
                            _settings_menu_idx = 0;
                        }
                    }
                } else if (left || right) {
                    if (_settings_menu_idx == 0 && _settings_item_idx == 0 && _node_prefs) {
                        // Adjust sleep timeout with left/right arrows
                        int current_idx = 4; // Default to 5min (300s)
                        
                        // Find current index
                        for (int i = 0; i < 6; i++) {
                            if (_node_prefs->screen_timeout_seconds == timeout_values[i]) {
                                current_idx = i;
                                break;
                            }
                        }
                        
                        // Change value
                        if (left && current_idx > 0) {
                            current_idx--;
                        } else if (left && current_idx == 0) {
                            current_idx = 5; // Wrap to Never
                        } else if (right && current_idx < 5) {
                            current_idx++;
                        } else if (right && current_idx == 5) {
                            current_idx = 0; // Wrap to 10s
                        }
                        
                        // Apply new value
                        _node_prefs->screen_timeout_seconds = timeout_values[current_idx];
                        
                        // Update runtime timeout
                        if (_node_prefs->screen_timeout_seconds == 0) {
                            _screen_timeout_millis = 0; // Never
                        } else {
                            _screen_timeout_millis = (unsigned long)_node_prefs->screen_timeout_seconds * 1000UL;
                        }
                        
                        // Reset auto-off timer with new timeout
                        if (_screen_timeout_millis > 0) {
                            _auto_off = millis() + _screen_timeout_millis;
                        } else {
                            _auto_off = 0;
                        }
                        
                        // Save to persistent storage immediately
                        the_mesh.savePrefs();
                        
                        Serial.printf("[Screen] Timeout changed to %u seconds (saved to SPIFFS)\n", _node_prefs->screen_timeout_seconds);
                    }
                } else if (select) {
                    if (_settings_menu_idx == 0) {
                        // Options selected
                        if (_settings_item_idx == 0) {
                            // Sleep timeout - do nothing (changed with left/right)
                        } else if (_settings_item_idx == 1) {
                            // Factory Reset selected - show confirmation
                            _show_factory_reset_confirm = true;
                            _settings_menu_idx = 0; // Start on "Yes"
                        } else if (_settings_item_idx == 2) {
                            // Support the project - show QR code
                            _show_qr_code = true;
                        }
                    } else {
                        // Back button selected
                        _settings_category = SettingsCategory::MAIN_MENU;
                        _settings_item_idx = 3; // Return to "Other" in main menu
                        _settings_menu_idx = 0;
                    }
                }
                
            } else if (_settings_category == SettingsCategory::DEVICE_INFO) {
                // Device Info - read-only, any key press returns to menu
                if (up || down || left || right || select) {
                    // Any key - back to main menu
                    _settings_category = SettingsCategory::MAIN_MENU;
                    _settings_item_idx = 0;
                    _settings_menu_idx = 0;
                }
                
            } else {
                // Other categories (empty for now) - just Back button
                if (select) {
                    // Back to main menu
                    _settings_category = SettingsCategory::MAIN_MENU;
                    _settings_item_idx = 0;
                    _settings_menu_idx = 0;
                }
            }
            break;
        }
            
        default:
            break;
    }
}

void UITask::sendMessage() {
    if (_input_length == 0) return;
    
    uint32_t timestamp = rtc_clock.getCurrentTime();
    
    // Add to chat history with contact/channel info
    if (_chat_is_channel) {
        addMessageToHistory(_node_prefs->node_name, _input_buffer, true, _chat_channel.name, true);
    } else {
        addMessageToHistory(_node_prefs->node_name, _input_buffer, true, _chat_contact.name, false);
    }
    
    if (_chat_is_channel) {
        // Send to channel
        the_mesh.sendGroupMessage(timestamp, _chat_channel.channel, 
                                  _node_prefs->node_name, _input_buffer, _input_length);
    } else {
        // Send to contact
        uint32_t expected_ack = 0;
        uint32_t timeout = 0;
        the_mesh.sendMessage(_chat_contact, timestamp, 0, _input_buffer, expected_ack, timeout);
    }
    
    // NOTE: Don't sync outgoing messages to phone via BLE!
    // Phone will receive them through mesh network naturally.
    // Only sync RECEIVED messages (which happens automatically in MyMesh::queueMessage)
}

void UITask::addMessageToHistory(const char* from_name, const char* text, bool is_outgoing,
                                  const char* contact_or_channel, bool is_channel) {
    if (_chat_history_count >= MAX_CHAT_MESSAGES) {
        // Shift messages down
        for (int i = 0; i < MAX_CHAT_MESSAGES - 1; i++) {
            _chat_history[i] = _chat_history[i + 1];
        }
        _chat_history_count = MAX_CHAT_MESSAGES - 1;
    }
    
    ChatMessage& msg = _chat_history[_chat_history_count];
    strncpy(msg.from_name, from_name, 31);
    msg.from_name[31] = '\0';
    strncpy(msg.text, text, 127);
    msg.text[127] = '\0';
    strncpy(msg.contact_or_channel, contact_or_channel, 31);
    msg.contact_or_channel[31] = '\0';
    msg.is_outgoing = is_outgoing;
    msg.is_channel = is_channel;
    msg.timestamp = millis();
    
    _chat_history_count++;
}

void UITask::filterDisplayText(const char* input, char* output, int max_len) {
    int out_idx = 0;
    int in_idx = 0;
    
    while (input[in_idx] != '\0' && out_idx < max_len - 1) {
        unsigned char c = (unsigned char)input[in_idx];
        
        // Handle 2-byte UTF-8 sequences (most European diacritics)
        if (c == 0xC4 || c == 0xC5) {
            unsigned char next = (unsigned char)input[in_idx + 1];
            const char* replacement = nullptr;
            
            // Polish characters
            if (c == 0xC4 && next == 0x85) replacement = "a";      // ą
            else if (c == 0xC4 && next == 0x87) replacement = "c"; // ć
            else if (c == 0xC4 && next == 0x99) replacement = "e"; // ę
            else if (c == 0xC5 && next == 0x82) replacement = "l"; // ł
            else if (c == 0xC5 && next == 0x84) replacement = "n"; // ń
            else if (c == 0xC5 && next == 0x9B) replacement = "s"; // ś
            else if (c == 0xC5 && next == 0xBA) replacement = "z"; // ź
            else if (c == 0xC5 && next == 0xBC) replacement = "z"; // ż
            else if (c == 0xC4 && next == 0x84) replacement = "A"; // Ą
            else if (c == 0xC4 && next == 0x86) replacement = "C"; // Ć
            else if (c == 0xC4 && next == 0x98) replacement = "E"; // Ę
            else if (c == 0xC5 && next == 0x81) replacement = "L"; // Ł
            else if (c == 0xC5 && next == 0x83) replacement = "N"; // Ń
            else if (c == 0xC5 && next == 0x9A) replacement = "S"; // Ś
            else if (c == 0xC5 && next == 0xB9) replacement = "Z"; // Ź
            else if (c == 0xC5 && next == 0xBB) replacement = "Z"; // Ż
            
            // Czech, Slovak, Hungarian, Romanian
            else if (c == 0xC4 && next == 0x8D) replacement = "c"; // č
            else if (c == 0xC4 && next == 0x8F) replacement = "d"; // ď
            else if (c == 0xC4 && next == 0x9B) replacement = "e"; // ě
            else if (c == 0xC5 && next == 0x99) replacement = "r"; // ř
            else if (c == 0xC5 && next == 0xA1) replacement = "s"; // š
            else if (c == 0xC5 && next == 0xA5) replacement = "t"; // ť
            else if (c == 0xC5 && next == 0xBE) replacement = "z"; // ž
            else if (c == 0xC4 && next == 0x83) replacement = "a"; // ă
            else if (c == 0xC5 && next == 0x91) replacement = "o"; // ő
            else if (c == 0xC5 && next == 0xB1) replacement = "u"; // ű
            else if (c == 0xC4 && next == 0x8C) replacement = "C"; // Č
            else if (c == 0xC4 && next == 0x8E) replacement = "D"; // Ď
            else if (c == 0xC4 && next == 0x9A) replacement = "E"; // Ě
            else if (c == 0xC5 && next == 0x98) replacement = "R"; // Ř
            else if (c == 0xC5 && next == 0xA0) replacement = "S"; // Š
            else if (c == 0xC5 && next == 0xA4) replacement = "T"; // Ť
            else if (c == 0xC5 && next == 0xBD) replacement = "Z"; // Ž
            else if (c == 0xC4 && next == 0x82) replacement = "A"; // Ă
            else if (c == 0xC5 && next == 0x90) replacement = "O"; // Ő
            else if (c == 0xC5 && next == 0xB0) replacement = "U"; // Ű
            
            if (replacement != nullptr) {
                while (*replacement && out_idx < max_len - 1) {
                    output[out_idx++] = *replacement++;
                }
                in_idx += 2;
            } else {
                in_idx += 2; // Skip unknown sequence
            }
        }
        // C3 prefix: Latin diacritics, German umlauts, etc.
        else if (c == 0xC3) {
            unsigned char next = (unsigned char)input[in_idx + 1];
            const char* replacement = nullptr;
            
            // Lowercase
            if (next == 0xA0 || next == 0xA1 || next == 0xA2 || next == 0xA3 || next == 0xA4 || next == 0xA5) replacement = "a"; // à á â ã ä å
            else if (next == 0xA7) replacement = "c"; // ç
            else if (next == 0xA8 || next == 0xA9 || next == 0xAA || next == 0xAB) replacement = "e"; // è é ê ë
            else if (next == 0xAC || next == 0xAD || next == 0xAE || next == 0xAF) replacement = "i"; // ì í î ï
            else if (next == 0xB1) replacement = "n"; // ñ
            else if (next == 0xB2 || next == 0xB3 || next == 0xB4 || next == 0xB5 || next == 0xB6 || next == 0xB8) replacement = "o"; // ò ó ô õ ö ø
            else if (next == 0xB9 || next == 0xBA || next == 0xBB || next == 0xBC) replacement = "u"; // ù ú û ü
            else if (next == 0xBD || next == 0xBF) replacement = "y"; // ý ÿ
            else if (next == 0xA6) replacement = "ae"; // æ
            else if (next == 0xB0) replacement = "d"; // ð (Icelandic)
            else if (next == 0x9F) replacement = "ss"; // ß (German)
            
            // Uppercase
            else if (next == 0x80 || next == 0x81 || next == 0x82 || next == 0x83 || next == 0x84 || next == 0x85) replacement = "A"; // À Á Â Ã Ä Å
            else if (next == 0x87) replacement = "C"; // Ç
            else if (next == 0x88 || next == 0x89 || next == 0x8A || next == 0x8B) replacement = "E"; // È É Ê Ë
            else if (next == 0x8C || next == 0x8D || next == 0x8E || next == 0x8F) replacement = "I"; // Ì Í Î Ï
            else if (next == 0x91) replacement = "N"; // Ñ
            else if (next == 0x92 || next == 0x93 || next == 0x94 || next == 0x95 || next == 0x96 || next == 0x98) replacement = "O"; // Ò Ó Ô Õ Ö Ø
            else if (next == 0x99 || next == 0x9A || next == 0x9B || next == 0x9C) replacement = "U"; // Ù Ú Û Ü
            else if (next == 0x9D) replacement = "Y"; // Ý
            else if (next == 0x86) replacement = "AE"; // Æ
            else if (next == 0x90) replacement = "D"; // Ð (Icelandic)
            
            if (replacement != nullptr) {
                while (*replacement && out_idx < max_len - 1) {
                    output[out_idx++] = *replacement++;
                }
                in_idx += 2;
            } else {
                in_idx += 2; // Skip unknown sequence
            }
        }
        // C4 prefix continued (more diacritics)
        else if (c == 0xC4) {
            unsigned char next = (unsigned char)input[in_idx + 1];
            const char* replacement = nullptr;
            
            // German umlauts (already handled above but for clarity)
            // Turkish and other special cases handled in C4/C5 section above
            
            if (replacement != nullptr) {
                while (*replacement && out_idx < max_len - 1) {
                    output[out_idx++] = *replacement++;
                }
                in_idx += 2;
            } else {
                in_idx += 2;
            }
        }
        // C8 prefix: Turkish ş, Romanian ș ț, etc.
        else if (c == 0xC8) {
            unsigned char next = (unsigned char)input[in_idx + 1];
            const char* replacement = nullptr;
            
            if (next == 0x99) replacement = "s"; // ș (Romanian)
            else if (next == 0x9B) replacement = "t"; // ț (Romanian)
            else if (next == 0x98) replacement = "S"; // Ș
            else if (next == 0x9A) replacement = "T"; // Ț
            
            if (replacement != nullptr) {
                while (*replacement && out_idx < max_len - 1) {
                    output[out_idx++] = *replacement++;
                }
                in_idx += 2;
            } else {
                in_idx += 2;
            }
        }
        // C4 B0/B1: Turkish ı İ (already partially handled)
        else if (c == 0xC4 && input[in_idx + 1] == 0xB1) {
            output[out_idx++] = 'i'; // ı (Turkish dotless i)
            in_idx += 2;
        }
        else if (c == 0xC4 && input[in_idx + 1] == 0xB0) {
            output[out_idx++] = 'I'; // İ (Turkish dotted I)
            in_idx += 2;
        }
        // C4 9F: Turkish ğ
        else if (c == 0xC4 && input[in_idx + 1] == 0x9F) {
            output[out_idx++] = 'g'; // ğ
            in_idx += 2;
        }
        else if (c == 0xC4 && input[in_idx + 1] == 0x9E) {
            output[out_idx++] = 'G'; // Ğ
            in_idx += 2;
        }
        // C5 9F: Turkish ş
        else if (c == 0xC5 && input[in_idx + 1] == 0x9F) {
            output[out_idx++] = 's'; // ş
            in_idx += 2;
        }
        else if (c == 0xC5 && input[in_idx + 1] == 0x9E) {
            output[out_idx++] = 'S'; // Ş
            in_idx += 2;
        }
        // C3 BE/C3 9E: Icelandic þ Þ
        else if (c == 0xC3 && input[in_idx + 1] == 0xBE) {
            if (out_idx + 2 < max_len) {
                output[out_idx++] = 't';
                output[out_idx++] = 'h'; // þ → th
            }
            in_idx += 2;
        }
        else if (c == 0xC3 && input[in_idx + 1] == 0x9E) {
            if (out_idx + 2 < max_len) {
                output[out_idx++] = 'T';
                output[out_idx++] = 'H'; // Þ → TH
            }
            in_idx += 2;
        }
        // C5 92/93: œ Œ (French ligatures)
        else if (c == 0xC5 && input[in_idx + 1] == 0x93) {
            if (out_idx + 2 < max_len) {
                output[out_idx++] = 'o';
                output[out_idx++] = 'e'; // œ
            }
            in_idx += 2;
        }
        else if (c == 0xC5 && input[in_idx + 1] == 0x92) {
            if (out_idx + 2 < max_len) {
                output[out_idx++] = 'O';
                output[out_idx++] = 'E'; // Œ
            }
            in_idx += 2;
        }
        // Handle other UTF-8 sequences (emojis and special characters) - skip them
        else if (c >= 0x80) {
            // Skip UTF-8 multi-byte sequences completely
            if (c >= 0xF0) {
                // 4-byte sequence (most emojis)
                in_idx += 4;
            } else if (c >= 0xE0) {
                // 3-byte sequence
                in_idx += 3;
            } else if (c >= 0xC0) {
                // 2-byte sequence (fallback for unhandled)
                in_idx += 2;
            } else {
                // Invalid sequence, skip 1 byte
                in_idx += 1;
            }
        }
        // ASCII printable characters (space to ~)
        else if (c >= 0x20 && c <= 0x7E) {
            output[out_idx++] = c;
            in_idx++;
        }
        // Skip control characters
        else {
            in_idx++;
        }
    }
    
    output[out_idx] = '\0';
}

void UITask::showAlert(const char* msg) {
    strncpy(_alert, msg, sizeof(_alert) - 1);
    _alert[sizeof(_alert) - 1] = '\0';
    _alert_expiry = millis() + 2000;
}

void UITask::gotoScreen(MenuScreen screen) {
    _menu_state = screen;
}

uint16_t UITask::getBattMilliVolts() {
    #ifdef PIN_VBAT_READ
        return analogReadMilliVolts(PIN_VBAT_READ) * 2;
    #else
        return 0;
    #endif
}

bool UITask::isButtonPressed() {
    return user_btn.isPressed();
}

void UITask::msgRead(int msgcount) {
    // Mark messages as read
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
    // Check if it's a direct message (not a channel message)
    // Direct messages: path_len <= 2 AND no '#' in name
    // Channel messages: contain channel name or have '#' prefix
    bool is_direct_message = true;
    bool is_channel_msg = false;
    
    // Check if message is from a channel
    ChannelDetails temp_ch;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        if (the_mesh.getChannel(i, temp_ch) && temp_ch.name[0] != '\0') {
            if (strstr(from_name, temp_ch.name) != nullptr) {
                is_direct_message = false;
                is_channel_msg = true;
                break;
            }
        }
    }
    
    // Always save message to history (for all contacts and channels)
    if (is_direct_message) {
        // Save to contact's history
        addMessageToHistory(from_name, text, false, from_name, false);
    } else if (is_channel_msg) {
        // For channel messages, extract channel name and save
        char channel_name[32];
        
        // Try to find which channel this belongs to
        ChannelDetails temp_ch;
        bool found_channel = false;
        for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
            if (the_mesh.getChannel(i, temp_ch) && temp_ch.name[0] != '\0') {
                if (strstr(from_name, temp_ch.name) != nullptr) {
                    strncpy(channel_name, temp_ch.name, 31);
                    channel_name[31] = '\0';
                    found_channel = true;
                    
                    // Mark as unread if not currently viewing this channel
                    if (!(_menu_state == MenuScreen::CHAT && _chat_is_channel && 
                          strcmp(_chat_channel.name, temp_ch.name) == 0)) {
                        _channel_has_unread[i] = true;
                    }
                    break;
                }
            }
        }
        
        if (found_channel) {
            addMessageToHistory(from_name, text, false, channel_name, true);
        }
    }
    
    // Only show notification for direct messages AND only if not currently chatting with that person
    if (is_direct_message) {
        // Check if we're currently in chat with this contact
        bool is_currently_chatting = (_menu_state == MenuScreen::CHAT && 
                                      !_chat_is_channel && 
                                      strcmp(_chat_contact.name, from_name) == 0);
        
        if (!is_currently_chatting) {
            strncpy(_notification_from, from_name, 31);
            _notification_from[31] = '\0';
            strncpy(_notification_text, text, 127);
            _notification_text[127] = '\0';
            _notification_expiry = millis() + 1500; // 1.5 seconds for battery optimization
            _has_notification = true;
            _need_refresh = true;
            
            // Wake up display if off or sleeping
            if (_screen_sleeping) {
                _screen_sleeping = false;
                Serial.println("[Sleep] Waking from light sleep (new message)");
            }
            
            if (_display && !_display->isOn()) {
                _display->turnOn();
                Serial.println("[Screen] Display turned on (new message)");
            }
            
            // Reset screen timeout
            if (_screen_timeout_millis > 0) {
                _auto_off = millis() + _screen_timeout_millis;
            }
        }
    }
    
    _need_refresh = true;
}

void UITask::notify(UIEventType t) {
    // Handle notifications
    _need_refresh = true;
}

void UITask::syncChatHistoryToBLE(int max_messages) {
    // Sync last N messages from chat history to phone via BLE
    if (_chat_history_count == 0) {
        Serial.println("No chat history to sync");
        return;
    }
    
    int start_idx = (_chat_history_count > max_messages) ? 
                    (_chat_history_count - max_messages) : 0;
    
    Serial.printf("Syncing %d messages to BLE (from idx %d to %d)\n", 
                  _chat_history_count - start_idx, start_idx, _chat_history_count - 1);
    
    for (int i = start_idx; i < _chat_history_count; i++) {
        ChatMessage& msg = _chat_history[i];
        
        // ONLY sync INCOMING messages (is_outgoing = false)
        // Outgoing messages are already sent through mesh and will appear on phone naturally
        if (msg.is_outgoing) {
            continue; // Skip outgoing messages
        }
        
        // Find contact or channel info
        if (msg.is_channel) {
            // Find channel by name
            ChannelDetails channel;
            bool found = false;
            for (int ch_idx = 0; ch_idx < MAX_GROUP_CHANNELS; ch_idx++) {
                if (the_mesh.getChannel(ch_idx, channel) && 
                    strcmp(channel.name, msg.contact_or_channel) == 0) {
                    found = true;
                    break;
                }
            }
            
            if (found) {
                the_mesh.queueOutgoingMessageForBLE(NULL, &channel, 
                                                     msg.from_name, msg.text, msg.timestamp);
            }
        } else {
            // Find contact by name
            ContactInfo contact;
            bool found = false;
            
            ContactsIterator iter = the_mesh.startContactsIterator();
            while (iter.hasNext(&the_mesh, contact)) {
                if (strcmp(contact.name, msg.contact_or_channel) == 0) {
                    found = true;
                    break;
                }
            }
            
            if (found) {
                the_mesh.queueOutgoingMessageForBLE(&contact, NULL,
                                                     msg.from_name, msg.text, msg.timestamp);
            }
        }
    }
    
    Serial.println("Chat history sync complete");
}

void UITask::enterLightSleep() {
    // Light sleep mode is currently DISABLED because it was too aggressive:
    // - Keyboard didn't wake the device reliably
    // - LoRa packets were sometimes missed
    // - Device became unresponsive after reboot
    //
    // Current approach: Only turn off display, keep CPU running normally
    // This provides good battery life while maintaining full functionality
    
    Serial.println("[Sleep] Light sleep disabled - CPU stays active for reliable operation");
}
