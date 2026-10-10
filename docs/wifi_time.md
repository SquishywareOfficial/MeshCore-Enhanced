# Optional Wi-Fi clock sync

The standard XIAO ESP32-S3 + Wio-SX1262 repeater and room builds can save one
2.4 GHz Wi-Fi network, briefly connect to request NTP time, then switch Wi-Fi off.
Existing/blank devices leave this feature disabled. It does not provide an
Internet mesh gateway, remote web console, or permanent Wi-Fi connection.
Companions and the ESP-NOW bridge retain their existing Wi-Fi behaviour.

## Configure through the USB console or authenticated admin CLI

Replace the sample network name and password below with your own values.
Do not add quotes around either value; spaces and punctuation are literal.
To edit an enabled profile, disable it first.

```text
set clock_SyncEnabled off
set wifi.ssid Your network name
set wifi.password replace-with-your-password
set clock_SyncMode startup
set clock_ResyncRepeatHours 24
set clock_SyncEnabled on
get wifi.ssid
get clock_SyncEnabled
get clock_SyncMode
get clock_ResyncRepeatHours
get wifi.status
set clock_disableTimeSyncOnLowBattery on
set clock_LowVoltage 3.5
```

`set wifi_SSID ...`, `get wifi_SSID`, and `set wifi_Password ...` are aliases.
The command verb and Wi-Fi setting names accept either case. `on`/`off` values
remain lowercase. SSID is limited to 32 bytes. Password accepts an empty value
for an open network, 8-63 printable ASCII characters, or a 64-digit hexadecimal
WPA PSK. `set wifi.password` alone clears the password. `wifi.clear` erases the
single profile, disables sync, and restores startup-only scheduling.

`get wifi.password` / `get wifi_Password` always return a write-only error.
These builds do not echo USB commands, preventing the saved password appearing
in console replies. Credentials are saved **unencrypted** alongside other node
preferences in `/prefs.json`; this is not encrypted storage. Guest room members
cannot run configuration commands. Existing radio/admin authentication applies.

## When sync runs

`clock_SyncEnabled` is the explicit master switch, default **off**. It aliases
`wifi.enabled`, so both names change the same saved setting. Having a saved
network does not force sync when the switch is off. A valid network is required
to enable sync. Enable the feature to queue the first attempt. Every subsequent
boot also queues an attempt, regardless of the fallback RTC date.

Choose and inspect the saved schedule explicitly:

```text
set clock_SyncMode startup
get clock_SyncMode
```

`startup` is the **default**, with no periodic sync after the bounded retry cycle.
Select `repeat` to sync on startup and at the interval saved in
`clock_ResyncRepeatHours`, counted after a complete retry cycle finishes:

```text
set clock_ResyncRepeatHours 24
set clock_SyncMode repeat
get clock_SyncMode
get clock_ResyncRepeatHours
```

`clock_ResyncRepeatHours` defaults to **24** and accepts whole hours **1-168**.
It can be changed while sync is enabled. The value is saved even in `startup`
mode: changing it there does not trigger a sync or enable periodic work. Switching
back to `repeat` restores that saved interval.
The dotted forms `clock.syncEnabled`, `clock.syncMode` and
`clock.resyncRepeatHours` are also supported.
Enabling sync or changing connection/mode/guard settings while enabled queues a
fresh cycle, as does changing repeat hours while in `repeat` mode.
Changing `clock_announce` does not restart or queue a sync.

Existing saved startup schedules remain startup-only. An existing saved positive
interval (including the former 24-hour mode) becomes `repeat` at that same rate;
Wi-Fi credentials and the enabled switch are preserved. `startup_24h` is no longer
a mode value. The older `wifi.interval` command remains compatible: `0` selects
startup without forgetting repeat hours, and `1`-`168` selects repeat at that
interval. Its setter still requires sync to be disabled; use the new clock
setting to change hours while enabled. The mode getter always returns `startup`
or `repeat`.
The schedule uses uptime and remains correct across `millis()` wraparound; it is restarted
on reboot. Each cycle stops after three failed attempts; there is no continuous retry loop when a network is unavailable.

Request an extra sync cycle without rebooting:

```text
clock_SyncClockNow
get wifi.status
clock
```

`clock_SyncClockNow` is an action command: use it directly, without `set` or a
value. It queues up to three attempts and returns immediately; `OK - sync queued`
does not mean the clock has already synced. The existing `wifi.sync` command is
an equivalent alias. Both require a configured, enabled profile, preserve the
saved schedule and battery guard, and return a busy error during an active
attempt or retry cycle. No reboot is needed after configuring.
To explicitly bypass the battery guard for one manual cycle:

```text
clock_SyncClockNow -Force
```

`clock_SyncClockNowForced` is an equivalent action alias. Command names and the
`-Force` flag are case-insensitive. The reply is `OK - forced sync queued`;
this still requires a valid, enabled Wi-Fi profile. The override covers that
cycle's retries and OTA deferrals, but cannot reset an active retry budget or
interrupt OTA. It is never saved to flash, does not change the battery guard
setting, and clears on success, exhausted retries, disabling sync or a change
that restarts sync. Subsequent ordinary/manual/automatic cycles use the guard.

`get wifi.status` shows disabled/unconfigured/waiting/connecting/syncing/retry_wait/ok/failed,
or `low_battery` when a measured battery is too low, and the last successful UTC epoch for the current boot (zero before success).
`clock` remains the existing human-readable UTC clock command.

