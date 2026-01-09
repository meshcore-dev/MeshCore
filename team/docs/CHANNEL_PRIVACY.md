# Channel & Privacy Design

## Channel Types

### Public Channel
**Purpose**: Open communication with all nearby users

**Characteristics**:
- Default channel, always available
- Known shared key: `izOH6cXN6mrJ5e26oRXNcg==`
- All MeshCore devices can decrypt messages
- Anyone in radio range can see messages

**Location Sharing**:
- ⚠️ **Optional** - User chooses whether to share location
- **Default: OFF** (for privacy)
- Toggle in channel settings
- Use case: Share location only when actively coordinating, hide when hunting

**Privacy Considerations**:
- Anyone with a MeshCore device can see your messages
- If location sharing ON: Anyone can see where you are
- Good for: General coordination, questions, non-sensitive info
- Avoid: Specific hunting locations, sensitive tactical info

---

### Private Channels
**Purpose**: Secure communication with trusted team members

**Characteristics**:
- Requires shared encryption key (16 bytes)
- Only devices with the key can decrypt messages
- Created by one user, shared with team
- Multiple private channels possible (e.g., "Hunt Group", "Family", "SAR Team")

**Location Sharing**:
- ✅ **Required** - Always ON
- Cannot be disabled
- Location is encrypted with channel key
- Only channel members can see your position

**Privacy Considerations**:
- Messages and location encrypted end-to-end
- Only trusted team members with key can see
- Safer for tactical/hunting coordination
- Share channel keys carefully (in-person, QR code)

---

## Location Sharing Matrix

| Channel Type | Location Sharing | User Control | Default |
|--------------|------------------|--------------|---------|
| Public | Optional | Toggle ON/OFF | OFF |
| Private | Required | Always ON | ON (locked) |

---

## UI Design for Privacy

### Channel Switcher
```
┌─────────────────────────────┐
│ Active Channel:             │
│                             │
│ ○ Public                    │
│   📍 Location: OFF ⚠️       │ <- Privacy indicator
│                             │
│ ● Hunt Group (Private)      │
│   📍 Location: ON ✓         │ <- Always ON
│                             │
│ ○ SAR Team (Private)        │
│   📍 Location: ON ✓         │
│                             │
│ [+ Create Channel]          │
└─────────────────────────────┘
```

### Location Sharing Toggle (Public Channel Only)
```
┌─────────────────────────────┐
│ Public Channel Settings     │
├─────────────────────────────┤
│                             │
│ Share My Location           │
│ [────────────○] OFF         │
│                             │
│ ⚠️  Warning: When OFF, your │
│    team cannot see where    │
│    you are on the map       │
│                             │
│ ✓  Tip: Use private channels│
│    for location sharing with│
│    your trusted team        │
│                             │
└─────────────────────────────┘
```

### Private Channel Settings (Location Always ON)
```
┌─────────────────────────────┐
│ Hunt Group Settings         │
├─────────────────────────────┤
│                             │
│ Share My Location           │
│ [────────────●] ON          │
│                             │
│ 🔒 Location sharing is      │
│    required for private     │
│    channels and cannot be   │
│    disabled                 │
│                             │
│ Your location is encrypted  │
│ and only visible to channel │
│ members                     │
│                             │
└─────────────────────────────┘
```

### Status Bar Indicator
```
Top of screen:
[🔒 Hunt Group] [📍 Sharing] [🔋 80%]
  ^channel      ^location    ^battery
                  status
```

When on public with location OFF:
```
[📢 Public] [📍 Hidden] [🔋 80%]
```

---

## User Workflows

### Scenario 1: Solo Hunter (Privacy Mode)
1. Stay on **Public channel**
2. Keep **Location OFF** (default)
3. Can send/receive messages
4. Others don't know where you are
5. Switch to private channel only when coordinating with known partners

### Scenario 2: Hunt Group Coordination
1. Create **Private channel** before trip
2. Share channel key with hunting partners (QR code)
3. Everyone joins private channel
4. **Location automatically shared** within group
5. Can see each other on map
6. Switch to public for general questions without revealing location

### Scenario 3: SAR Operation
1. SAR team has **Private channel**
2. All team members' locations visible
3. Public channel for general coordination
4. Public location OFF to avoid civilian confusion
5. Only SAR team sees actual positions

---

## Implementation Details

### Device-Level Setting
The companion radio has `advert_loc_policy` in NodePrefs:
- `ADVERT_LOC_NONE = 0` - Don't broadcast location
- `ADVERT_LOC_SHARE = 1` - Broadcast location

**TEAM App Logic**:
```kotlin
fun shouldBroadcastLocation(currentChannel: Channel): Boolean {
    return when {
        currentChannel.isPublic -> currentChannel.shareLocation  // User choice
        else -> true  // Private channels always share (required)
    }
}

fun updateLocationBroadcast(enabled: Boolean) {
    if (enabled) {
        // Send CMD_SET_ADVERT_LATLON with current position
        // Then CMD_SEND_SELF_ADVERT to broadcast
        setDeviceAdvertPolicy(ADVERT_LOC_SHARE)
    } else {
        // Only allowed on public channel
        if (currentChannel.isPublic) {
            setDeviceAdvertPolicy(ADVERT_LOC_NONE)
        }
    }
}
```

### Channel Switching Logic
```kotlin
fun switchToChannel(channel: Channel) {
    currentChannel = channel
    
    // Update location sharing based on new channel
    val shouldShare = shouldBroadcastLocation(channel)
    updateLocationBroadcast(shouldShare)
    
    // Update UI indicator
    updateLocationSharingUI(shouldShare, channel.isPublic)
}
```

### Database Schema
```kotlin
@Entity(tableName = "channels")
data class ChannelEntity(
    @PrimaryKey val hash: Byte,
    val name: String,
    val sharedKey: ByteArray,
    val isPublic: Boolean,
    
    // Location sharing preference
    // For public: user preference (default false)
    // For private: always true (enforced in code)
    val shareLocation: Boolean,
    
    val createdAt: Long
)
```

---

## Privacy Best Practices

### For Users
1. **Default to Privacy**: Keep public location OFF unless actively coordinating
2. **Use Private Channels**: For your hunting/SAR team
3. **Share Keys Securely**: In-person or via secure method (not public channel!)
4. **Review Settings**: Before each trip, check which channel is active
5. **Location Awareness**: Status bar shows if location is being shared

### For Developers
1. **Privacy by Default**: Public location OFF out of the box
2. **Clear Indicators**: Always show location sharing status
3. **Warnings**: Alert when switching to public with location ON
4. **No Surprises**: Private channels clearly state location is required
5. **Encryption**: Private channel locations encrypted with channel key

---

## Future Enhancements

- **Geofencing**: Auto-switch location sharing based on area (e.g., ON when in hunting zone)
- **Time-based**: Auto-disable location sharing after X hours
- **Emergency Override**: Emergency button broadcasts location on all channels
- **Location Zones**: Share location only when within certain radius of team
