# XIAO ESP32-S3 + Wio-SX1262: optional peripherals

Use the standard firmware target for the device's role. Each image works with
or without the optional battery divider; the saved `battery.connected` switch
starts **off**, including with older preferences. Off returns 0 mV/unavailable
without sampling or initialising the ADC. No separate battery image is needed.

| Role / standard target | Optional features | Command access |
| --- | --- | --- |
| `Xiao_S3_WIO_repeater` | Battery, SHT4x, manual outputs | USB text console or authenticated remote administrator CLI |
| `Xiao_S3_WIO_room_server` | Same battery, SHT4x and output behaviour as repeater | USB text console or authenticated remote room administrator CLI |
| `Xiao_S3_WIO_companion_radio_ble` | Battery settings and voltage reporting | Existing phone app: Settings -> Extra Tools -> Command Line, over Bluetooth |
| `Xiao_S3_WIO_companion_radio_usb`, `Xiao_S3_WIO_companion_radio_serial`, `Xiao_S3_WIO_companion_radio_wifi` | Same companion battery support | A client's local Command Line over the corresponding framed transport |

Companion Command Line supports the `battery.connected`, `battery.gpio` and
`adc.multiplier` get/set families below. Other commands reply `Unknown command`.
The companion's normal USB connection uses framed app commands; a raw serial
text terminal is not this console. Repeater/room administration travels through
the companion to the remote node's authenticated CLI. Select the intended node's
console when configuring it.

## Enable or disable

After installing the divider, use the command access for your role listed above:

```text
set battery.connected on
get battery.connected
```

Disable sensing on nodes without the divider:

```text
set battery.connected off
```

Changes take effect immediately and are saved across restarts. Disabling keeps
any calibration for later reuse. Other board types without this optional feature
return `Error: unsupported` and retain their existing battery behaviour.

This firmware adds the saved setting and command interface. It does not add a
checkbox to the separately distributed phone app or config.meshcore.io. A client
can use `get battery.connected` to detect support and read the value, and use
`set battery.connected on/off` when its checkbox changes. Calibration controls
can be hidden while off. This setting means **external divider installed**;
a battery connected only to BAT+/BAT- is not sufficient.

## Wiring (one Li-ion/LiPo cell, 3.7 V nominal, 4.2 V maximum)

```text
Battery + / BAT+ ---- 100 kOhm ----+---- D0 / A0 / GPIO1
                                 |
                              100 kOhm
                                 |
Battery - / BAT- -----------------+---- XIAO GND
```

Keep the normal BAT+/BAT- power connections; this is an additional sensing
branch. Do not put a resistor in the battery ground return or connect BAT+
directly to the ADC. Disconnect battery and USB before wiring. This circuit
is not suitable for a multi-cell pack.

Use 1% resistors if available and a **100 nF ceramic capacitor from D0 to GND**
close to the board. The divider draws approximately 21 microamps at 4.2 V.
A full cell gives 2.1 V at the ADC; the default multiplier of **2.0** reports
4.2 V. The firmware selects 11 dB attenuation and averages four calibrated ADC
readings. No extra multiplier is needed in the app.

D0/GPIO1 is unused by the stock Wio kit's radio and I2C. Check optional expansion
boards before enabling sensing. This is the Wio B2B kit, not the differently
wired `Xiao_S3` radio target.

## Select the sensing pin (saved per node)

The board exposes nine analog-capable header pins: D0-D5 and D8-D10. This Wio
repeater supports the three straightforward free choices below. D2/GPIO3 is a
strapping pin, D4/D5 are used for I2C, and D8-D10 are used by the radio's SPI.

| Board label | GPIO number for the setting |
| --- | --- |
| D0 / A0 (default) | 1 |
| D1 / A1 | 2 |
| D3 / A3 | 4 |

Example selecting D3 (use chip GPIO numbers, not the D-label number):

```text
set battery.connected off
set battery.gpio 4
get battery.gpio
set battery.connected on
```

Install the divider on the selected pin before enabling. If moving existing
wiring, disable sensing, disconnect power, move the divider connection, then
reconnect power and select/enable the new pin. All command interfaces listed above support pin selection; the pin is saved across reboot. Selecting a pin
while sensing is off does not access the ADC or change the pin's mode.

`set battery.gpio default` selects the build default (D0/GPIO1). A missing setting
also uses that default. Invalid/reserved pins are rejected, and a saved invalid
pin prevents automatic enabling at startup. Disable sensing before changing the
pin. All supported pin choices use the same firmware image.

