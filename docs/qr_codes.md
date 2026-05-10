# QR Codes

This document provides an overview of QR Code formats that can be used for sharing MeshCore channels and contacts. The formats described below are supported by the MeshCore mobile app.

## Add Channel

**Example URL**:

```
meshcore://channel/add?name=Public&secret=8b3387e9c5cdea6ac9e5edbaa115cd72
```

**Parameters**:

- `name`: Channel name (URL-encoded if needed)
- `secret`: 16-byte secret represented as 32 hex characters

## Add Contact

**Example URL**:

```
meshcore://contact/add?name=Example+Contact&public_key=9cd8fcf22a47333b591d96a2b848b73f457b1bb1a3ea2453a885f9e5787765b1&type=1
```

**Parameters**:

- `name`: Contact name (URL-encoded if needed)
- `public_key`: 32-byte public key represented as 64 hex characters
- `type`: numeric contact type
    - `1`: Companion
    - `2`: Repeater
    - `3`: Room Server
    - `4`: Sensor
