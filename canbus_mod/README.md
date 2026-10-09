# CAN bus module for HZ-B500

`canbus.mod` adds vehicle CAN features to the HZ-B500 display.
It receives CAN messages through the [ESP32-C3 UART bridge](https://github.com/Gamadril/hz-b500-mods/blob/main/uart2can/README.md) and uses them to:

- control wireless Android Auto media playback (next/previous track) from the steering wheel
- show a parking-distance (PDC) overlay

The current implementation targets a BMW E90 and the analyzed HZ-B500-MB firmware.
Other vehicles or firmware versions require changes.

## Hardware connection

CAN frames reach the display over **UART3** from the bridge.
UART3 is not enabled in the stock firmware, so it must be activated first (see below).

| Display signal | SoC pin | Where            |
|----------------|---------|------------------|
| UART3 TX       | PB6     | Connector **J6** |
| UART3 RX       | PB7     | Connector **J6** |

Connect these to the bridge as described in the
[bridge README](https://github.com/Gamadril/hz-b500-mods/blob/main/uart2can/README.md)
(TX of one side to RX of the other, plus a common ground).

## Enable UART3

By default PB6 and PB7 are not configured as UART3.
J6 exposes the pins, but the pin multiplexing has to be changed in the device's `sys_config.fex`, which is stored in the kernel partition of the flash image.

1. Extract the flash dump and the kernel partition with
   [d1s-melis-tools](https://github.com/Gamadril/d1s-melis-tools).
2. Locate `sys_config.fex` in the extracted kernel partition.
3. Add or edit the `[uart3]` section so it reads:
```ini
   [uart3]
   uart_tx = port:PB06<7><1><default><default>
   uart_rx = port:PB07<7><1><default><default>
```
4. Repack the kernel partition and flash it back to the device.

Without this change the module receives no data, even if the wiring and the bridge are correct.

## Build and install

Run `make` in this directory to build `build/canbus.mod`.
Install the module in the firmware's `mod/` directory and load it before `desktop.mod` in `startup.sh`.

Use [d1s-melis-tools](https://github.com/Gamadril/d1s-melis-tools) to extract, edit, and repack a device flash dump. Follow its documentation for the flash dump workflow and add the built module to the extracted firmware.
The UART3 change above and the module installation can be done in the same repack.

## How it works

### PDC overlay

PDC is shown as a full-screen overlay without switching views.
The module temporarily hides the active video layer and restores it when PDC closes, so returning to Android Auto is fast.

An earlier approach used the `Back.data` view.
It was dropped because it needed a hard-coded jump to the `EnterUI` function in `init.axf`, whose RAM address differs between firmware versions (it worked on a development board but not on the device in the car), and because Android Auto took a long time to reconnect and reinitialize its UI afterwards.

### Steering wheel media keys

The module decodes the steering wheel Up/Down CAN frames and sends the corresponding ioctl requests while an Android Auto session is active.