## Build and update

Main-branch pushes automatically build all six standard XIAO roles. Use the
[firmware downloads and USB flasher](https://squishywareofficial.github.io/MeshCore-Enhanced/)
for updates with Erase data unchecked, or fresh installations with it checked.
Application downloads remain available for manual updates. See the [build and flashing guide](../../docs/enhanced_builds.md)
for version identification, checksums and settings preservation.

```sh
pio run -e Xiao_S3_WIO_repeater
pio run -e Xiao_S3_WIO_repeater -t mergebin
```

For a companion or room, select its target instead:

```sh
pio run -e Xiao_S3_WIO_companion_radio_ble
pio run -e Xiao_S3_WIO_companion_radio_ble -t mergebin
pio run -e Xiao_S3_WIO_room_server
pio run -e Xiao_S3_WIO_room_server -t mergebin
```

Wired and unwired nodes of the same role use the same image. The application is
`.pio/build/<target>/firmware.bin`; the combined initial-installation
image is `firmware-merged.bin` in that directory. Use only the application image
for a supported application-only update flow. Do not upload a combined image through OTA.

Back up identity/settings, check partition/update compatibility, then verify
identity/settings after updating. This feature needs no factory erase. Existing
or missing settings default to sensing off; enable it explicitly on wired nodes.

## Verify and calibrate

1. Before attaching D0, measure the battery and divider midpoint: the midpoint
   should be half the battery voltage. Disconnect power before connecting D0.
2. Check `get battery.gpio` matches the wired pin. Enable `battery.connected`;
   run `get adc.multiplier` (default `2.000`).
3. For a repeater/room, run USB `stats-core` and compare `battery_mv` with a meter.
   For a companion, check the app's normal battery voltage display. Existing core
   stats and permitted battery telemetry report the same corrected reading.
4. If needed, set `new multiplier = current multiplier * meter volts / reported volts`.
   For meter 4.10 V, report 4.00 V and current 2.0, use `set adc.multiplier 2.05`.
   `set adc.multiplier 0` restores the build default. Never calculate calibration
   from a zero reading. Calibration can be configured while sensing is off.
5. Reboot and verify the switch, pin and calibration persist. Turn sensing off and
   verify the reported value is 0 mV; turn it on again to restore readings.
6. Compare readings on battery power and while charging. Test multiple battery
   voltages; a single calibration cannot remove all ADC error.

An enabled input without a divider floats and produces meaningless readings.
This does not automatically detect battery presence, charging, or capacity.
Software tests do not replace physical voltage and remote-telemetry checks.

## References

- [MeshCore #203: this kit and an external divider](https://github.com/meshcore-dev/MeshCore/issues/203)
- [Seeed XIAO ESP32-S3 pinout](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- [Espressif calibrated ADC and attenuation API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html)
- [Espressif ADC noise/capacitor guidance](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/adc.html)

## Enclosure temperature and humidity

Use one 3.3 V-compatible SHT40 breakout at I2C address 0x44: SDA to D4/GPIO5,
SCL to D5/GPIO6, supply to 3V3 and ground to GND. Disconnect power before wiring.
The B2B radio socket shares board GPIOs; it is not an independent expander.
Keep the sensor away from board/regulator heat inside the enclosure. These are
inside-enclosure readings, not outdoor air temperature. Do not share address
0x44 with an INA226 or another device.

The standard repeater and room-server images work with or without this sensor. Detection happens
at startup; restart after attaching it. `get environment` reads a fresh pair;
`get temperature` and `get humidity` each read a fresh pair and display one field.
Readings use high precision with no heater/retries. ESP32 transactions are capped
at 50 ms (a shorter existing timeout is retained); errors never return cached data.
The existing environment telemetry permissions and channel assignment remain intact.
No mobile GUI additions or periodic broadcasts are introduced.

## Manual digital outputs

The standard `Xiao_S3_WIO_repeater` and `Xiao_S3_WIO_room_server` images support
active-high outputs on chip
GPIO1 (D0), GPIO2 (D1), GPIO4 (D3), GPIO43 (D6) and GPIO44 (D7), when free.
GPIO1 is initially reserved for the battery divider, even when sensing is off.
Move the disabled battery input before assigning GPIO1. A battery input cannot
move onto an assigned output. UART/GPS/bridge and other active build pin claims
reserve their pins; USB CDC leaves GPIO43/44 available in this target.

Example: `output configure 2`, `output on 2`, `output status`, `output off 2`,
then `output remove 2`. Success returns `OK`. Only configured pins can switch.
Repeating configuration leaves an ON output ON without a pulse. Removal sets LOW
before releasing to INPUT. Removing an already absent pin does not touch it.
Assignments persist, but all restored outputs start OFF after reboot/power cycle.
Startup resolves battery ownership first and discards conflicting output bits.
Unconfigured pins remain untouched. Switching states does not write flash.
Commands use USB or authenticated remote administration; room guests and normal
room posts cannot control them. Outputs are not enabled in companion builds.

For an indicator LED, connect GPIO -> **330 ohm resistor** -> LED anode, LED
cathode -> GND (check suitable LED current). Larger loads need an external
transistor/MOSFET/driver and appropriate power supply, with a common ground.
Do not power motors, relays or other substantial loads directly from a GPIO.
Use an external pull-down where a driver must remain off before firmware starts;
firmware cannot guarantee the electrical state during reset or the bootloader.
No PWM, active-low settings, automation or restored-ON state in this version.

### Software verification

Native suites: `test_config_serializer`, `test_xiao_battery`, `test_environment`,
`test_xiao_outputs`, `test_xiao_pin_claims` and `test_environment_manager`. The Windows fallback is
`powershell -NoProfile -ExecutionPolicy Bypass -File test/run_xiao_native.ps1`.
It also compiles an output suite with UART pins reserved. The fallback requires
Python with the ziglang package and PlatformIO's cached googletest dependency.
Physical checks remain separate: divider calibration, real sensor comparison,
LED/power-cycle behavior,
and remote operation while repeating have not been performed for these additions.

## Custom chatroom build identification

The room target now builds `v1.17.1-sq2` (build `08 Oct 2026`). `sq2` adds read-only
room diagnostics; the installed chatroom remains `sq1` until separately updated.
Use `ver` through the room USB console or authenticated remote CLI to check the
installed build. Other role version strings and protocol numbers are unchanged.

In sq2, `get memory` reports internal and PSRAM heap total/free/largest blocks,
`get storage` reports filesystem total/used/free, and `get history` reports the
current RAM post count/capacity/bytes and oldest/newest timestamps. Sizes are bytes.
The filesystem report is usable capacity, not raw flash-partition size. Free bytes
are not all safe to occupy: leave filesystem/garbage-collection headroom.

These commands do not change settings, pins, files, timestamps or sync progress.
They retain the existing admin gate remotely and correlation-prefix handling.
The cache remains 32 volatile posts; persistent history is not implemented yet.
Live memory/storage measurements and persistent-history implementation remain pending.

## Companion phone-console transport

This checkout backports command 66 / reply 29 from upstream commit
`2dbd463eda815ae389d1694a6965e3a692d1b5bd` for battery commands. Requests may carry
`hh|` correlation prefixes, echoed in every command reply. Frame parsing is
bounded, accepts an optional trailing zero/padding, and rejects malformed frames
before changing settings. The advertised protocol version is unchanged.

In Settings -> Extra Tools -> Command Line on your BLE companion, after wiring:

```text
get battery.connected
set battery.connected off
set battery.gpio default
get adc.multiplier
set battery.connected on
get battery.connected
```

Settings take effect immediately and persist through the existing preference
store. Battery voltage flows through the app's existing battery/storage, core
stats and telemetry responses with no additional divider correction. The switch
is a manual declaration that a divider is installed, not automatic battery
presence detection. No new app screen, percentage model or charging/ageing sensor
is introduced. Physical phone and circuit checks remain pending.

## Verification for companion and chatroom additions

The native runner also exercises `test_battery_cli`, `test_companion_battery`
(with feature enabled and disabled), and `test_companion_cli`. Companion frame
tests use the actual XIAO board ADC implementation. Output tests cover independent
pins, command-level battery conflicts, non-default battery/calibration restoration
and all outputs restoring OFF. Common preference serialization is checked in a
separate executable from companion serialization.

Run `test/run_xiao_native.ps1` for native checks. The latest software verification
passed 78 native checks and all six standard XIAO Wio firmware builds on 2026-10-08;
the subsequent sq2 room diagnostic build also compiled successfully. Successful
compilation and mocked tests do not complete the physical phone, radio, sensor
or LED checks.