Each sync cycle makes **up to three attempts total** (one initial attempt and
two retries). Connection and NTP phases each have a 12-second timeout. After a
failed attempt, Wi-Fi and SNTP are stopped for five seconds before the next
attempt. A successful fresh NTP response ends the cycle immediately. With no
deferral, the worst-case cycle is approximately 82 seconds (three 24-second
attempts and two five-second waits). `clock_SyncClockNow` / `wifi.sync` cannot reset a running retry
budget. Low battery and OTA interruptions defer work without consuming a failed
attempt or reporting final failure. LoRa, serial bot and
sensor handling continue during attempts. The MCU stays awake for the attempt,
then SNTP and Wi-Fi are stopped on success, failure or disable. Sync itself does not
write flash or change any mesh radio setting. Optional room announcements use
the existing history journal and therefore write one normal post per completed cycle. OTA takes priority: starting OTA
cancels the station attempt and leaves the OTA access point running.

## Optional room announcements

Room servers can announce the final result in their room chat:

```text
set clock_announce on
get clock_announce
set clock_announce off
```

The saved setting defaults to **off** on new and existing devices. The dotted
alias `clock.announce` is also accepted. It can be changed while sync is enabled
without restarting a cycle. To test an enabled, configured profile, use `clock_SyncClockNow`.

The room posts one message after success, or after all three attempts fail:

```text
[ROOM] Clock was synced to: 2026-10-10 03:15:42 UTC
[ROOM] Clock failed to sync (3 attempts exhausted).
```

These are ordinary posts authored by the room's own identity. The `[ROOM]` prefix
distinguishes them from an external `[SquishyBot]` reply; the server name/key do
not change. They use the existing encrypted room delivery and catch-up path,
rather than broadcasting on a public channel. In the enhanced room build they
are retained in flash like other posts; the legacy room keeps its RAM cache.
Members receive them through normal playback/polling, so radio delivery can lag
behind the actual sync. Storage recovery/full/write failure follows the normal
post failure behaviour: the server does not bypass the journal or spam retries.

There are no intermediate retry posts and no failure posts for disabled sync,
missing profiles, low-battery deferral or OTA ownership. The failure message
does not label the previously synced time as a new sync. Successful time is
applied to the RTC/history civil clock before the success post is created.
Repeaters have no room chat, so enabling `clock_announce` there returns an
unsupported-role error; their Wi-Fi retry behaviour is the same.

## Low-battery guard

The guard defaults to **on**, with a **3.5 V** cutoff. It uses the existing
calibrated battery reading, so the configured divider multiplier is already
applied; the cutoff refers to full battery voltage, not the halved ADC voltage.
Only enabled battery sensing with a nonzero reading activates the guard.
An unavailable/disabled reading does not block a node from syncing.

```text
set clock_disableTimeSyncOnLowBattery on
get clock_disableTimeSyncOnLowBattery
set clock_LowVoltage 3.5
get clock_LowVoltage
```

The dotted forms `clock.disableTimeSyncOnLowBattery` and `clock.lowVoltage` are
also accepted. The guard accepts `on`/`off` or `true`/`false`. Voltage is in volts,
2.000-5.000, with at most three decimal places. These two guard settings can be
changed while sync is enabled. Choose the cutoff appropriate to your battery.

If voltage is below the cutoff, the attempt waits without powering Wi-Fi on.
During an active attempt voltage is checked once per second; falling below the
cutoff cancels that attempt. Deferred attempts recheck once per minute and resume
when voltage reaches the cutoff **plus 0.1 V** (3.6 V with the default), reducing
repeated starts near the threshold. `clock_SyncClockNow` / `wifi.sync` request an immediate fresh
battery check while preserving the guard. Disabling sync cancels deferred work.
The ADC is not polled by this feature between completed scheduled attempts.

## Chatroom history dates

Only a fresh, plausible NTP response establishes trusted civil time. The ESP32's
May 2024 startup placeholder is never accepted as a successful sync. Firmware
updates the selected RTC and the room's `onClockSet` history-time anchor on the
main loop. Journal timestamps/cursors retain their existing uniqueness rules;
NTP does not rewrite stored posts or delivery progress. New successful syncs may
correct civil time backwards without rolling back the journal's timestamp floor.
After a full power cycle the room must obtain a new trusted time, through this
feature or the existing authenticated clock commands, before inactivity purges.
No New Zealand timezone offset is stored: message timestamps remain UTC.

Startup-only avoids routine Wi-Fi energy use, but it does not guarantee a
particular clock accuracy: the internal RTC can drift with temperature in light
sleep. Daily sync is available when accurate dates over long uptimes matter.

## Implementation and verification

Inspired by the local MicriOS T-Display `BootTimeSyncService` and `ClockApp`
(background connection/NTP phases and radio shutdown). MeshCore uses its existing
preferences rather than MicriOS's five-profile NVS store. See
[MicriOS source](https://github.com/SquishywareOfficial/MicriOS/blob/main/MicriOS-T-Display/BootTimeSyncService.cpp)
and [Espressif SNTP hook documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/system_time.html).

`XIAO_WIO_WIFI_TIME=1` gates preferences/commands/scheduler/backend to these roles.
The backend overrides ESP-IDF's supported weak `sntp_sync_time` hook to capture a
response; it does not trust `time(nullptr)` or apply time from the lwIP thread.
Native tests use a deterministic backend and exercise schedule migration and
remembered repeat intervals, single-cycle forced battery overrides, timeout, shutdown, OTA
ownership, interval/wraparound, measured-low-battery deferral/recovery, settings migration,
validation, failed writes, three-attempt budgets, backoff/wraparound,
one terminal completion, and persisted announcement toggles without restarting sync.
Hardware Wi-Fi, time accuracy and simultaneous mesh traffic still require a live
network test; successful compilation alone does not verify those behaviours.
