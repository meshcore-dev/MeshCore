#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <lvgl.h>

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "ChatStore.h"

struct ContactInfo;

// LVGL-based standalone UI: status bar + bottom tabs (Chats/Contacts/Node/
// Settings), message threads with bubbles, on-screen keyboard.
class UITask : public AbstractUITask {
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  int _msgcount;
  bool _display_asleep;
  unsigned long _next_status_refresh;
  uint8_t _pending_login_key[6] = {0};
  unsigned long _pending_login_deadline = 0;
  bool _wake_prev = false;
  unsigned long _next_wake_poll = 0;
  unsigned long _lock_grace = 0;   // suppress auto-wake right after button lock
  bool _thread_clear_armed = false;

  // active thread (chat screen)
  uint8_t _thread_key[6] = {0};
  bool _thread_is_channel = false;
  bool _thread_is_console = false;   // repeater terminal: sends are CLI commands
  int _thread_channel_idx = -1;
  char _thread_name[36] = {0};

  // repeater manager state
  uint8_t _rep_key[6] = {0};
  char _rep_name[36] = {0};
  bool _rep_logged_in = false;
  bool _rep_logging_in = false;
  uint32_t _stats_tag = 0;   // outstanding status request, matched in onUnhandledResponse

  // trace route state (contact detail Trace button)
  uint32_t _trace_tag = 0;
  unsigned long _trace_started = 0;
  unsigned long _trace_deadline = 0;

  void updateThreadSubtitle();   // route (flood/hops) in header
  void showIncoming(const uint8_t* key, const char* from_name, const char* text);
  void cliReply(const ContactInfo& from, const char* text);
  void loginResult(const uint8_t* pub_key, bool success);
  void statusResponse(const uint8_t* pub_key, const uint8_t* data, int len);
  void repeaterTick();
  void traceTick();

  void buildShell();
  void buildThreadScr();
  void buildRepeaterScr();
  void buildContactOverlays();
  void refreshRepeaterScr();
  void buildChatsTab(lv_obj_t* parent);
  void buildContactsTab(lv_obj_t* parent);
  void buildNodeTab(lv_obj_t* parent);
  void buildSettingsTab(lv_obj_t* parent);
  void refreshStatusBar();

public:
  void refreshChatsTab();
  void refreshContactsTab();
  void refreshNodeTab();
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial)
    : AbstractUITask(board, serial), _sensors(NULL), _node_prefs(NULL),
      _msgcount(0), _display_asleep(false), _next_status_refresh(0) { }

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // navigation / actions (called from LVGL event callbacks)
  void openThread(const uint8_t key[6], const char* name);
  void openContactThread(const ContactInfo& contact);
  void openConsoleThread();   // repeater terminal on _rep_key
  void closeThread();
  void refreshThread();
  void sendFromThread(const char* text);
  void openRepeaterManager(const ContactInfo& contact);
  void closeRepeaterManager();
  void repeaterLogin(const char* password, bool flood = false);
  void sendRepeaterCommand(const char* cmd);
  bool threadIsConsole() const { return _thread_is_console; }
  void showToast(const char* text);
  void nodeNameChanged();   // refresh status bar after settings edit
  void openContactDetail(int contact_idx);   // long-press on contact / map marker
  void clearCurrentThread();                 // trash button in thread header (armed)
  void openRouteForThread();                 // route button in DM thread header
  void openTraceForContact(int contact_idx); // trace button in contact detail
  void requestRepeaterStatus();              // GUI manager status refresh
  void resendMessage(int k);                 // tap a not-delivered bubble
  void openChannelOptions(const uint8_t key[6]);   // long-press a channel row
  void finishSetupWizard(int preset_idx, const char* name, int tz_hours, bool apply);
  const char* savedRepeaterPw();             // saved password for current repeater, or NULL
  int  getMsgCount() const { return _msgcount; }
  NodePrefs* nodePrefs() { return _node_prefs; }
  SensorManager* sensors() { return _sensors; }

  // MyMesh::Listener
  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details, const char* text) override;
  void onQueueSizeChanged(int msgcount) override;
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onACKRecv(uint32_t ack_crc) override;
  bool onUnhandledResponse(const ContactInfo& from, uint32_t tag, const uint8_t* data, uint8_t len) override;
  void onTraceRecv(mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                   const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) override;

  // from AbstractUITask
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};

// phone-style shift for LVGL keyboards: one-shot upper case, double-tap for caps lock
void kbAttachShiftBehavior(lv_obj_t* kb);
