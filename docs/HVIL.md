# HVIL software protection — 5.15.R

This implements C2000 software protection. It has not been validated on an inverter and does not establish vehicle suitability.

## Behaviour

The ADC motor interrupt is raised after ADCA SOC2 completes, so it reads the current HVIL conversion before running either motor controller. One ADC count is 0.1875 mA. The current is checked against persistent `hvilmin` (ID 171) and `hvilmax` (ID 172), inclusively. Defaults are 10–30 mA, provisional margins around the previously reported 13–20 mA readings. Calibrate these limits on the actual inverter; do not interpret them as validated fault thresholds.

At startup and after PWM timer reconfiguration, all phase outputs stay under a one-shot Trip-Zone force until 100 ms of valid samples have accumulated. Qualification uses the configured PWM sample frequency and rounds up. `hvilstate` (ID 2069) indicates qualifying (0), ready (1), or fault (2). Ready permits output; it does not command motor torque.

Any out-of-range sample, saturated ADC value, invalid current window, or zero sample frequency latches the fault. A single glitch therefore requires a reset; there is no debounce delay that permits continued operation with an invalid sample. In the ISR, the fault forces TZA and TZB low on all three phase ePWM modules, selects `OFF`, and posts `HVIL` as `ERROR_STOP`. Trip-Zone overrides the complementary dead-band outputs. The common C2000 output-enable function checks the interlock while preserving the previous interrupt mask, preventing an ISR from tripping between the guard and output release.

The fault remains latched when the loop closes, parameters change, errors are cleared, or PWM is reconfigured. After correcting the fault, an explicit `oic cmd reset` restarts the firmware. Startup zeroes SINE amplitude/slip and FOC d/q current commands after EEPROM loading, so saved motor commands cannot restart the motor. A closed loop must qualify again before new motor commands can produce PWM. No bypass parameter is provided.

## Portable check

From the repository root, without GoogleTest, hardware, or a running service:

```sh
c++ -std=c++98 -Wall -Wextra -Werror -Iinclude -Ilibopeninv/include test/testhvil.cpp -o /tmp/hvil-test
/tmp/hvil-test
```

Expected output: `HVIL interlock checks passed`. The check covers qualification at all three PWM frequencies, open/low/high/invalid samples, inclusive limits, malformed calibration, fault persistence, and reconfiguration. Assertions remain enabled in release builds. The CI host test step uses CTest to include this check alongside the existing unit tests.

With the normal CMake dependencies installed:

```sh
cmake -S . -B build/host-sine -G "Unix Makefiles" -DCONTROL=SINE -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-sine --target OpenInverterTest HvilInterlockTest
ctest --test-dir build/host-sine/test --output-on-failure
```

Repeat with `build/host-foc` and `-DCONTROL=FOC`. C2000 builds use `-DPLATFORM=c2000` in separate directories and require the TI compiler on `PATH`. Keep `FLASH_BUILD=OFF` for RAM images.

## Software validation — 2026-10-05

The standalone C++98 check passed, including AddressSanitizer and UndefinedBehaviorSanitizer. A temporary version with fault latching removed failed the fault-persistence assertion. Host CTest passed 35/35 tests in SINE and 44/44 in FOC. Both C2000 `inverter` RAM images compiled and linked with TI 25.11.1.LTS on macOS arm64.

Host validation used AppleClang 21 with `-DCMAKE_CXX_FLAGS="-Wno-error=unused-private-field -Wno-error=character-conversion"` for existing CAN diagnostic fields and the pinned GoogleTest headers. CMake 4 required `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` for the existing GoogleTest version. These overrides are local validation options, not changes to project warning policy. The legacy angle-reference conversion and FOC fixture calibration were corrected before the passing runs.

## Bench acceptance — pending

Record the inverter variant, calibration, PWM frequency, instrumentation, and observed results. Use RAM-only loading and an appropriate test bench; these checks must precede any hardware-validation claim.

1. Verify that an open loop at startup holds all six phase outputs low and reports `hvilstate=2`; CAN diagnostics must remain available.
2. With valid current, verify that outputs remain inhibited during qualification and that zero motor commands produce no drive after qualification.
3. Open the loop while PWM is active. Measure all six outputs at the MCU and buffer/gate path, including worst-case interrupt load. Confirm `OFF` and `HVIL`; record the measured detection-to-shutdown latency.
4. Close the loop and attempt parameter changes and restart commands. Confirm that no output returns before an explicit reset, then confirm that reset leaves torque commands zero and qualification repeats.
5. Repeat for FDU/SINE and RDU/FOC, all supported PWM frequencies, noise, and the calibrated current-window boundaries. Check the effect of missing or delayed ADC interrupts separately; portable tests do not prove behaviour during interrupt starvation.
