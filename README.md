# C2000 openinverter — Tesla Model 3/Y Drive Unit Firmware

[![Build status](../../actions/workflows/CI-build.yml/badge.svg)](../../actions/workflows/CI-build.yml)

A port of the Huebner inverter project to the [TI C2000](https://www.ti.com/microcontrollers-mcus-processors/microcontrollers/c2000-real-time-control-mcus/overview.html) family of micro-controllers. Specifically this targets the TMS320F28377D MCU found in both the Tesla Model 3 / Y front and rear drive unit inverters, allowing a completely open source solution to running Tesla Model 3 / Y drive units in non-Tesla vehicles. For other Tesla drive units, inverters from other manufacturers and DIY inverters have a look at the [openinverter project](https://openinverter.org).

**This software is not yet validated on hardware and is NOT suitable for use in vehicles at this time.**

**DO NOT attempt to use this code in Tesla Model 3 / Y drive units unless you are an experienced embedded software engineer. When developing this software it is very easy to wipe the existing Tesla software rendering the drive unit largely worthless.**

## Goals

The main goal of this firmware is to create a usable and safe control system for Tesla Model 3 / Y drive units — both front (induction motor, SINE control) and rear (PMSM, FOC control). A secondary goal is to create a more portable implementation of the openinverter algorithms that can be tested and/or simulated outside of embedded hardware platforms.

## Hardware

All six known Tesla Model 3/Y inverter variants (both front and rear drive units) use the same TMS320F28377D silicon. The firmware supports both drive unit types via a compile-time `CONTROL` flag:

* `CONTROL=FOC` — rear drive unit (PMSM, resolver-based FOC)
* `CONTROL=SINE` — front drive unit (induction motor, V/Hz with slip control)

### Confirmed Hardware

* Tesla Model 3 / Y front drive units (induction motor, TMS320F28377D)
* Tesla Model 3 / Y rear drive units 1120980-00-G, 1120990-00-G (PMSM, TMS320F28377D) — RDU validated for CAN/SDO communication
* Texas Instruments LAUNCHXL-F28379D development board (used as JTAG probe and for development)

### JTAG Connection

Connect using the LAUNCHXL-F28379D as an external XDS100v2 probe. See [JTAG cable documentation](docs/Tesla-M3-JTAG-cable.md).

**Critical**: Always use `Load RAM only` in CCS flash settings. Writing to flash will erase Tesla firmware.

## Active Development Branch

Active development is on the `portable-cpp` branch of the fork
[techn0zahrt-ctrl/c2000-inverter-M3-Y](https://github.com/techn0zahrt-ctrl/c2000-inverter-M3-Y/tree/portable-cpp).

## Status

### Completed
* [x] Port core libopeninv code to work on x86_64 and C2000 with unit tests
* [x] Hardware independent, statically virtualised Field Oriented Control / Sine PWM generation
* [x] Port and update existing unit tests and extend to verify FOC PWM generation
* [x] Fix integer overflows affecting C2000 in PWM generation, SineCore and libopeninv
* [x] Support operation on Tesla M3 rear drive unit hardware
* [x] Tesla M3 gate driver integration
* [x] Tesla M3 safety PSU/PMIC integration
* [x] Functional HV PWM generation on Tesla M3 hardware
* [x] Cross-platform resolver to digital conversion and Tesla M3 integration
* [x] Analogue sampling of phase currents for FOC
* [x] Analogue capture of other Tesla M3 signals
* [x] Tesla M3 inverter temperature monitoring
* [x] Over-current and over/under-voltage detection
* [x] Basic vehicle and throttle integration
* [x] openinverter compatible CAN support (CanMap, CanSdo, SdoCommands)
* [x] CAN control of openinverter serial parameters and commands
* [x] Storing system parameters in SPI EEPROM (Microchip 25LC256)
* [x] Tesla M3 oil pump LIN bus driver — C2000 SCIA break generation and RX validated on hardware; oil temperature, pressure, motor speed, and supply voltage spot values updating correctly; pump commanded at correct speed (1.3 A vs 7–9 A uncontrolled)
* [x] Build compatibility with TI C2000 compiler 25.11.0.LTS
* [x] WSL2/Ubuntu build environment support
* [x] First hardware validation on Tesla FDU inverter (RAM boot via JTAG)
* [x] PMIC initialization confirmed on hardware
* [x] Gate driver initialization confirmed on hardware — working with 3-attempt retry for PSU stabilization; FDU config uses single CFG4 entry applied to all gate driver chips
* [x] Main loop executing with LED heartbeat on hardware
* [x] CAN hardware driver working (GPIO4/5 for Tesla M3 RDU)
* [x] SDO communication working — parameter read/write/dumpall via oic tool
* [x] JSON parameter database transfer working (oic dumpall)
* [x] Parameter persistence via SPI EEPROM (Microchip 25LC256) working
* [x] oic tool fully compatible — dumpall, read, write, cmd save/load all functional
* [x] Debug environment working (CCS VS Code extension with source path mapping)
* [x] WSL2 CMake build with debug and release configurations
* [x] SINE firmware build working — FDU (induction motor) build verified
* [x] ADC channels corrected per hardware pin map
* [x] DC link voltage sensing (ADCB IN2) working — `udcgain` calibrated to 8.5625 dig/V
* [x] Phase current sensing corrected — Phase A on ADCA IN4, Phase B on ADCD IN2
* [x] Resolver sine/cosine corrected — Sine on ADCA IN0, Cosine on ADCB IN1
* [x] HVIL current sensing — ADCA IN5, readable via `oic read` (1 count ≈ 0.1875 mA)
* [x] 6-channel temperature mux reading — CD4051 select on GPIO30/31/32, ADC on ADCB IN3; all 6 channels reading correctly (`tmphs` = max of ch2/3/4, `tmpm` = ch5)
* [x] PWM generation verified — correct 3-phase waveforms confirmed at MCU test points
* [x] Parameter save/load via SPI EEPROM working correctly
* [x] HVIL loop hardware wiring validated — 13–20 mA confirmed via `oic read hvilcur`; HV discharge resistors disable when loop is closed
* [x] PMIC TLF35584 watchdog working correctly — startup sequence reordered so EEPROM load completes before watchdog starts, preventing timeout during `parm_load()`
* [x] PWM buffer (74245) enable confirmed working — PMIC must be in NORMAL state (DEVSTAT=0x02) for SS2 to assert and enable the buffer
* [x] `oic cmd reset` working — implemented via `_c_int00()` software restart (jumps to C runtime entry point, re-runs full initialisation without requiring hardware reset)
* [x] **Tesla Model 3 FDU (induction motor) first successful spin under openinverter firmware** — motor controlled via `oic write ampnom` and `oic write fslipspnt`; confirmed running smoothly at 1 A, 120 V DC, speed tracks slip frequency
* [x] Phase current sensing confirmed working during motor run
* [x] Motor control via `oic write ampnom` / `oic write fslipspnt` functional
* [x] CAN-based debug logging — `canlogger.cpp/h` sends ASCII text over CAN ID 0x7FF in 8-byte null-terminated frames; replaces CIO printf, eliminating JTAG dependency and DINT/EINT interrupt blocking; `tools/can_logger.py` receives and displays messages in real time
* [x] On-demand CAN logging — Python client sends start/stop/interval commands to ID 0x7FE; firmware only transmits when client is connected; auto-stops after 30 s keepalive timeout; eliminates CAN ACK blocking when no client is listening
* [x] Motor speed feedback — `Encoder::GetSpeed()` converts resolver frequency to RPM; `oic read speed` returns RPM confirmed working during motor spin
* [x] PMIC watchdog fix — startup sequence reordered so `parm_load()` completes before `PowerWatchdog::Init()` starts the 100 ms window watchdog, preventing timeout during EEPROM load
* [x] ADC trigger prescaler bug fixed — `EPWM_setADCTriggerEventPrescale` was set to `15U` causing the PWM ISR to fire at 813 Hz instead of 12.2 kHz; this produced a 15× slip frequency error requiring ~150 Hz commanded slip to achieve what should be 10 Hz, with massive reactive current and inverter overheating as symptoms
* [x] C28x word-size bug fixed in `SineCore::Atan2` — `int temp` changed to `int32_t temp` (two occurrences); on C28x `int` is 16-bit so resolver coordinates were silently truncated in the closed-loop angle calculation; the bug was hidden on STM32 where `int` is 32-bit
* [x] Tesla M3 oil pump LIN bus validated on hardware — `tmpoil`, `oilpres`, `pmprev`, `upmp` spot values confirmed updating; break generation uses baud-rate trick (9600 → 19200) with FIFO disabled during break so TXEMPTY is reliable; `SCI_performSoftwareReset` after FIFO re-enable clears stale SCIRXBUF/RXRDY/BRKDT that otherwise silently blocks the FIFO receiver
* [x] Resolver frequency calculation corrected — `UpdateRotorFrequency()` moved from scheduler timer ISR into PWM ISR, called every 1220 PWM cycles (exactly 100 ms at 12.2 kHz); eliminates timing collapse where scheduler tasks could compress multiple 100 ms windows into one call, producing absurd sample counts and frequency spikes
* [x] Resolver frequency spike protection — output clamped to 200 Hz; individual candidate values ≥ 500 Hz discarded as implausible (previous value held); low-amplitude condition holds last known angle rather than jumping to 0, preventing `UpdateTurns()` from accumulating a false step
* [x] ISR execution timing corrected — `execTicks` now measures each ISR invocation independently (was accumulating without reset); `s_maxExecTicks` tracks worst-case ISR duration; `s_adcOverflowCount` counts ADCA INT1 overflow events, confirming whether tasks are blocking ADC servicing
* [x] Extended diagnostic CAN logging — resolver window diagnostics (`absTurns`, `maxDiff`, `samples`) logged alongside ISR timing (`execTicks`, `maxExecTicks`, `overflows`) and motor speed; enables in-the-field spike diagnosis without JTAG

### In Progress
* [ ] Motor tuning under load — slip frequency and ampnom calibration at higher currents; hardware testing with fully corrected resolver timing pending
* [ ] CAN bus reliability — cable quality matters; use twisted/shielded CAN cable and verify 120 Ω termination at both ends of the bus
* [ ] Inverter heating under load — deadtime and switching loss investigation needed at higher power levels
* [ ] Vehicle control loop — throttle and direction via CAN for in-vehicle use
* [ ] Battery testing — higher current testing planned with 2× 13S LiPo in series (~96 V, high current)

### Not Yet Started
* [ ] CAN firmware upgrade over openinverter CAN protocol
* [ ] HVIL software integration (fault detection, safe-state on loop open)
* [ ] Extended hardware validation with motor running under sustained load
* [ ] Front drive unit (SINE/induction) hardware validation under load
* [ ] Vehicle integration testing

## Tools

### CAN Debug Logger (`tools/can_logger.py`)

A Python script that controls and receives debug messages from the firmware. Logging is **on-demand** — the firmware only transmits when this script is running. The script sends a start command on launch, sends keepalive commands every 5 s to maintain the connection, and sends a stop command on exit. If the script exits or loses connection, the firmware automatically stops logging after 30 s.

**Requirements:**
```
pip install python-can
```

**Usage (PCAN USB, defaults — 2 s interval):**
```
python tools/can_logger.py
```

**Faster interval (500 ms):**
```
python tools/can_logger.py --interval 500
```

**All options:**
```
python tools/can_logger.py --interface pcan --channel PCAN_USBBUS1 --bitrate 500000 --interval 2000
python tools/can_logger.py --no-keepalive   # send start once, no periodic keepalive
```

**SocketCAN (Linux / Raspberry Pi):**
```
python tools/can_logger.py --interface socketcan --channel can0
```

The script opens the adapter in normal (active) mode so the PCAN hardware sends CAN ACK bits — required for the MCU's transmit sequencing. The PEAK driver on Windows allows multiple applications (e.g. `oic` and `can_logger.py`) to share the same physical adapter simultaneously; both will receive all frames.

**Control protocol** (ID `0x7FE`): byte 0 = command (`0x01` start, `0x00` stop), byte 1 = interval in 100 ms units. **Log channel** (ID `0x7FF`): ASCII text in 8-byte null-padded frames; a null byte signals end-of-message.

**Debug output** includes: gate driver status, PMIC status registers, DC link voltage, phase currents, resolver angle, HVIL current, all 6 temperature channels, and CPU load. Compile with `-DUSE_CIO_DEBUG` to redirect output to CIO printf over JTAG instead.

## HVIL Wiring

The HVIL is a ~20 mA current loop that must be closed to satisfy the hardware safety logic. When the loop is closed, the HV discharge resistors are disabled. **The PWM output buffer (74245) is controlled separately by the TLF35584 PMIC SS2 signal — it is not gated by HVIL.**

### Confirmed loop order (bench wiring)

Current flows in this order:

```
12V supply (or PSU current-limited to 20 mA)
  │
  ├─ resistor (~560 Ω for 20 mA with 12V, or use PSU current limit)
  │
HV connector — HVIL IN
  │
HV connector — HVIL OUT
  │
LV connector — pin 23 (HVIL IN)
  │
LV connector — pin 4  (HVIL OUT)
  │
GND
```

Both the HV connector HVIL pins and LV connector pins 4/23 must be in the loop simultaneously.

### Verification

* Target current: **15–24 mA** (nominal 20 mA)
* Confirmed reading: **13–20 mA** via `oic read hvilcur`
* The parameter is updated every PWM cycle (~10 kHz) so readings are always fresh
* Unit: 1 ADC count ≈ 0.1875 mA (3.3V ref, ADCA IN5, scaling resistor network on board)
* When the loop is closed, the HV discharge resistors turn off (visible as a change in quiescent current)

## Compiling

The build process runs on Linux or WSL2 (Ubuntu). Windows and macOS are not supported.

### Install Build Tools

Install the TI C2000 compiler standalone [C2000-CGT](https://www.ti.com/tool/C2000-CGT) (tested with 25.11.0.LTS) and add it to your PATH.

Install build dependencies on Ubuntu/WSL2:
```
sudo apt-get install git gcc-arm-none-eabi cmake ninja-build lcov
```

On Fedora:
```
sudo dnf install git arm-none-eabi-gcc-cs-c++ cmake ninja-build lcov
```

### Build Process

CMake 3.20 or later required. GCC 10.x or later for host builds.

Download git submodules and build libopencm3:
```
make get-deps
```

Build and run unit tests on Linux/WSL2:
```
cmake --preset default
cmake --build build/host
cd build/host && ./test/OpenInverterTest
```

Build for C2000 (FOC — rear drive unit):
```
cmake --preset c2000
cmake --build build/c2000
```

Build for STM32F1 (verification only):
```
make get-deps
cmake --preset stm32f1
cmake --build build/stm32f1
```

## Code Composer Studio Integration

The C2000 firmware runs from RAM only. Use CCS to load and debug via JTAG.

Generate CCS project files:
```
mkdir ~/my-c2000-build-project
cd ~/my-c2000-build-project
cmake -G "Eclipse CDT4 - Ninja" -DPLATFORM=c2000 -DCMAKE_BUILD_TYPE=Release <repo_location>
```

Open CCS, select `File | Open Projects From Filesystem...` and point it at `~/my-c2000-build-project`.

When creating a debug configuration:

* Connection: `Texas Instruments XDS100v2 USB Debug Probe_0/C28xx_CPU1`
* Program: `${workspace_loc:/c2000-sine/platform/c2000/inverter/inverter}`
* Flash Settings → Download Settings: **`Load RAM only`** — failure to set this will erase Tesla firmware

## License

This software is licensed with the GPL v3 as detailed in [LICENSE](LICENSE).

Additionally some C2000 specific code in `platform/c2000/driverlib` and `platform/c2000/device_support` has this additional license:
```
Copyright (C) 2013-2021 Texas Instruments Incorporated - http://www.ti.com/
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

  Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the
  distribution.

  Neither the name of Texas Instruments Incorporated nor the names of
  its contributors may be used to endorse or promote products derived
  from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```