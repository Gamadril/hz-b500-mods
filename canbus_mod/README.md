# CAN bus module for HZ-B500

`canbus.mod` adds vehicle CAN features to the HZ-B500 display.
It receives CAN messages through the [ESP32-C3 UART bridge](../uart2can/README.md) and uses them to control wireless Android Auto media playback from the steering wheel and show a parking-distance overlay.

The current implementation targets a BMW E90 and the analyzed HZ-B500-MB firmware.
Other vehicles or firmware versions require changes.

## Build and install

Run `make` in this directory to build `build/canbus.mod`. Install the module in
the firmware's `mod/` directory and load it before `desktop.mod` in
`startup.sh`. Connect the display's UART3 to the bridge as described in the
[bridge README](../uart2can/README.md).

Use [d1s-melis-tools](https://github.com/Gamadril/d1s-melis-tools) to extract,
edit, and repack a device flash dump. Follow its documentation for the flash
dump workflow and add the built module to the extracted firmware.
