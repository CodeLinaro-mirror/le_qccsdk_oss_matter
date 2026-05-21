# QCC730MI Matter All-Clusters Application

Matter SDK v1.5 all-clusters application for the Qualcomm QCC730MI SoC
(Cortex-M33 @80 MHz, Zephyr RTOS, WiFi-only, no BLE/Thread).

## Build

```bash
# From connectedhomeip/examples/all-clusters-app/qcom/qcc730/:
source setup_env.sh
./build.sh
# Output: build/zephyr/zephyr_HASHED.elf
```

## Key Files

| File | Purpose |
|---|---|
| `CMakeLists.txt` | Zephyr build entry point |
| `prj.conf` | All Zephyr Kconfig settings |
| `chip-module/CMakeLists.txt` | GN↔CMake bridge, HW crypto flags |
| `boards/qcc730mi.overlay` | NVS partition (64 KB at 0x3F0000) |
| `qcc730mi-mbedtls-perf.h` | mbedTLS ECC SW performance overrides |
| `main/CHIPProjectConfig.h` | MRP timers, event queue size |
| `main/AppTask.cpp` | Matter server init, commissioning |
| `main/main.cpp` | Entry point, `matter reset` shell command |

## Features

- Matter on-network commissioning over WiFi (PASE + CASE)
- QCC730 PKA hardware acceleration for P-256 ECC (~47–130× faster)
- Commissioning time: ~5 s (vs ~55 s with SW-only ECC)
- OnOff cluster, NetworkCommissioning, GeneralCommissioning
- Shell: `wifi connect`, `net iface`, `matter reset`
- NVS: 64 KB partition, 16 sectors (no GC during commissioning)

## Commissioning

```bash
uart:~$ matter reset          # clear stale NVS (required after re-flash)
uart:~$ wifi connect -s SSID -p PASSWORD -k 2
# host:
./chip-tool pairing onnetwork-long 1 20202021 3840 --bypass-attestation-verifier 1
./chip-tool onoff toggle 1 1 --timeout 30
```
