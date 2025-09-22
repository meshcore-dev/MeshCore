#pragma once

// Forward declaration – prevents include cascades.
struct NodePrefs;

// Global pointer to YOUR single NodePrefs instance.
extern NodePrefs* g_nodePrefs;

// Convenient getters:
inline NodePrefs& prefs()       { return *g_nodePrefs; }
inline const NodePrefs& cprefs(){ return *g_nodePrefs; }
