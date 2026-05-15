# Bootloader Host

Flashes firmware to the Master Hub via its USB bootloader.

## Requirements

```
pip install hidapi pyserial
```

## Usage

Flash with the default firmware (Master_hub_1.0.6):

```
python bootloader_host.py
```

Flash a specific firmware file:

```
python bootloader_host.py --firmware path/to/firmware.cyacd
```

## How it works

1. Searches for the Master Hub on serial (tries new VID/PID, then falls back to old)
2. Sends a reboot-to-bootloader command over serial
3. Waits for the HID bootloader to appear
4. Flashes the `.cyacd` firmware file and exits the bootloader
