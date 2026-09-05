# Changelog

All notable changes to this project will be documented in this file.

## [0.2.4] - Fixes

### Fixed

- Binary payloads truncated by strcpy and switched to memcpy [18](https://github.com/theelims/PsychicMqttClient/pull/18)
- Fixed broken link to Adafruit cert repository

### Changed

- Replaced `mqtt.eclipseprojects.io` with `broker.hivemq.com`
- Replaced debug tag with 🐙

## [0.2.3] - Various Fixes and Certificate Validation

### Fixed

- Issue [16](https://github.com/theelims/PsychicMqttClient/issues/16) was fixed.

### Added

- PR [17](https://github.com/theelims/PsychicMqttClient/pull/17) adds functionality to authenticate via client key + certificate.

## [0.2.2] - Various Fixes

### Fixed

- Issue [14](https://github.com/theelims/PsychicMqttClient/issues/14) was fixed.
- Issue [13](https://github.com/theelims/PsychicMqttClient/issues/13) was fixed.

### Added

- The function `forceStop()` was added to address issue [12](https://github.com/theelims/PsychicMqttClient/issues/12).

## [0.2.1] - Fixing Wildcard Behavior

### Changed

- Issue [6](https://github.com/theelims/PsychicMqttClient/issues/6) was fixed.

### Added

- Example for wildcards in topics.

## [0.2.0] - Compatible with Arduino 3 / ESP-IDF 5

### Added

- CI workflow
- ESP-IDF 5 compatibility

### Changed

- updated library.json

## [0.1.1] - Stability Fixes

### Fixed

- Fixed subscribe error when not connected
- Freezing of event-loop when disconnect() is called while the MQTT server is not connected

## [0.1.0] - Initial Release
