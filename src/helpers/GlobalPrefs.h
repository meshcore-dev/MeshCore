#pragma once

// Vorwärtsdeklaration – kein Include-Rattenschwanz.
struct NodePrefs;

// Globaler Zeiger auf DEINE eine NodePrefs-Instanz.
extern NodePrefs* g_nodePrefs;

// Bequeme Getter:
inline NodePrefs& prefs()       { return *g_nodePrefs; }
inline const NodePrefs& cprefs(){ return *g_nodePrefs; }
