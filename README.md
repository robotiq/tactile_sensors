# Robotiq SDK

Public SDKs, quickstart tools and firmware utilities for Robotiq products, one folder per product.

> **Moved from `tactile_sensors`:** this repository was `github.com/robotiq/tactile_sensors`. The TSF-85 content now lives in [tsf/](tsf/), and the old URL redirects here. Existing clones keep working; update their remote with `git remote set-url origin https://github.com/robotiq/robotiq-sdk.git`.

| Folder | Product | Status |
|--------|---------|--------|
| [tsf/](tsf/) | TSF-85 tactile sensor: C++ SDK, Python quick-connect and web viewer, firmware flashing tool, device setup utilities | available |
| grippers/ | 2F-85, 2F-140 and Hand-E grippers: C++ SDK (moving here from [Robotiq/grippers](https://github.com/Robotiq/grippers)) | coming |

Each product folder has its own README with requirements, build steps and quickstart commands.

## License

This repository is licensed under the BSD 3-Clause License. See [LICENSE](LICENSE) for details.

**Exception:** `tsf/firmware/bootloader_host.py` is a derivative of
[cyrozap/Cypress-HID-Bootloader-Host](https://github.com/cyrozap/Cypress-HID-Bootloader-Host)
and is licensed under the GNU General Public License v3.0, not BSD-3-Clause. See
[tsf/firmware/LICENSE](tsf/firmware/LICENSE). It is a standalone firmware-flashing tool;
the SDKs and other components neither import nor link it.
