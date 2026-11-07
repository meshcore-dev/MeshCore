#pragma once

// Internals shared by the ui-lvgl translation units. Not part of the UITask
// interface; only the ui-lvgl sources include this.

#include "UITask.h"
#include "../MyMesh.h"
#include "target.h"
#include <helpers/TxtDataHelpers.h>
#include <SPIFFS.h>
#include "ChatStore.h"
#include "Sound.h"
#include "SettingsUI.h"

// theme
#define COL_BG       0x0A1020   // deep navy
#define COL_CARD     0x16233B   // panel/card
#define COL_ACCENT   0x58B4FF   // meshcore sky blue
#define COL_ACCENT_D 0x1F4E7A   // outgoing bubble
#define COL_TXT      0xE8ECF2
#define COL_MUTED    0x8FA3BF
#define COL_WARN     0xFFA030

#define KB_HEIGHT 160

// UI font: the icon glyphs the screens use, falling back to montserrat for text.
LV_FONT_DECLARE(fa_icons_14);

#define SYMBOL_TOWER     "\xEF\x94\x99"   /* U+F519 broadcast tower */
#define SYMBOL_ADDR_BOOK "\xEF\x8A\xB9"   /* U+F2B9 address book */

extern UITask* ui;   // singleton for LVGL callbacks
extern bool units_miles;

// UITask.cpp
int  prefReadInt(const char* path, int dflt);
void prefWriteInt(const char* path, int v);

// UIOverlays.cpp
void buildSplash();
void buildPolishOverlays();
void showNotifBanner(const uint8_t key[6], const char* from_name, const char* text);
void wizardOpen();
void aboutOpen();

// UIThread.cpp
bool threadOpen();
void qrLoad();
void qrEditorBuild();
void qrEditorOpen();

// UIContacts.cpp
void contactPathOpen(int contact_idx);

// UIRepeater.cpp
void repeaterStateLoad();
void repeaterScrShow();
void repeaterScrHide();
