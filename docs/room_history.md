# Persistent XIAO room history

The standard `Xiao_S3_WIO_room_server` build stores the newest **2,000 posts**
in flash. Catch-up is independently configurable from **1 to 2,000**, with a
default of **100**. It sends the newest eligible missing posts oldest first,
excluding the recipient's own posts before applying the limit. Live posts after
the login snapshot are additional. Ordinary keep-alives resume that window;
they do not open endless extra batches of older history.

The four standard XIAO companion builds include queue backpressure and retry
deduplication. The radio format and existing phone interface are unchanged.
Other room boards retain their original 32-post RAM implementation. Local
`*_legacy` regression targets are excluded from the six published images.

## Commands

Use the room's USB text console or its authenticated remote **administrator**
command line. These are not local companion console commands. Guest posts that
look like commands remain ordinary text.

```text
get history.playback
set history.playback 100
set history.playback 2000
set history.playback default
get history
get history.storage
get history.stack
history.users.list
history.users.list 1
history.users.purge <public-key-or-unique-prefix>
history.users.purge all
history.users.purge.inactive 30
get history.clock
history.clock.set <UTC-epoch-seconds>
```

Settings are saved before `OK`; invalid values or a failed save leave playback
unchanged. A changed cap applies to the next login session. Diagnostics report
archive count, playback, member count/state, index/table memory, PSRAM/internal
allocation, bytes and errors. Read-only diagnostics do not dump message bodies,
passwords or private keys.
`get history.stack` reports the loop task's minimum remaining stack in bytes
during this boot, including startup recovery.

## Returning users and fresh joins

Membership is identified by the companion's **full public key**. Renaming it
keeps its membership. A new key with the same display name is a new member.
Each saved member has a first-join sequence floor, actual confirmed delivery
sequence/timestamp and last successful login date. The registry holds up to
256 members separately from the live room ACL/client limit.

A new member receives only posts committed **after its first join**. Returning
members recover their saved progress even when their companion reports
`since=0`. A stale cursor cannot rewind saved progress or bypass the join floor.
Cap skips and the join floor are distinct from ACKed delivery. Posts older than
the retained 2,000 are unavailable.

The server saves matching delivery ACKs before advancing its durable cursor.
Storage failure stops durable progress and is diagnosed; retries may occur.
If tracking is full, new normal joins are rejected without evicting existing
members. Authenticated administration remains accessible for cleanup, though
that admin session may have no playback while tracking is unavailable/full.

## Listing and forgetting members

Listing returns one bounded row per zero-based page: full key, delivered room
timestamp, trusted login UTC or `unknown`, page/count and registry revision.
If membership changes during pagination, restart with page 0.

Purge accepts a full public key or a **unique prefix of at least 12 hex digits**.
Short, malformed, absent and ambiguous keys are rejected. Clearing everyone
requires the literal `all`. Names are not identifiers for this command.

A successful purge invalidates affected playback and pending ACKs. A forgotten
member's next join/check-in starts fresh at the archive's then-current end,
excluding posts between purge and rejoin too. Purge changes delivery tracking
only: it does not delete posts, change passwords/permissions, or revoke access.
Bulk purges switch one checked registry generation atomically.

## Login dates and trusted time

This XIAO starts with an **untrusted** date after every reboot. Saved message
times and the MCU's default clock cannot determine how long power was off.
Set `history.clock.set` to a real UTC epoch, or successfully use the existing
administrator `time <epoch>`/remote `clock sync` command. `get history.clock`
shows whether a trusted administrator time is available during this boot.

UTC advances from that anchor using 64-bit monotonic uptime. The separate
history clock can be corrected backwards without rewinding message timestamps.
No trusted flag survives reboot. A login during untrusted time makes that
member's latest login date unknown; keep-alives/posts do not count as logins.

`history.users.purge.inactive <days>` is manual, accepts whole days from 1 to
36,500, and refuses to change anything without trusted time. It removes members
whose known last login is at least that old; unknown and future dates are
skipped and counted separately. There is no automatic expiry timer.

## Storage and recovery

No repartitioning is required. The existing 8 MiB flash layout keeps both OTA
application slots and SPIFFS at **0x670000 / 0x180000 bytes**. History uses bounded
`/rh_s_*` segments and two `/rh_ctl_*` reservation copies. `/rhu_s_*` snapshots
and `/rhu_l_*` mutation logs store membership separately from `/s_contacts`.

Budgets are 512 KiB for posts and 128 KiB for membership, combined 640 KiB.
Growth also requires projected filesystem use at most 65% and at least 256 KiB
free. The compact 2,000-entry index is at most 80,000 bytes; the 256-member table
is at most 32 KiB. Allocation prefers PSRAM. Checked internal fallback must
still leave at least 96 KiB free and a 32 KiB largest internal block.

