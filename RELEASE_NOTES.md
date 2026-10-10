# MeshCore Enhanced release notes

Use one `## <tag>` section per stable Enhanced Release. Version tags use the
format `v<major>.<minor>.<patch>-sq<number>`, for example `v1.17.1-sq3`.
Commit the notes to `main` and wait for the XIAO workflow to pass before tagging.

## v1.17.1-sq7

- SQ7 XIAO room: independently saved `chat_ReplayAmount` (default 100, existing
  values preserved) and `chat_HistoryAmount` (default 2,000), both bounded 1-2,000.
  Reducing retention expires old posts permanently; enlarging it cannot resurrect
  partial-segment records. New firmware reads v1 journal controls and writes v2;
  rollback to SQ6 requires restoring a matching pre-upgrade filesystem backup.
- Administrator/USB-only targeted replay: `/chat replay USER 500 200` queues the
  inclusive 301-post range oldest first, preserving authors/IDs/timestamps and
  normal saved delivery progress. Saved aliases, full keys/unique prefixes,
  replay status/cancellation and bounded readable USB log pages are included.
- SQ7 enhanced companions accept authenticated older room frames without rewinding
  normal sync progress, with existing queue backpressure and immediate retry
  deduplication. Updated companions are needed for backfill from SQ4 history builds.
  Native checks exercise migration, rotation, alias faults, ACK independence and
  a complete 301-post replay through the actual companion receive path.

- XIAO repeater and room: optional one-profile Wi-Fi UTC time sync at startup
  and configurable intervals, write-only password commands, bounded attempts
  and OTA priority. Fresh NTP also establishes the room history civil clock.
  `clock_SyncMode` selects `startup` (default) or `repeat` with saved
  `clock_ResyncRepeatHours` (default 24, range 1-168); prior schedules are preserved.
  `clock_SyncClockNow` queues a manual sync (alias of `wifi.sync`), respecting
  the enabled profile, retry budget and low-battery guard. `-Force` or the
  `clock_SyncClockNowForced` alias bypasses battery checks for that cycle only.
  Each cycle allows three attempts with five-second Wi-Fi-off waits. Saved
  `clock_announce` (default off, room only) posts one final `[ROOM]` result under
  the room identity through normal history and encrypted delivery.

- Enhanced XIAO room USB bot API: bounded post reads, full authors/sequence IDs,
  explicit retention gaps and retry-safe room-authored replies. No additional
  companion radio or radio/phone protocol changes required. XIAO USB replies
  explicitly terminate hardware USB packets, with checked TX/RX buffers and
  a short controller yield to keep rapid history pages complete.
- Python monitor, sample `/bot ping`, `/bot help`, `/bot about` commands and
  custom-handler hook with SQLite
  progress/outbox, identity binding, reply pacing and reconnect recovery.

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

XIAO room smoke checks additionally verified settings/identity retention, DHT11,
PSRAM, saved playback limits and posts across soft reboots. Hardware testing
corrected startup stack usage and the ESP-IDF 4.4 SPIFFS sync path. The SQ7
application-only hardware update preserved all 223 prior posts, identity,
settings and saved member progress; USB history reads and startup Wi-Fi sync
were verified. Targeted radio replay and saved-alias power-cycle checks remain
pending, along with full 2,000-post physical capacity, loaded resource acceptance
and power interruption during writes.

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
