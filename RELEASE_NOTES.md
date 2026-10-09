# MeshCore Enhanced release notes

Use one `## <tag>` section per stable Enhanced Release. Version tags use the
format `v<major>.<minor>.<patch>-sq<number>`, for example `v1.17.1-sq3`.
Commit the notes to `main` and wait for the XIAO workflow to pass before tagging.

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
