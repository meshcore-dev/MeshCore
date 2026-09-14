#include "MapView.h"

#include <Arduino.h>
#include <math.h>
#include <SD_MMC.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include "../MyMesh.h"
#include "target.h"
#include "UITask.h"

// Wio Tracker L2 Pro SDIO 1-bit wiring; TF power rail is raised by board init
#define SD_PIN_CLK 2
#define SD_PIN_CMD 3
#define SD_PIN_D0  1

#define TILE_PX   256
#define GRID_COLS 3
#define GRID_ROWS 2
#define MAP_MIN_Z 5
#define MAP_MAX_Z 17

static SensorManager* map_sensors = NULL;
static UITask* map_task = NULL;
static lv_obj_t* map_canvas = NULL;
static lv_obj_t* tiles[GRID_COLS * GRID_ROWS];
static lv_obj_t* marker = NULL;
static lv_obj_t* status_lbl = NULL;
static lv_obj_t* zoom_lbl = NULL;
static bool sd_ok = false;

#define MAX_MAP_NODES 16
static lv_obj_t* node_marks[MAX_MAP_NODES];
static lv_obj_t* node_labels[MAX_MAP_NODES];
static int node_contact_idx[MAX_MAP_NODES];   // contact index behind each marker

static void node_mark_cb(lv_event_t* e) {
  int slot = (int)(intptr_t) lv_event_get_user_data(e);
  if (slot >= 0 && slot < MAX_MAP_NODES && map_task != NULL) {
    map_task->openContactDetail(node_contact_idx[slot]);
  }
}

// view center in tile-float coordinates at the current zoom
static int map_z = 13;
static double center_fx, center_fy;
static bool center_initialized = false;

static void lonlatToTileF(double lon, double lat, int z, double* fx, double* fy) {
  double n = (double)(1 << z);
  *fx = (lon + 180.0) / 360.0 * n;
  double lat_r = lat * M_PI / 180.0;
  *fy = (1.0 - asinh(tan(lat_r)) / M_PI) / 2.0 * n;
}

static void savedPosLoad(double* lat, double* lon) {
  File f = SPIFFS.open("/lastpos", "r");
  if (!f) return;
  double a = f.parseFloat();
  double b = f.parseFloat();
  f.close();
  if (a != 0 || b != 0) { *lat = a; *lon = b; }
}
static void savedPosStore(double lat, double lon) {
  static unsigned long last_save = 0;
  if (last_save != 0 && millis() - last_save < 600000) return;   // at most every 10 min
  last_save = millis();
  File f = SPIFFS.open("/lastpos", "w");
  if (f) { f.printf("%.6f %.6f\n", lat, lon); f.close(); }
}

static void mapApplyDim(bool dim);

static void mapInitCenter() {
  if (center_initialized) return;
  double lat = 0, lon = 0;
  NodePrefs* np = map_task != NULL ? map_task->nodePrefs() : NULL;
  if (np != NULL && (np->node_lat != 0 || np->node_lon != 0)) { lat = np->node_lat; lon = np->node_lon; }
  else savedPosLoad(&lat, &lon);
  if (lat == 0 && lon == 0) {
    map_z = 5;   // nothing known yet: wide view until GPS or the phone sets a position
    lonlatToTileF(0.0, 20.0, map_z, &center_fx, &center_fy);
  } else {
    lonlatToTileF(lon, lat, map_z, &center_fx, &center_fy);
  }
  center_initialized = true;
}

// not exported through lvgl.h in this configuration
extern "C" void lv_image_cache_drop(const void* src);

#define SYMBOL_SUN  "\xEF\x86\x85"   /* U+F185 */
#define SYMBOL_MOON "\xEF\x86\x86"   /* U+F186 */

static bool map_night = false;     // defined here: the pak loader needs it
static bool dark_set_ok = false;   // a dark set was seen on the card
static bool day_set_ok = false;    // a day set was seen on the card
static int  dark_hits = 0;         // tiles served from maps_dark this refresh

