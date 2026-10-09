# MeshCore Enhanced release notes

Use one `## <tag>` section per stable Enhanced Release. Version tags use the
format `v<major>.<minor>.<patch>-sq<number>`, for example `v1.17.1-sq3`.
Commit the notes to `main` and wait for the XIAO workflow to pass before tagging.

## Unreleased

- Standard XIAO room server: newest 2,000 posts retained in flash, catch-up
  configurable from 1 to 2,000 (default 100), chronological playback and saved
  delivery progress for returning full public-key identities.
- New/forgotten users start with future posts. Administrator commands list and
  purge saved members without altering the archive or room permissions. Manual
  inactivity cleanup requires a trusted UTC clock set during the current boot.
- Checked journal/member/preferences writes and fail-closed storage recovery;
  nonblank filesystem failures do not automatically format or regenerate identity.
- All four standard XIAO companions: queue acceptance before room ACK/cursor
  advancement, full-queue backpressure and content-validated retry deduplication.
- Storage/protocol tests include two simulated recipients accepting 2,000 posts
  each with 16/256 queues and restart recovery. See [history guide](docs/room_history.md).
- Hardware checks verified 204 stored posts, saved member delivery progress and
  playback=200 after a user-performed power cycle. A server-generated post was
  acknowledged by the companion and confirmed visible in the phone app.

No Release tag has been created for these changes.

XIAO room smoke checks additionally verified settings/identity retention, DHT11,
PSRAM, saved playback limits and posts across soft reboots. Hardware testing
corrected startup stack usage and the ESP-IDF 4.4 SPIFFS sync path. The numbered
200-post phone catch-up test is in progress; full 2,000-post physical capacity,
loaded resource acceptance and power interruption during writes remain pending.

## v1.17.1-sq3

First combined Squishyware Release for the original Seeed XIAO ESP32-S3 +
Wio-SX1262 B2B kit, based on MeshCore v1.17.1.

- Six firmware variants: repeater, chatroom, and Bluetooth, USB, UART and Wi-Fi
  companions. Each download includes application and full installation images,
  partition metadata, checksums and flashing instructions.
- Repeater, chatroom and companion builds support saved optional battery sensing,
  a selectable ADC GPIO and voltage-divider calibration.
- Repeater and chatroom builds support selectable SHT4x or DHT11 temperature and
  humidity sensors and configurable digital outputs, through USB or authenticated
  remote administration. DHT11 readings use a configurable GPIO and cached paired
  samples. Companion builds retain battery support only.
- Chatroom builds include read-only memory, filesystem and history diagnostics.
  Message history remains the existing 32-message RAM cache; this Release does
  not add extended or persistent history.
- The USB flasher follows successful `main` builds independently of Releases.
  Its unchecked-by-default **Erase data** option preserves settings on compatible
  installations; selecting it performs a fresh installation.

Validation includes native firmware tests, publishing and installer regression
tests, and all six CI builds. DHT11 temperature/humidity readings were checked on
a physical XIAO chatroom device. The browser installer has simulated serial tests;
physical browser flashing remains a separate hardware check.
