# Bootloader Host

Flashes firmware to the Master Hub via its USB bootloader.

## Requirements

```
pip install hidapi pyserial
```

## Firmware compatibility

Each hardware revision runs its own firmware family. A hub and its fingers
always share the same hardware revision.

| Hardware revision | Finger serial numbers | Firmware | Latest |
|---|---|---|---|
| 2.1 | `21xxx` | 1.x | 1.1.8 |
| 2.2 | `22xxx` | 2.x | 2.2.0 |

Don't flash firmware from the other family: the hub also reprograms its
fingers with the firmware it carries.

## Usage

Flash the latest firmware for the connected hub. The hardware revision is
detected automatically and the firmware is taken from `bin/`:

```
python bootloader_host.py
```

Flash a specific firmware file. It is refused if it doesn't match the
hardware revision:

```
python bootloader_host.py --firmware path/to/Master_hub_X.Y.Z.cyacd
```

If the hub is already in its bootloader (for example after an interrupted
update), the hardware revision can't be detected. Give it from the finger
serial numbers:

```
python bootloader_host.py --hw-rev 2.2
```

`--latest` is still accepted. It is now the default behaviour.

### `--force`

Skips the hardware compatibility check, after asking you to type `FORCE`.

> **Warning:** flashing firmware made for another hardware revision can leave
> the hub and its fingers unusable. Only use `--force` if Robotiq support told
> you to.

## How it works

1. Searches for the Master Hub on serial (tries new VID/PID, then falls back to old)
2. Detects the hardware revision while the firmware is running:
   - asks the firmware for its version: `1.x` means 2.1, `2.x` means 2.2;
   - on `2.x`, cross-checks the hub device info stored in EEPROM at
     production. Missing device info only gives a warning; unexpected device
     info stops the script.
3. Picks the firmware and checks that it matches the hardware revision
4. Sends a reboot-to-bootloader command over serial
5. Waits for the HID bootloader to appear
6. Checks that the firmware is for this chip, flashes the `.cyacd` file,
   verifies the application checksum and exits the bootloader

## License

`bootloader_host.py` is a derivative of
[cyrozap/Cypress-HID-Bootloader-Host](https://github.com/cyrozap/Cypress-HID-Bootloader-Host)
(© 2016 Forest Crossman) and is licensed under the **GNU General Public License
v3.0** — see [LICENSE](LICENSE). This is the one GPL-licensed component in an
otherwise BSD-3-Clause repository; it is a standalone tool, not linked by the SDK.
