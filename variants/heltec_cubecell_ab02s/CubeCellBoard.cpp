#include "target.h"
#include "Oled.h"
#include "StatusScreen.h"
#include "CubeCellGPS.h"
#include <helpers/asr650x/BigStack.h>
#include <MyMesh.h>

#ifndef BIGSTACK_SIZE
  #define BIGSTACK_SIZE 2048
#endif

static OledPanel oled;
static StatusScreen screen;
static uint32_t last_input_ms = 0;

void CubeCellBoard::begin() {
  // Ed25519 needs more than the fixed 2 KB main stack: it runs on a stack taken from the heap (BigStack.h).
  // Without it, signing would overflow the main stack, so stop here (the app sees no reply).
  if (!asr650x_bigstack_init(BIGSTACK_SIZE)) {
    while (true) delay(1000);
  }
  bool ok = oled.begin();
  sensors.setOledStatus(ok ? "ok" : "none");
  screen.begin(&oled);
#ifdef ASR650X_STACK_WATCH
  asr650x_stack_paint();
#endif
}

static void update_status_input(uint32_t now) {
  asr650x::StatusInput in;
  memset(&in, 0, sizeof(in));
  NodePrefs* prefs = the_mesh.getNodePrefs();
  const asr650x::GpsData& g = cubecell_gps.data();
  in.name = prefs->node_name;
  memcpy(in.id4, the_mesh.self_id.pub_key, 4);
  in.time_valid = rtc_clock.synced();
  in.epoch = rtc_clock.getCurrentTime();
  if (!cubecell_gps.enabled()) in.gps_state = 0;
  else if (cubecell_gps.state() == asr650x::GPS_WARMING) in.gps_state = 1;
  else if (g.pos_valid) in.gps_state = 2;
  else if (cubecell_gps.timed_out()) in.gps_state = 3;
  in.sats = g.sats;
  in.pos_valid = g.pos_valid;
  in.lat_e6 = g.lat_e6;
  in.lon_e6 = g.lon_e6;
  int n = the_mesh.getNumContacts();
  in.contacts = (uint8_t)(n < 0 ? 0 : (n > 255 ? 255 : n));
  in.max_contacts = (uint8_t)(MAX_CONTACTS > 255 ? 255 : MAX_CONTACTS);
  in.freq_khz = (uint32_t)(prefs->freq * 1000.0f + 0.5f);
  in.sf = prefs->sf;
  in.warming_s = cubecell_gps.warming_s();
  screen.set_input(in);
}

void CubeCellBoard::loop() {
  uint32_t now = millis();
  if (now - last_input_ms >= 1000) {
    last_input_ms = now;
    update_status_input(now);
  }
  screen.loop(now);
}

#ifdef ASR650X_STACK_WATCH
extern "C" { extern uint32_t __cy_stack_limit[]; extern uint32_t __cy_stack[]; void* sbrk(int); }

void asr650x_stack_paint() {
  uint32_t* sp;
  __asm volatile("mov %0, sp" : "=r"(sp));
  for (uint32_t* p = __cy_stack_limit; p < sp - 16; p++) *p = 0xA5A5A5A5u;
}

static uint32_t stack_used() {
  uint32_t* p = __cy_stack_limit;
  while (p < __cy_stack && *p == 0xA5A5A5A5u) p++;
  return (uint32_t)((uint8_t*)__cy_stack - (uint8_t*)p);
}

const char* asr650x_watch_string() {
  static char name[64];
  extern char _end;
  snprintf(name, sizeof(name), "CC stk=%u big=%u heap=%u p=%02x%02x%02x", (unsigned)stack_used(),
           (unsigned)asr650x_bigstack_highwater(), (unsigned)((uint32_t)sbrk(0) - (uint32_t)&_end),
           (unsigned)asr650x_boot_pool[0], (unsigned)asr650x_boot_pool[1], (unsigned)asr650x_boot_pool[2]);
  return name;
}
#endif
