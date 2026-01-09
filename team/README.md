# TEAM - Tactical Emergency Area Messaging

Android application for position tracking and messaging over MeshCore LoRa mesh network.

## Overview

TEAM is a hunting/tactical communication app designed for use when traditional networks are unavailable. It leverages MeshCore's LoRa mesh network to provide:

- Real-time position sharing
- Text messaging (public & private channels)
- Offline map support
- Waypoint creation and sharing
- Message delivery tracking with ACK confirmation
- Direct contact detection for range management

## Target Use Cases

- Hunting expeditions
- Search and Rescue (SAR) operations
- Outdoor recreation in remote areas
- Emergency communication when cellular is unavailable
- Tactical operations requiring off-grid communication

## Hardware Requirements

### Development Hardware
- LilyGO T-Beam (ESP32) - for initial development and testing

### Target Hardware  
- Heltec T114 V2 (nRF52840) - power-efficient for extended field use

### Mobile Device
- **Android**: 8.0+ (API 26+) - Primary development platform
- **iOS**: Support can be added later (see [docs/IOS_SUPPORT.md](docs/IOS_SUPPORT.md))
- Bluetooth LE support
- GPS capability

## Project Structure

```
team/
├── README.md              # This file
├── DEVELOPMENT_PLAN.md    # Detailed development roadmap
├── docs/                  # Additional documentation
│   ├── ARCHITECTURE.md    # System architecture and design
│   ├── BLE_PROTOCOL.md    # BLE protocol quick reference
│   └── IOS_SUPPORT.md     # iOS compatibility considerations
└── app/                   # Android application (to be created)
```

## Development Status

🔴 **Pre-Development** - Project planning and documentation phase

See [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) for detailed implementation roadmap.

## Key Features

### Phase 1: Foundation
- BLE connection to MeshCore companion radio
- Basic packet communication
- Connection management

### Phase 2: Messaging  
- Text messaging with delivery tracking
- Public and private channels
- ACK-based confirmation showing which devices received messages
- Direct contact identification

### Phase 3: Mapping
- Offline map storage
- Current location display
- Map tile caching

### Phase 4: Position Sharing
- Broadcast location to mesh network
- Display team members on map
- Real-time position updates

### Phase 5: Waypoints
- Create and share waypoints
- Display on map
- Channel-based sharing

### Phase 6: Polish
- Battery optimization
- Error handling
- Field testing

## License

Part of the MeshCore project - see parent directory for license information.

## Contributing

This is a companion application to MeshCore. Development follows the MeshCore contribution guidelines.
