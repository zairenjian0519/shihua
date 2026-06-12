# IEC104 Slave Automated Tests

This pytest suite implements the main cases from `IEC104_Slave_测试计划与测试用例.md`.
It uses a raw TCP IEC104 client instead of the ProIEC104Client GUI, and can optionally
control the Linux target over SSH for build/start/diag/pcap workflows.

## Default Target

```text
host: 192.168.159.209
ssh user/password: sps / sps
project dir: /home/sps/shihua/104_slave
IEC104 port: 2404
diag port: 24040
CA: 1
```

## Install

Base protocol tests only need pytest:

```bash
py -m pip install -r tests/iec104_auto/requirements.txt
```

Remote management is optional. Install it only when using `--manage-slave`,
`--capture-pcap`, or tests marked `active`. Python 3.10+ is recommended on Windows:

```bash
py -m pip install -r tests/iec104_auto/requirements-remote.txt
```

## Run

Run against an already started slave:

```bash
py -m pytest tests/iec104_auto -v --slave-host 192.168.159.209 --ca 1 -m "link or standard or negative"
```

If setup fails with `slave closed the connection during STARTDT`, verify the target first:

```bash
ssh sps@192.168.159.209
cd /home/sps/shihua/104_slave
ps -ef | grep iec104_slave
ss -lntp | grep 2404
tail -100 run.log
```

Also close ProIEC104Client/Wireshark-driven client sessions before pytest if
`max_open_connections` is `1`, because the slave may accept then close a second master connection.

Build and restart the Linux slave over SSH, then run all tests:

```bash
py -m pytest tests/iec104_auto -v --manage-slave --slave-host 192.168.159.209 --ssh-user sps --ssh-pass sps --project-dir /home/sps/shihua/104_slave
```

Run active upload tests, which require SSH/diag access:

```bash
py -m pytest tests/iec104_auto -v --manage-slave -m "active"
```

Capture pcap and collect artifacts:

```bash
py -m pytest tests/iec104_auto -v --manage-slave --capture-pcap --artifacts artifacts/iec104_auto
```

Artifacts:

```text
artifacts/iec104_auto/
  iec104_test.pcap
  slave_run.log
```

## Known XFail Cases

Some tests are marked `xfail` to keep known protocol gaps visible:

- Standard general interrogation currently appends custom/counter data.
- Active YX currently sends untimed `0x01` before timed `0x1E` SOE.
- Rejected control/setpoint responses currently return `0x0A` with negative=false.

These are expected differences until the protocol interpretation or implementation is changed.

## Coverage

- Link layer: STARTDT, TESTFR, STOPDT, I-frame sequence checks.
- Standard services: `0x64`, `0x65`, `0x67`.
- Control/setpoint: `0x2D`, `0x31` select, execute, and out-of-range cases.
- Custom calls: `0x88..0x95` ACK, data, finish, and IOA range checks.
- Active upload: diag-driven YX/YC upload checks for `0x1E/0x23`.
- Negative cases: unknown TypeID, wrong COT, wrong CA/IOA.

## Deep Packet Checks

The codec parses information objects for:

- `0x01`: single point, SIQ value and quality.
- `0x0B`: scaled measured value, SVA and QDS.
- `0x0F`: integrated total, BCR value and status byte.
- `0x1E`: single point with CP56Time2a.
- `0x23`: scaled measured value with CP56Time2a.
- `0x25`: short floating point measured value with CP56Time2a.
- Custom word-array payloads used by `0x89..0x95`.

The test suite now checks:

- I-frame sequence gaps or duplicates in collected response streams.
- VSQ count versus parsed payload object count.
- Exact IOA coverage for demo point-table ranges such as `0x0001..0x1000`,
  `0x4001..0x5000`, and `0x6401..0x6600`.
- Extra IOAs outside the expected ranges.
- Representative default demo values, for example `0x4001=10`,
  `0x6401=1000`, `0x1001=100`, `0x5401=1524`.
- Diag-injected values read back by interrogation, for example YC `0x4002=456`
  and DD `0x6401=123456`.
