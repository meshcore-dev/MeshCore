#pragma once

#include <lvgl.h>
#include <helpers/SensorManager.h>

// Slippy-map viewer: renders OSM raster tiles from the microSD card
// (/sdcard/maps/{z}/{x}/{y}.png via LVGL's POSIX FS + lodepng decoder),
// with drag panning, zoom buttons, and an own-position marker from GPS.

class UITask;
void mapViewBuild(lv_obj_t* parent, SensorManager* sensors, UITask* task);
void mapViewRefresh();        // reposition/reload tiles + marker
bool mapViewSdOk();
