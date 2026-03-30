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
* Tesla Model 3 / Y rear drive units 1120980-00-G, 1120990-00-G (PMSM, TMS320F28377D)
* Texas Instruments LAUNCHXL-F28379D development board (used as JTAG probe and for development)

### JTAG Connection

Connect using the LAUNCHXL-F28379D as an external XDS100v2 probe. See [JTAG cable documentation](docs/Tesla-M3-JTAG-cable.md).

**Critical**: Always use `Load RAM only` in CCS flash settings. Writing to flash will erase Tesla firmware.

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
* [x] Tesla M3 oil pump control via LIN bus
* [x] Build compatibility with TI C2000 compiler 25.11.0.LTS
* [x] WSL2/Ubuntu build environment support
* [x] First hardware validation on Tesla FDU inverter (RAM boot via JTAG)
* [x] PMIC initialization confirmed on hardware
* [x] Gate driver initialization confirmed on hardware  
* [x] Main loop executing with LED heartbeat on hardware

* [ ] CAN firmware upgrade over openinverter CAN protocol
* [ ] High Voltage InterLock (HVIL) support
* [ ] Front drive unit (SINE/induction) hardware validation
* [ ] Vehicle integration testing

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