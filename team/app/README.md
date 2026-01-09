# TEAM Android App

Android application for TEAM (Tactical Emergency Area Messaging) - a companion app for MeshCore LoRa mesh network.

## Project Structure

```
app/
├── build.gradle.kts           # App-level build configuration
├── src/main/
│   ├── AndroidManifest.xml    # App permissions and components
│   └── java/com/meshcore/team/
│       ├── TeamApplication.kt # Application class
│       ├── MainActivity.kt    # Main entry point
│       ├── data/              # Data layer
│       │   ├── ble/          # BLE communication
│       │   ├── database/     # Room database
│       │   └── repository/   # Data repositories
│       ├── domain/           # Business logic
│       │   └── model/        # Domain models
│       ├── ui/               # Presentation layer
│       │   ├── screen/       # UI screens (Compose)
│       │   └── theme/        # App theme
│       └── service/          # Background services
```

## Current Phase: Phase 1 - Foundation

### Completed
- ✅ Project setup with Gradle
- ✅ Jetpack Compose UI framework
- ✅ Basic app structure
- ✅ BLE constants and protocol definitions
- ✅ Android permissions configured

### Next Steps
1. Implement BLE scanning and connection
2. Frame encoding/decoding
3. Command/response handling
4. Connection state management

## Dependencies

- **Kotlin** 1.9.21
- **Jetpack Compose** (Material 3)
- **Room Database** 2.6.1
- **DataStore** (preferences)
- **osmdroid** 6.1.18 (maps)
- **Timber** 5.0.1 (logging)
- **WorkManager** 2.9.0 (background tasks)
- **Location Services** 21.1.0

## Building

Open in Android Studio and sync Gradle, or use command line:

```bash
./gradlew assembleDebug
```

## Running

Connect Android device or start emulator, then:

```bash
./gradlew installDebug
```

## License

Part of the MeshCore project.
