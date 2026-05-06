# Number Allocations

This document lists unique numbers/identifiers used in various MeshCore protcol payloads.

# Group Data Types

The `PAYLOAD_TYPE_GRP_DATA` payloads have a 16-bit data-type field, which identifies which application the packet is for.

To make sure multiple applications can function without interfering with each other, the table below is for reserving various ranges of data-type values. Just modify this table, adding a row, then submit a PR to have it authorised/merged.

NOTE: the range FF00 - FFFF is for use while you're developing, doing POC, and for these you don't need to request to use/allocate.

Once you have a working app/project, you need to be able to demonstrate it exists/works, and THEN request type IDs. So, just use the testing/dev range while developing, then request IDs before you transition to publishing your project.

| Data-Type range | App name                    | Contact                                              |
|-----------------|-----------------------------|------------------------------------------------------|
| 0000 - 00FF     | -reserved for internal use- |  |
| FF00 - FFFF     | -reserved for testing/dev-  |  |

(add rows, inside the range 0100 - FEFF for custom apps)
