# UI Design Notes

Quick reference for TEAM app UI/UX decisions.

## Design Principles

**Core Principle**: **SIMPLICITY**
- Clear, uncluttered interface
- Essential features immediately accessible
- Minimal taps to common actions
- No feature creep - focused on hunting/SAR needs

## Color Coding System

### Marker Colors (User Status)
Visual indication of connection quality and recency:

- 🟢 **Green** = Direct contact
  - User heard you directly (no mesh hops)
  - Within direct radio range
  - Best signal quality
  
- 🟡 **Yellow** = Via mesh
  - User received via multi-hop routing
  - May be out of direct range but connected
  - Still reliable communication
  
- 🔴 **Red** = Stale
  - Haven't heard from user in X seconds (configurable, default 5 minutes)
  - May be out of range or device off
  - Position shown is last known

### Message Status Indicators
- **Sending...** - Message being transmitted
- **Sent ✓** - Successfully transmitted to radio
- **Delivered (N)** - Received by N devices
  - Green badge for direct receivers
  - Yellow badge for mesh receivers

## Map Features

### User's Own Marker
```
      ▲ (pointing in compass direction)
     ╱ ╲
    ╱   ╲
   ╱  🔵 ╲  (blue for self, rotates with compass)
  ╱       ╲
 ╱_________╲
```
- Always centered or easily re-centered
- Shows compass heading (arrow points where you're facing)
- Blue color to distinguish from other users
- Tap to recenter map

### Distance Rings
```
     ╭─────╮
    ╱       ╲
   │    🔵   │  500m
    ╲       ╱
     ╰─────╯
      ╱   ╲
     │  🔵  │  1000m
      ╲   ╱
```
- Concentric circles around user
- Default: 500m increments
- Configurable in settings (250m, 500m, 1km, etc.)
- Toggle on/off
- Helps estimate distances to other users/landmarks

### Other User Markers
- Color-coded by status (green/yellow/red)
- Shows name/callsign
- Tap for details:
  - Last seen time
  - Round-trip time (if recent message)
  - Hop count
  - Signal strength estimate

## Map Styles (User Selectable)

Like ATAK, user can choose:

1. **Street Map** (default)
   - OpenStreetMap standard
   - Good for urban/suburban
   
2. **Satellite**
   - High-resolution imagery
   - Best for unfamiliar terrain
   
3. **Topographic**
   - Contour lines, elevation
   - Critical for hunting in hills/mountains
   - USGS Topo maps
   
4. **Terrain**
   - Shaded relief
   - Shows elevation without contour clutter

## Screen Layout Ideas

### Option A: Map-First (Recommended)
```
┌─────────────────────────────┐
│  [☰] TEAM      [⚙️] [🔋80%] │  <- Top bar
├─────────────────────────────┤
│                             │
│         MAP VIEW            │
│                             │
│    🟢 Alice    🟡 Bob       │
│                             │
│         🔵 (You)            │
│      ╭─────500m─────╮       │
│     ╱               ╲       │
│    │                 │      │
│     ╲     🔴 Charlie ╱       │
│      ╰──────────────╯       │
│                             │
├─────────────────────────────┤
│ [💬 3] [📍] [⊕] [👥]        │  <- Bottom nav
└─────────────────────────────┘

[💬 3] = Messages (with unread count)
[📍] = Waypoints (Version 2)
[⊕] = Quick message/status
[👥] = User list
```

### Option B: Tab-Based
```
┌─────────────────────────────┐
│ [MAP] [CHAT] [TEAM] [MORE]  │  <- Tabs
├─────────────────────────────┤
│                             │
│     (Current tab content)   │
│                             │
└─────────────────────────────┘
```

**Recommendation**: Option A (Map-First)
- Map is primary for hunting
- Messages accessible via badge
- One-tap to all features

## Quick Actions

### Long-Press Map
- Create waypoint (Version 2)
- Send location to chat
- Measure distance

### Swipe Gestures
- Swipe up from bottom: Open messages
- Swipe down from top: Connection status
- Two-finger rotate: Rotate map to compass heading

## Settings (Keep Minimal)

### Map Settings
- Map style (street/satellite/topo/terrain)
- Distance rings (on/off, interval)
- Show user names (on/off)
- Map rotation (compass/north-up)

### Connection Settings
- BLE device selection
- Auto-reconnect
- Companion radio PIN

### Position Settings
- Share location (on/off)
  - Public channel: User controlled (default OFF)
  - Private channels: Always ON (disabled/grayed out)
- Broadcast interval (60s / 120s / 300s)
- Location accuracy threshold

### Notification Settings
- Message notifications
- New user detected
- User went stale
- Battery warnings

### Display Settings
- Stale timeout (2min / 5min / 10min / 30min)
- Keep screen on
- Dark mode

## Notification Priorities

### Critical (sound + vibrate)
- Direct message received
- Emergency status from user

### Important (vibrate)
- New user detected in range
- User went stale (red)

### Low (silent)
- Position update
- Message delivered confirmation

## Accessibility

- Large, tappable markers (minimum 48dp)
- High contrast colors (green/yellow/red work for most color blindness)
- Text labels on all markers
- Haptic feedback for important actions

## Future UI Considerations (Maybe Later)

- Split-screen (map + messages side-by-side on tablets)
- Picture-in-picture map while chatting
- AR compass overlay
- 3D terrain view
- Historical tracks/breadcrumbs
- Custom marker icons per user
