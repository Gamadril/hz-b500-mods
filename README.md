# HZ-B500 F133/D1s modifications

Hardware and software modifications for an F133/D1s-based HZ-B500 car display with Android Auto and CarPlay. This repository is an umbrella project: each modification lives in its own subdirectory with its own build and usage instructions.

## Projects

| Directory | Purpose |
| --- | --- |
| [`uart2can/`](uart2can/README.md) | ESP32-C3 and SN65HVD230 CAN-to-UART bridge. It listens to vehicle CAN traffic and forwards frames to the display as SLCAN text. |
| [`canbus_mod/`](canbus_mod/README.md) | Melis `canbus.mod` for the display. It reads the bridge's UART frames, handles BMW E90 steering-wheel media controls for wireless Android Auto, and draws a parking-distance overlay. |
| [`scripts/`](scripts/patch_wireless_audio.sh) | Contains an optional tool to adjust Android Auto audio gain in the firmware's `wireless.mod` (wireless AA) or `auto.mod` (wired AA). |

The bridge and module are designed to work together. See their READMEs for wiring, baud rates, build commands, and firmware-specific details. The current CAN message handling targets a BMW E90 and the analyzed HZ-B500-MB firmware; other vehicles or firmware revisions may need changes.

## Firmware modification

Use [d1s-melis-tools](https://github.com/Gamadril/d1s-melis-tools) to extract, modify, and repack the F133/D1s Melis firmware or a device flash dump. For example, it can unpack the ROOTFS where `canbus.mod` is installed and repack the modified firmware image. Follow that project's documentation for the appropriate image or dump workflow, then follow [`canbus_mod/README.md`](canbus_mod/README.md) for module placement and startup configuration.

To adjust Android Auto audio gain, run
`scripts/patch_wireless_audio.sh INPUT_MOD OUTPUT_MOD [GAIN]`.
`INPUT_MOD` is either `wireless.mod` or `auto.mod`. Both modules contain the
same sample-scaling routine. `GAIN` replaces the 60 and is an optional percentage
from 10 to 100; the default is 100. The script also supports
`--overwrite INPUT_MOD [GAIN]`.

## License

MIT; see [LICENSE](LICENSE).