// Packed tiles: maps/{z}/{x}.pak = 'TPK1' | y0 | y1 | offsets[n+1] | PNGs.
// One file per tile column, since a FAT card copies millions of small files
// far slower than a few thousand large ones. Loose z/x/y.png still works.
static uint8_t* pak_buf[GRID_COLS * GRID_ROWS];
static size_t pak_cap[GRID_COLS * GRID_ROWS];
static lv_image_dsc_t pak_dsc[GRID_COLS * GRID_ROWS];
static int pak_key_z[GRID_COLS * GRID_ROWS];
static int pak_key_x[GRID_COLS * GRID_ROWS];
static int pak_key_y[GRID_COLS * GRID_ROWS];

static bool loadPakTile(lv_obj_t* img, int z, int tx, int ty, int slot) {
  int zkey = z | (map_night ? 0x100 : 0);
  if (pak_key_z[slot] == zkey && pak_key_x[slot] == tx && pak_key_y[slot] == ty) {
    return true;   // this slot already shows exactly this tile (and this set)
  }
  const char* first = map_night ? "maps_dark" : "maps";
  const char* second = map_night ? "maps" : "maps_dark";
  bool from_dark = map_night;
  char path[48];
  snprintf(path, sizeof(path), "/sdcard/%s/%d/%d.pak", first, z, tx);
  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    snprintf(path, sizeof(path), "/sdcard/%s/%d/%d.pak", second, z, tx);
    f = fopen(path, "rb");
    from_dark = !map_night;
  }
  if (f == NULL) return false;
  uint8_t hdr[12];
  uint32_t y0, y1;
  if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "TPK1", 4) != 0) { fclose(f); return false; }
  memcpy(&y0, hdr + 4, 4);
  memcpy(&y1, hdr + 8, 4);
  if (ty < (int) y0 || ty > (int) y1) { fclose(f); return false; }
  uint32_t off[2];
  if (fseek(f, 12 + 4 * (ty - (int) y0), SEEK_SET) != 0 || fread(off, 4, 2, f) != 2) { fclose(f); return false; }
  uint32_t len = off[1] - off[0];
  if (len == 0 || len > 262144) { fclose(f); return false; }
  if (pak_cap[slot] < len) {
    uint8_t* nb = (uint8_t*) heap_caps_realloc(pak_buf[slot], len, MALLOC_CAP_SPIRAM);
    if (nb == NULL) { fclose(f); return false; }
    pak_buf[slot] = nb;
    pak_cap[slot] = len;
  }
  bool ok = fseek(f, off[0], SEEK_SET) == 0 && fread(pak_buf[slot], 1, len, f) == len;
  fclose(f);
  if (!ok) return false;

  lv_image_cache_drop(&pak_dsc[slot]);   // same dsc pointer, new bytes
  memset(&pak_dsc[slot], 0, sizeof(pak_dsc[slot]));
  pak_dsc[slot].header.magic = LV_IMAGE_HEADER_MAGIC;
  pak_dsc[slot].header.cf = LV_COLOR_FORMAT_RAW;
  pak_dsc[slot].header.w = TILE_PX;
  pak_dsc[slot].header.h = TILE_PX;
  pak_dsc[slot].data_size = len;
  pak_dsc[slot].data = pak_buf[slot];
  lv_image_set_src(img, &pak_dsc[slot]);
  lv_obj_invalidate(img);
  pak_key_z[slot] = zkey; pak_key_x[slot] = tx; pak_key_y[slot] = ty;
  if (from_dark) { dark_hits++; dark_set_ok = true; }
  return true;
}

static bool sdMount() {
  if (sd_ok) return true;
  SD_MMC.setPins(SD_PIN_CLK, SD_PIN_CMD, SD_PIN_D0);
  sd_ok = SD_MMC.begin("/sdcard", true /* 1-bit mode */);
  MESH_DEBUG_PRINTLN("map: SD %s", sd_ok ? "mounted" : "mount FAILED");
  if (sd_ok) {
    File d = SD_MMC.open("/maps");
    day_set_ok = d && d.isDirectory();
    MESH_DEBUG_PRINTLN("map: tile store %s", day_set_ok ? "present" : "missing");
    if (d) d.close();
    File dd = SD_MMC.open("/maps_dark");
    dark_set_ok = dd && dd.isDirectory();
    if (dd) dd.close();
    MESH_DEBUG_PRINTLN("map: dark set %s", dark_set_ok ? "present" : "not on card");
    if (dark_set_ok && !day_set_ok) map_night = true;   // dark-only card: start dark
  }
  return sd_ok;
}

