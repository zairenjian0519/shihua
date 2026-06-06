# IEC104 Slave Demo

This directory contains a C implementation skeleton for the IEC 60870-5-104 slave described in
`../IEC104_Slave_软件详细设计文档.md`.

Implemented demo scope:

- `lib60870-C` based CS104 slave startup.
- JSON5-style configuration loading for the key server/APCI fields.
- Static demo point table for YX/YC/YK/YT/DD address areas.
- General interrogation response for YX/YC points.
- Counter interrogation response for DD points.
- Clock sync, connection event, raw message hooks.
- Single command (`C_SC_NA_1`) and setpoint command (`C_SE_NA_1/C_SE_NB_1/C_SE_NC_1`) handling.
- Periodic YC enqueue and shared-memory adapter stub.
- Custom TypeID recognition stub for historical YX/YC extension points.

Build:

```sh
sudo apt install -y build-essential cmake pkg-config
cmake -S . -B build
cmake --build build
```

Run:

```sh
./build/iec104_slave -c 104config.json5
```

On Windows build generators the executable may be under `build/Debug` or `build/Release`.

The shared-memory adapter and diagnostic command service are intentionally kept as separate modules so the
field data source and CLI protocol can be filled in without changing the IEC104 callback surface.

Configuration parsing:

- The loader preprocesses JSON5-style configuration into strict JSON.
- If `../json-c-master` exists, CMake builds and links that local json-c source tree.
- Otherwise, with `libjson-c-dev` installed, CMake enables the system json-c package.
- If json-c is not found, the program still builds and falls back to the legacy key scanner for the basic server/APCI fields.
