# Robotiq SDK

Public SDKs, quickstart tools and firmware utilities for Robotiq products, one folder per product.

> **Moved from `tactile_sensors`:** this repository was `github.com/robotiq/tactile_sensors`. The TSF-85 content now lives in [tsf/](tsf/), and the old URL redirects here. Existing clones keep working; update their remote with `git remote set-url origin https://github.com/robotiq/robotiq-sdk.git`.

> **Moved from `Robotiq/grippers`:** the grippers C++ SDK now lives in [grippers/](grippers/), with its full history. [Robotiq/grippers](https://github.com/Robotiq/grippers) no longer changes; its tags and pinned commits keep working.

| Folder | Product | Status |
|--------|---------|--------|
| [tsf/](tsf/) | TSF-85 tactile sensor: C++ SDK, Python quick-connect and web viewer, firmware flashing tool, device setup utilities | available |
| [grippers/](grippers/) | 2F-85, 2F-140, Hand-E and Hand-E C10 grippers: C++ SDK, for Linux, Windows, macOS and freestanding/RTOS targets | available |

Each product folder has its own README with requirements, build steps and quickstart commands.

## Versioning

The whole repository shares one version, kept in the [VERSION](VERSION) file. Every SDK's CMake project reads it, and each release tags the repository as `vX.Y.Z`. The first robotiq-sdk release is 3.0.0, above every earlier tag of the TSF-85 and grippers repositories.

Versions follow [semantic versioning](https://semver.org): a patch release fixes bugs, and a minor release adds API to any SDK. A breaking change to a documented API is deprecated first, and lands only in a planned major release of the whole repository, so users move up a major version rarely. What counts as the documented API is defined in each product's README.

The grippers CMake package keeps `SameMajorVersion` compatibility: `find_package(grippers 3)` accepts every 3.x release.

## License

This repository is licensed under the BSD 3-Clause License. See [LICENSE](LICENSE) for details. The grippers SDK's [license](grippers/LICENSE) is the same, with an additional copyright notice.

**Exception:** `tsf/firmware/bootloader_host.py` is a derivative of
[cyrozap/Cypress-HID-Bootloader-Host](https://github.com/cyrozap/Cypress-HID-Bootloader-Host)
and is licensed under the GNU General Public License v3.0, not BSD-3-Clause. See
[tsf/firmware/LICENSE](tsf/firmware/LICENSE). It is a standalone firmware-flashing tool;
the SDKs and other components neither import nor link it.