void mapViewRefresh() {
  if (map_canvas == NULL) return;
  mapInitCenter();
  dark_hits = 0;

  int cw = lv_obj_get_width(map_canvas);
  int chh = lv_obj_get_height(map_canvas);
  if (cw <= 0) { cw = 320; chh = 150; }   // pre-layout fallback

  int n = 1 << map_z;
  int base_tx = (int)floor(center_fx) - 1;
  int base_ty = (int)floor(center_fy) - 1;

  char src[48];
  for (int j = 0; j < GRID_ROWS; j++) {
    for (int i = 0; i < GRID_COLS; i++) {
      lv_obj_t* img = tiles[j * GRID_COLS + i];
      int tx = base_tx + i;
      int ty = base_ty + j;
      int px = (int)((tx - center_fx) * TILE_PX) + cw / 2;
      int py = (int)((ty - center_fy) * TILE_PX) + chh / 2;
      lv_obj_set_pos(img, px, py);
      int slot = j * GRID_COLS + i;
      if (sd_ok && tx >= 0 && ty >= 0 && tx < n && ty < n) {
        if (!loadPakTile(img, map_z, tx, ty, slot)) {
          // loose-tile fallback; the leading slash matters after "/sdcard"
          pak_key_z[slot] = -1;
          snprintf(src, sizeof(src), "A:/%s/%d/%d/%d.png",
                   map_night ? "maps_dark" : "maps", map_z, tx, ty);
          lv_image_set_src(img, src);
        }
        lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
      } else {
        pak_key_z[slot] = -1;
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  mapApplyDim(map_night && dark_hits == 0);
  lv_label_set_text_fmt(zoom_lbl, map_night ? "z%d dark" : "z%d", map_z);
  if (!sd_ok) {
    lv_label_set_text(status_lbl, "No SD card / tiles");
    lv_obj_remove_flag(status_lbl, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(status_lbl, LV_OBJ_FLAG_HIDDEN);
  }

  int used = 0;
  for (int idx = MAX_ANON_CONTACTS; idx < the_mesh.getTotalContactSlots() && used < MAX_MAP_NODES; idx++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(idx, c)) break;
    if (c.name[0] == 0 || (c.gps_lat == 0 && c.gps_lon == 0)) continue;
    double nfx, nfy;
    lonlatToTileF(c.gps_lon / 1000000.0, c.gps_lat / 1000000.0, map_z, &nfx, &nfy);
    int px = (int)((nfx - center_fx) * TILE_PX) + cw / 2;
    int py = (int)((nfy - center_fy) * TILE_PX) + chh / 2;
    if (px < -20 || py < -20 || px > cw + 20 || py > chh + 20) continue;
    node_contact_idx[used] = idx;
    lv_obj_set_pos(node_marks[used], px - 5, py - 5);
    lv_obj_remove_flag(node_marks[used], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(node_labels[used], c.name);
    lv_obj_set_pos(node_labels[used], px + 7, py - 6);
    lv_obj_remove_flag(node_labels[used], LV_OBJ_FLAG_HIDDEN);
    used++;
  }
  for (int i = used; i < MAX_MAP_NODES; i++) {
    lv_obj_add_flag(node_marks[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(node_labels[i], LV_OBJ_FLAG_HIDDEN);
  }

  bool marker_shown = false;
  if (map_sensors != NULL) {
    LocationProvider* nmea = map_sensors->getLocationProvider();
    if (nmea != NULL && nmea->isValid()) {
      savedPosStore(nmea->getLatitude() / 1000000.0, nmea->getLongitude() / 1000000.0);
      double gfx, gfy;
      lonlatToTileF(nmea->getLongitude() / 1000000.0, nmea->getLatitude() / 1000000.0,
                    map_z, &gfx, &gfy);
      int mx = (int)((gfx - center_fx) * TILE_PX) + cw / 2;
      int my = (int)((gfy - center_fy) * TILE_PX) + chh / 2;
      if (mx >= 0 && my >= 0 && mx < cw && my < chh) {
        lv_obj_set_pos(marker, mx - 6, my - 6);
        lv_obj_remove_flag(marker, LV_OBJ_FLAG_HIDDEN);
        marker_shown = true;
      }
    }
  }
  if (!marker_shown) lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
}

static void map_press_cb(lv_event_t* e) {
  lv_indev_t* indev = lv_indev_active();
  if (indev == NULL) return;
  lv_point_t vect;
  lv_indev_get_vect(indev, &vect);
  if (vect.x == 0 && vect.y == 0) return;
  center_fx -= (double)vect.x / TILE_PX;
  center_fy -= (double)vect.y / TILE_PX;
  double n = (double)(1 << map_z);
  if (center_fx < 0) center_fx = 0;
  if (center_fy < 0) center_fy = 0;
  if (center_fx > n) center_fx = n;
  if (center_fy > n) center_fy = n;
  mapViewRefresh();
}

static void zoom_cb(lv_event_t* e) {
  int dir = (int)(intptr_t) lv_event_get_user_data(e);
  int new_z = map_z + dir;
  if (new_z < MAP_MIN_Z || new_z > MAP_MAX_Z) return;
  double scale = dir > 0 ? 2.0 : 0.5;
  center_fx *= scale;
  center_fy *= scale;
  map_z = new_z;
  mapViewRefresh();
}

static void locate_cb(lv_event_t* e) {
  if (map_sensors == NULL) return;
  LocationProvider* nmea = map_sensors->getLocationProvider();
  if (nmea == NULL || !nmea->isValid()) return;
  lonlatToTileF(nmea->getLongitude() / 1000000.0, nmea->getLatitude() / 1000000.0,
                map_z, &center_fx, &center_fy);
  mapViewRefresh();
}

// with a dark set on the card the toggle swaps sets, otherwise it recolors
static lv_obj_t* night_btn_lbl = NULL;

// Dimming is only a fallback for cards without a dark set, so it is applied
// after a refresh, once it is known whether any tile came from maps_dark.
static void mapApplyDim(bool dim) {
  for (int k = 0; k < GRID_COLS * GRID_ROWS; k++) {
    lv_obj_set_style_image_recolor(tiles[k], lv_color_hex(0x0A1428), 0);
    lv_obj_set_style_image_recolor_opa(tiles[k], dim ? LV_OPA_60 : LV_OPA_TRANSP, 0);
  }
}

static void mapApplyNight() {
  for (int k = 0; k < GRID_COLS * GRID_ROWS; k++) pak_key_z[k] = -1;   // reload
  if (night_btn_lbl != NULL) lv_label_set_text(night_btn_lbl, map_night ? SYMBOL_SUN : SYMBOL_MOON);
  mapViewRefresh();
}

static void mapNightLoad() {
  File f = SPIFFS.open("/mapnight", "r");
  if (f) { map_night = f.parseInt() != 0; f.close(); }
}

static void night_cb(lv_event_t* e) {
  map_night = !map_night;
  File f = SPIFFS.open("/mapnight", "w");
  if (f) { f.print(map_night ? 1 : 0); f.close(); }
  mapApplyNight();
  if (map_task != NULL) {
    if (!map_night) map_task->showToast("Day tiles");
    else if (dark_hits > 0) map_task->showToast("Dark tiles");
    else map_task->showToast("No dark tiles here - dimming instead");
  }
}

static lv_obj_t* makeMapBtn(lv_obj_t* parent, const char* txt, lv_event_cb_t cb, void* ud,
                            lv_align_t align, int x_ofs, int y_ofs) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_size(btn, 36, 30);
  lv_obj_align(btn, align, x_ofs, y_ofs);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
  lv_obj_t* lbl = lv_label_create(btn);
  lv_label_set_text(lbl, txt);
  lv_obj_center(lbl);
  return btn;
}

void mapViewBuild(lv_obj_t* parent, SensorManager* sensors, UITask* task) {
  map_sensors = sensors;
  map_task = task;
  sdMount();

  lv_obj_set_style_pad_all(parent, 0, 0);
  map_canvas = lv_obj_create(parent);
  lv_obj_set_size(map_canvas, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(map_canvas, lv_color_hex(0x101820), 0);
  lv_obj_set_style_border_width(map_canvas, 0, 0);
  lv_obj_set_style_radius(map_canvas, 0, 0);
  lv_obj_set_style_pad_all(map_canvas, 0, 0);
  lv_obj_remove_flag(map_canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(map_canvas, LV_OBJ_FLAG_SCROLL_CHAIN);   // drags pan the map, never the tabview
  lv_obj_add_flag(map_canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(map_canvas, map_press_cb, LV_EVENT_PRESSING, NULL);
  lv_obj_set_style_clip_corner(map_canvas, true, 0);

  for (int k = 0; k < GRID_COLS * GRID_ROWS; k++) {
    pak_key_z[k] = pak_key_x[k] = pak_key_y[k] = -1;
    tiles[k] = lv_image_create(map_canvas);
    lv_obj_set_size(tiles[k], TILE_PX, TILE_PX);
    lv_obj_remove_flag(tiles[k], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(tiles[k], LV_OBJ_FLAG_HIDDEN);
  }

  for (int i = 0; i < MAX_MAP_NODES; i++) {
    node_marks[i] = lv_obj_create(map_canvas);
    lv_obj_set_size(node_marks[i], 10, 10);
    lv_obj_set_style_radius(node_marks[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(node_marks[i], lv_color_hex(0xFFA030), 0);   // orange = other nodes
    lv_obj_set_style_border_color(node_marks[i], lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(node_marks[i], 1, 0);
    lv_obj_add_flag(node_marks[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(node_marks[i], 8);   // fingertip-sized hit box
    lv_obj_add_event_cb(node_marks[i], node_mark_cb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
    lv_obj_add_flag(node_marks[i], LV_OBJ_FLAG_HIDDEN);

    node_labels[i] = lv_label_create(map_canvas);
    lv_label_set_text(node_labels[i], "");
    lv_label_set_long_mode(node_labels[i], LV_LABEL_LONG_DOT);
    lv_obj_set_width(node_labels[i], 90);
    lv_obj_set_style_text_color(node_labels[i], lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(node_labels[i], lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(node_labels[i], LV_OPA_70, 0);
    lv_obj_set_style_radius(node_labels[i], 4, 0);
    lv_obj_set_style_pad_hor(node_labels[i], 3, 0);
    lv_obj_set_style_pad_ver(node_labels[i], 1, 0);
    lv_obj_add_flag(node_labels[i], LV_OBJ_FLAG_HIDDEN);
  }

  marker = lv_obj_create(map_canvas);
  lv_obj_set_size(marker, 12, 12);
  lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(marker, lv_color_hex(0x58B4FF), 0);
  lv_obj_set_style_border_color(marker, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(marker, 2, 0);
  lv_obj_remove_flag(marker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);

  status_lbl = lv_label_create(map_canvas);
  lv_label_set_text(status_lbl, "");
  lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x8FA3BF), 0);
  lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, 0);

  zoom_lbl = lv_label_create(map_canvas);
  lv_label_set_text(zoom_lbl, "");
  lv_obj_set_style_text_color(zoom_lbl, lv_color_hex(0xE8ECF2), 0);
  lv_obj_align(zoom_lbl, LV_ALIGN_TOP_LEFT, 4, 4);

  makeMapBtn(map_canvas, LV_SYMBOL_PLUS, zoom_cb, (void*)(intptr_t)+1, LV_ALIGN_BOTTOM_RIGHT, -4, -40);
  makeMapBtn(map_canvas, LV_SYMBOL_MINUS, zoom_cb, (void*)(intptr_t)-1, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
  makeMapBtn(map_canvas, LV_SYMBOL_GPS, locate_cb, NULL, LV_ALIGN_BOTTOM_LEFT, 4, -4);
  lv_obj_t* nb = makeMapBtn(map_canvas, LV_SYMBOL_EYE_OPEN, night_cb, NULL, LV_ALIGN_BOTTOM_LEFT, 4, -40);
  night_btn_lbl = lv_obj_get_child(nb, 0);
  mapNightLoad();
  mapApplyNight();

  mapViewRefresh();
}

bool mapViewSdOk() { return sd_ok; }
