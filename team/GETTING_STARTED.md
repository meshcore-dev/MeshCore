# TEAM Development Guide

## Getting Started

This guide will help you set up and start developing the TEAM Android app.

## Prerequisites

1. **Android Studio** (latest version)
   - Download: https://developer.android.com/studio
   - Include Android SDK Platform 34

2. **Java Development Kit (JDK) 17**
   - Usually bundled with Android Studio

3. **Android Device or Emulator**
   - Physical device: Android 8.0+ (API 26+)
   - Emulator: Create AVD with API 26+

4. **MeshCore Hardware** (for testing)
   - LilyGO T-Beam (development)
   - Flashed with companion firmware
   - BLE enabled

## Setup Steps

### 1. Open Project in Android Studio

```bash
cd C:\Users\Mackintosht\Documents\MeshCore\team
# Open this folder in Android Studio
```

### 2. Sync Gradle

Android Studio should automatically prompt to sync Gradle. If not:
- Click **File → Sync Project with Gradle Files**
- Wait for dependencies to download

### 3. Enable Developer Options on Device

On your Android phone:
1. Go to **Settings → About Phone**
2. Tap **Build Number** 7 times
3. Go back to **Settings → Developer Options**
4. Enable **USB Debugging**

### 4. Run the App

1. Connect your device via USB
2. Click the **Run** button (green play icon) in Android Studio
3. Select your device
4. App should install and launch

## Project Architecture

### MVVM + Clean Architecture

```
UI Layer (Compose)
    ↓
ViewModel
    ↓
Repository
    ↓
Data Sources (BLE, Database, Location)
```

### Package Structure

- **data**: Data sources, BLE, database, repositories
- **domain**: Business logic, models, use cases
- **ui**: Compose screens, ViewModels, theme
- **service**: Background services

## Development Workflow

### Phase 1: BLE Connection

Current focus area. To implement:

1. **BLE Scanner** (`data/ble/BleScanner.kt`)
   - Scan for MeshCore devices
   - Filter by service UUID

2. **BLE Connection Manager** (`data/ble/BleConnectionManager.kt`)
   - Connect/disconnect
   - Handle pairing
   - Manage connection state

3. **Frame Handler** (`data/ble/FrameHandler.kt`)
   - Encode commands to binary
   - Decode responses from binary
   - Queue management

4. **Connection Screen** (`ui/screen/connection/ConnectionScreen.kt`)
   - Show available devices
   - Connect button
   - Connection status

## Testing BLE

### Without Hardware (Development)

Create mock BLE data for testing UI:

```kotlin
// In ViewModel
val mockConnectionState = MutableStateFlow(ConnectionState.Connected)
val mockDeviceName = "T-Beam-1234"
```

### With T-Beam Hardware

1. Flash companion firmware to T-Beam
2. Configure BLE PIN (default 123456)
3. Power on T-Beam
4. Open TEAM app
5. Scan for devices
6. Connect using PIN

## Debugging

### Enable Logging

Timber is configured for debug builds. View logs:

```bash
adb logcat -s TEAM
```

### BLE Debugging

Use **nRF Connect** app on another device to:
- Verify T-Beam is advertising
- Check service UUIDs
- Test characteristics

### Common Issues

**"Cannot resolve symbol..."**
- File → Invalidate Caches → Invalidate and Restart

**Gradle sync failed**
- Check internet connection
- Clear Gradle cache: `./gradlew clean`

**App crashes on launch**
- Check Logcat for stack trace
- Verify permissions in manifest

## Code Style

- Follow Kotlin style guide
- Use meaningful variable names
- Comment complex BLE protocol logic
- Keep functions small and focused

## Git Workflow

```bash
# Create feature branch
git checkout -b feature/ble-scanner

# Make changes, test
git add .
git commit -m "Implement BLE scanner with service UUID filtering"

# Push
git push origin feature/ble-scanner
```

## Next Steps

1. Implement `BleScanner.kt`
2. Implement `BleConnectionManager.kt`
3. Create connection UI screen
4. Test with T-Beam

See `DEVELOPMENT_PLAN.md` for full roadmap.

## Resources

- [Android Developer Docs](https://developer.android.com)
- [Jetpack Compose](https://developer.android.com/jetpack/compose)
- [BLE Guide](https://developer.android.com/guide/topics/connectivity/bluetooth/ble-overview)
- [osmdroid Wiki](https://github.com/osmdroid/osmdroid/wiki)
- [MeshCore Docs](../docs/)
