# CAN to UART listener for F133/D1s car devices

This firmware turns an ESP32-C3 SuperMini and an SN65HVD230 CAN transceiver into a CAN-to-UART bridge. It was created for an aftermarket car device based on the F133/D1s: the device needs to observe vehicle CAN messages to detect signals such as vehicle state, without participating in the vehicle network.

The ESP32-C3 runs its CAN controller in **listen-only mode**. It receives frames but does not transmit frames or acknowledge traffic. Frames are sent to the F133/D1s over UART in a subset of the SLCAN (Lawicel) text format. The bridge accepts standard and extended CAN frames, including remote frames.

## Hardware and defaults

The `esp32c3_supermini` PlatformIO environment is configured for:

| Connection | ESP32-C3 pin | 
| --- | --- | 
| CAN TX → transceiver TXD | GPIO2 |
| CAN RX ← transceiver RXD | GPIO3 |
| UART RX ← F133/D1s TX | GPIO0 |
| UART TX → F133/D1s RX | GPIO1 |

Power the transceiver at 3.3V. The configured CAN bitrate is **100 kbit/s** (BMW E90 K-CAN), and the channel opens automatically at boot. Change the pin, UART baud, CAN bitrate, or auto-open definitions in [`platformio.ini`](platformio.ini) to match your setup.
In my setup SN65HVD230 RS pin tied to GND for high-speed mode.
CANH and CANL connected to K-CAN bus without extra termination - it's already in the vehicle.
Serial port settings: 750000 baud, 8N1

The **750000 baud** UART setting is intentional. The software on the F133/D1s opens its port requesting 921600 baud, but its UART uses a 24 MHz clock and the resulting hardware divisor is 2. The actual line rate is therefore 24 MHz / (16 × 2) = **750000 baud**. The ESP32-C3 must use that physical rate, even though the F133/D1s software requests 921600.

## Build and flash

Install [PlatformIO](https://platformio.org/install), connect the ESP32-C3 over USB, then run from this directory:

```sh
pio run -e esp32c3_supermini
pio run -e esp32c3_supermini -t upload
```

If PlatformIO does not find the board automatically, add `--upload-port /path/to/port` to the upload command. USB is used for flashing; the bridge data uses the GPIO UART pins listed above.

## UART protocol

CAN frames arrive as carriage-return-terminated SLCAN lines (`t`/`T` for data frames, `r`/`R` for remote frames). Commands also end with `\r`. The most useful commands are `C` to close the channel, `S2`–`S8` to choose 50, 100, 125, 250, 500, 800, or 1000 kbit/s while closed, and `O` to reopen it. `Z0`/`Z1` disable or enable timestamps while closed; `F` reads status. Successful commands return `\r`, while unsupported or invalid commands return BEL (`0x07`). Transmit commands are rejected because the bridge is always listen-only.
