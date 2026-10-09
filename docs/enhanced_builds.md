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
Direct local room and companion builds identify this history work as
`v1.17.1-sq4-hist`; this local label is not a published Release tag. On-air protocol
versions and device settings are not changed by the publishing workflow.

## Install or update in the browser

The page uses pinned esptool-js 0.7.0 over USB in desktop Chrome or Edge.
Each role has an **Install via USB** button and an **Erase data** checkbox,
unchecked by default. Select the same hardware and role as your existing node.

With Erase data **unchecked**, the installer verifies the firmware download,
chip and flash size, reads and verifies the installed partition table and OTA
records, and writes only the application in the active slot (app0 or app1).
It verifies the written application and checks that the partition table and
all non-application partitions, including NVS, OTA records and SPIFFS, are
unchanged before restarting. It refuses an incompatible layout, unreadable or
ambiguous OTA selection, or an update awaiting boot confirmation; it never
automatically falls back to erasing. This is an in-place USB update, not an
atomic OTA update: keep USB connected until it finishes. The restart clears
any messages stored only in RAM, including the legacy room cache. Enhanced XIAO
room history stored in flash survives a compatible update without erasure.

With Erase data **checked**, it explicitly erases the chip and writes the full
installation image at offset zero. **This erases settings, identity keys and
stored data.** Use it for a new device or when intentionally starting over.

Attach the LoRa antenna before powering the radio. Back up important settings,
use a data-capable USB cable, and close other apps using the port. Enter BOOT
mode if needed, then reconnect without holding BOOT after flashing if necessary.

Configure your name, passwords and regional radio settings through the phone app
(companion) or config.meshcore.io (repeater/chatroom). Battery sensing is off until
you install the divider and explicitly enable it. See the
[wiring and command guide](../variants/xiao_s3_wio/README.md). UART and Wi-Fi builds
are advanced client transports; the stock Wi-Fi target contains placeholder
credentials, so configure/rebuild it for your network before use.

## Updating an existing node

For a browser update, use Install via USB with Erase data unchecked as described
above. Downloading and flashing manually remains optional: `firmware.bin` is the
application image for a supported USB/OTA update flow. Check the partition layout
and active slot before a manual USB write, and verify identity/settings afterward.

The complete `firmware-merged.bin` is for fresh installation at offset 0; do not
upload it as an OTA application. Browser images are remerged with the DIO header
recommended by ESP Web Tools for qio_opi boards, using the same compiled app and
the board's 8 MB partition layout. Packaging fails on an unexpected layout.

## How publication works

`.github/workflows/build-xiao-wio.yml` builds six independent targets, then
`tools/publish_xiao.py` verifies matching commits, checksums, chip and partitions
before assembling the Pages artifact. Firmware and manifests use commit-specific
URLs on the same HTTPS site, preventing cached binaries from an earlier build
being offered as the current one. No runtime GitHub API calls or cross-origin release downloads
are required for firmware downloads. The browser loads pinned esptool-js and
SparkMD5 modules from esm.sh. The generated full-install manifest is retained for
external ESP Web Tools users; the page's installer does not use it for updates.
Publishing requires GitHub Pages to use **GitHub
Actions** as its source. No custom domain or upstream documentation domain is used.

The inherited full-board release workflows remain available for manual/tag builds.
The broad upstream PR build matrix and upstream unit-test job run only in the
original repository; this fork's focused workflow covers the XIAO targets and
native suites. The official MeshCore web flasher does not distribute this fork's
custom images. Local `TODO*` notes and device backups are excluded from publication.

## Persistent room history

The standard XIAO room target retains the newest 2,000 posts in its existing
SPIFFS partition. Catch-up defaults to the newest 100 missing eligible posts;
`set history.playback 2000` increases it for future login sessions. Saved user
progress survives room restarts. New or manually forgotten users start fresh.
See [room history commands, recovery and update limits](room_history.md).

Enhanced XIAO companions acknowledge room posts only after queue acceptance.
A full queue pauses catch-up until the phone drains it and checks in again.
Native protocol/storage checks cover this behavior; real phone, loaded heap/GC
and power-loss testing remain a separate hardware acceptance step.
