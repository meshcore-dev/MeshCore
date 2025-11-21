# Packet Structure

| Field           | Size (bytes)                     | Description                                               |
|-----------------|----------------------------------|-----------------------------------------------------------|
| header          | 1                                | Contains routing type, payload type, and payload version. |
| transport_codes | 4 (optional)                     | 2x 16-bit transport codes (if ROUTE_TYPE_TRANSPORT_*)     |
| path_len        | 1                                | Length of the path field in bytes.                        |
| path            | up to 64 (`MAX_PATH_SIZE`)       | Stores the routing path if applicable.                    |
| payload         | up to 184 (`MAX_PACKET_PAYLOAD`) | The actual data being transmitted.                        |

Note: see the [payloads doc](./payloads.md) for more information about the content of payload.

## Header Breakdown

bit 0 means the lowest bit (1s place)

| Bits  | Mask   | Field           | Description                                   |
|-------|--------|-----------------|-----------------------------------------------|
| 0-1   | `0x03` | Route Type      | Flood, Direct, Reserved - see below.          |
| 2-5   | `0x3C` | Payload Type    | Request, Response, ACK, etc. - see below.     |
| 6-7   | `0xC0` | Payload Version | Versioning of the payload format - see below. |

## Route Type Values

| Value  | Name                          | Description                          |
|--------|-------------------------------|--------------------------------------|
| `0x00` | `ROUTE_TYPE_TRANSPORT_FLOOD`  | Flood routing mode + transport codes |
| `0x01` | `ROUTE_TYPE_FLOOD`            | Flood routing mode (builds up path). |
| `0x02` | `ROUTE_TYPE_DIRECT`           | Direct route (path is supplied).     |
| `0x03` | `ROUTE_TYPE_TRANSPORT_DIRECT` | direct route + transport codes       |

## Payload Type Values

| Value  | Name                      | Description                                   |
|--------|---------------------------|-----------------------------------------------|
| `0x00` | `PAYLOAD_TYPE_REQ`        | Request (destination/source hashes + MAC).    |
| `0x01` | `PAYLOAD_TYPE_RESPONSE`   | Response to REQ or ANON_REQ.                  |
| `0x02` | `PAYLOAD_TYPE_TXT_MSG`    | Plain text message.                           |
| `0x03` | `PAYLOAD_TYPE_ACK`        | Acknowledgment.                               |
| `0x04` | `PAYLOAD_TYPE_ADVERT`     | Node advertisement.                           |
| `0x05` | `PAYLOAD_TYPE_GRP_TXT`    | Group text message (unverified).              |
| `0x06` | `PAYLOAD_TYPE_GRP_DATA`   | Group datagram (unverified).                  |
| `0x07` | `PAYLOAD_TYPE_ANON_REQ`   | Anonymous request.                            |
| `0x08` | `PAYLOAD_TYPE_PATH`       | Returned path.                                |
| `0x09` | `PAYLOAD_TYPE_TRACE`      | trace a path, collecting SNI for each hop.    |
| `0x0A` | `PAYLOAD_TYPE_MULTIPART`  | packet is part of a sequence of packets.      |
| `0x0B` | `PAYLOAD_TYPE_CONTROL`    | control packet data (unencrypted)             |
| `0x0C` | .                         | reserved                                      |
| `0x0D` | .                         | reserved                                      |
| `0x0E` | .                         | reserved                                      |
| `0x0F` | `PAYLOAD_TYPE_RAW_CUSTOM` | Custom packet (raw bytes, custom encryption). |

## Payload Version Values

| Value  | Version | Description                                       |
|--------|---------|---------------------------------------------------|
| `0x00` | 1       | 1-byte src/dest hashes, 2-byte MAC.               |
| `0x01` | 2       | Future version (e.g., 2-byte hashes, 4-byte MAC). |
| `0x02` | 3       | Future version.                                   |
| `0x03` | 4       | Future version.                                   |
