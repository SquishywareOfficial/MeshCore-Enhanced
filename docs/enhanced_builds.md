# XIAO Wio builds and browser installation

[Open the MeshCore Enhanced flasher](https://squishywareofficial.github.io/MeshCore-Enhanced/).

This fork builds the original Seeed XIAO ESP32-S3 (8 MB flash / 8 MB PSRAM)
with the Wio-SX1262 B2B kit. The differently wired `Xiao_S3` target is not interchangeable.

Every push to `main` runs the native checks and builds these six standard targets:

| Role | PlatformIO target |
| --- | --- |
| Repeater | `Xiao_S3_WIO_repeater` |
| Chatroom | `Xiao_S3_WIO_room_server` |
| Bluetooth companion | `Xiao_S3_WIO_companion_radio_ble` |
| USB companion | `Xiao_S3_WIO_companion_radio_usb` |
| UART companion | `Xiao_S3_WIO_companion_radio_serial` |
| Wi-Fi companion | `Xiao_S3_WIO_companion_radio_wifi` |

Pull requests build and test too, but do not publish the site. Manual workflow
runs on `main` also refresh the site. A failed test or any failed target leaves
the previous successful site available. Artifacts are downloadable from the
[XIAO build workflow](https://github.com/SquishywareOfficial/MeshCore-Enhanced/actions/workflows/build-xiao-wio.yml)
for 30 days; the deployed site keeps the latest successful set.

Each download includes the application image, complete installation image,
partition table and `build.json` with its commit, version and SHA-256 checksums.
CI version strings are `v1.17.1-sq-<short commit>` and fit the companion protocol's
19-character version field. The build date is refreshed by upstream `build.sh`.
Direct local builds retain their existing version defaults. On-air protocol
versions and device settings are not changed by the publishing workflow.

## Fresh installation

The page uses ESP Web Tools over USB in desktop Chrome or Edge. Its buttons
perform a full fresh installation, **erasing settings, identity keys and stored
data**. Attach the LoRa antenna before powering the radio. Use a data-capable USB
cable and close any other app using the port. Enter BOOT mode if needed, then
reconnect without holding BOOT after flashing if the console does not start.

Configure your name, passwords and regional radio settings through the phone app
(companion) or config.meshcore.io (repeater/chatroom). Battery sensing is off until
you install the divider and explicitly enable it. See the
[wiring and command guide](../variants/xiao_s3_wio/README.md). UART and Wi-Fi builds
are advanced client transports; the stock Wi-Fi target contains placeholder
credentials, so configure/rebuild it for your network before use.

## Updating an existing node

Download `firmware.bin` (the application image) for the same role and hardware.
Use the supported USB/OTA application update flow after backing up identity and
settings and checking its partition layout and active OTA slot. The browser page
does not offer an application-only update: writing app0 blindly is unsafe when
app1 is active. Verify saved settings and identity after updating.

The complete `firmware-merged.bin` is for fresh installation at offset 0; do not
upload it as an OTA application. Browser images are remerged with the DIO header
recommended by ESP Web Tools for qio_opi boards, using the same compiled app and
the board's 8 MB partition layout. Packaging fails on an unexpected layout.

## How publication works

`.github/workflows/build-xiao-wio.yml` builds six independent targets, then
`tools/publish_xiao.py` verifies matching commits, checksums, chip and partitions
before assembling the Pages artifact. Firmware and manifests are served from
the same HTTPS site. No runtime GitHub API calls or cross-origin release downloads
are required by the installer. Publishing requires GitHub Pages to use **GitHub
Actions** as its source. No custom domain or upstream documentation domain is used.

The inherited full-board release workflows remain available for manual/tag builds.
The broad upstream PR build matrix and upstream unit-test job run only in the
original repository; this fork's focused workflow covers the XIAO targets and
native suites. The official MeshCore web flasher does not distribute this fork's
custom images. Local `TODO*` notes and device backups are excluded from publication.