Writes check byte counts, flush, backend cache completion, close and readback.
ESP-IDF 4.4 SPIFFS has no VFS `fsync` hook; its checked `SPIFFS_close` flushes the
write cache. Unsupported `fsync` alone is accepted only with successful flush
and close; real synchronization/close errors still fail the write. The enhanced
room uses a 16 KiB loop-task stack and a temporary heap-based startup inventory.
See the [ESP-IDF SPIFFS adapter](https://github.com/espressif/esp-idf/blob/v4.4.7/components/spiffs/esp_spiffs.c)
and [SPIFFS close implementation](https://github.com/pellepl/spiffs/blob/master/src/spiffs_hydrogen.c).
Versioned records
include CRCs, identity binding and commit trailers. A reservation precedes each
post so retries/restarts cannot reuse a committed timestamp. Recoverable
incomplete tails are sealed; healthy archive opening does not rewrite posts.
Member logs compact at 40 KiB through checked bounded generation snapshots.
Newer committed snapshots/epochs prevent forgotten members from reappearing.

Startup mounts without automatic formatting. Only a partition whose entire
validated extent reads as erased may be formatted for initial provisioning.
Nonblank mount faults, missing/corrupt identity alongside existing files, foreign
history identities, unknown schemas and unrecoverable corruption preserve files
and report recovery. Mount/identity/preferences recovery disables normal room
traffic; USB reports the reason and allows reboot. Journal/member faults leave
administration reachable but refuse unavailable post/playback operations.
Do not erase a failed node as a first diagnosis step; inspect/backup its storage.

Checked application writes do not make SPIFFS immune to filesystem-level
corruption or prove physical power-loss durability. Archives and key/config
files are **plaintext at rest**. These changes do not add flash encryption.

## Updates and compatibility

Use a compatible application-only update, leaving the browser installer's
**Erase data** unchecked. Identity, radio, passwords, peripherals and ACL formats
are preserved. A fresh erase deletes them and history. The initial update cannot
recover the old 32-post RAM cache after reboot, or reconstruct previously
unsaved non-admin memberships; those unrecorded keys first join as new members.

Older firmware has no support for these history/member files. A downgrade leaves
them on flash but cannot keep them current; returning to history firmware may
resume its older saved state. Back up before rollback or recovery. Do not
manually delete reservation/checkpoint files or copy another server's archive.

Password-restricted rooms use the same history behavior and existing per-client
encrypted radio transport. Saved history membership **never grants access**.
`set guest.password <password>` plus `set allow.read.only off` restricts unknown
visitors; existing ACL authorizations/blank-password known-key logins remain
separate. Revoking a key requires the existing access-management commands.
A history purge is not revocation, and knowing a valid password permits rejoin.

Enhanced companions ACK only accepted room messages; full queues pause catch-up
until the phone drains them and checks in again. A matching content-validated
retry can ACK without another queue entry. A LoRa ACK proves companion
acceptance, **not durable phone storage or display**. Companion reboot can lose
its accepted RAM queue. Stock/third-party companions may ACK discarded frames;
the server remains wire compatible but cannot guarantee lossless large catch-up
through those builds. Radio pacing is still at least 1.2 seconds per push,
about 40 minutes for 2,000 posts to one recipient before retries/airtime limits.

## Verification status

Native tests exercise production codec/journal/member/clock/admin/playback and
room receive-to-commit-to-ACK code with injected storage faults. Delivery tests
use actual BaseChatMesh receive/ACK scheduling and the shared companion frame
render/queue/acceptance code with mocked radio/cryptography. The integrated
harness covers 2,000 posts each to two recipients, full 16/256 queues,
drain/resume and saved-cursor restoration. These are software checks.

Hardware smoke checks on one XIAO room confirmed retained identity/settings,
DHT11 readings, PSRAM allocation, playback saves at 2,000/default 100, and three
test posts/timestamp recovery across soft reboots. Startup stack exhaustion and
the unsupported VFS sync call found by those checks were corrected. These smoke
checks do not establish 2,000-post physical capacity or power-cut durability.

A subsequent user-performed power cycle retained all 204 stored posts, the
returning member's saved delivery position and playback=200, with no history
recovery errors. One server-generated post was acknowledged by the companion
and confirmed visible in the phone app. A numbered 200-post catch-up test is
in progress; observed delivery acknowledgements advance and phone arrival
pacing varies. This does not yet establish a complete, gap-free phone transcript.

Full 2,000-post physical capacity, large phone-queue draining, stock-companion
comparison, fresh-node provisioning, loaded internal heap/PSRAM/SPIFFS/GC timing,
and sacrificial-device power interruption DURING writes remain pending. Ordinary
power-cycle recovery does not prove those fault cases. Do not describe them as
passed from native tests or successful compilation.
