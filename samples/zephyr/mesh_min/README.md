# MeshCore minimal sample

Brings up the MeshCore mesh engine on Zephyr using the bundled platform helpers
and a loopback radio. No external LoRa hardware or extra west modules required —
this is the smoke test that the core builds and runs on a Zephyr target.

## Build & run (native_sim)

```console
west build -b native_sim samples/zephyr/mesh_min
west build -t run
```

Expected console output:

```
MeshCore minimal sample: mesh engine up
```

## What it does

- Instantiates `mesh::Mesh` with:
  - `LoopbackRadio` — accepts sends, never receives;
  - `ZephyrMillisClock`, `ZephyrRNG`, `ZephyrRTCClock`, from
    `ports/zephyr/MeshCoreZephyr.*`;
  - a 16-slot `StaticPoolPacketManager` and `SimpleMeshTables`.
- Generates a random `LocalIdentity`, calls `begin()`, then runs `loop()`.

See `../../../zephyr/README.md` for how to turn this into a real, radio-backed
role (repeater, room server, sensor, …).
