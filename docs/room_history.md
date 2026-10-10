# Persistent XIAO room history

The standard `Xiao_S3_WIO_room_server` build saves a configurable archive of
**1 to 2,000 posts** in flash (`chat_HistoryAmount`, default **2,000**).
Normal catch-up has its own **1 to 2,000** limit (`chat_ReplayAmount`, default
**100**). Existing saved playback values, including 200, are preserved. It sends
the newest eligible missing posts oldest first,
excluding the recipient's own posts before applying the limit. Live posts after
the login snapshot are additional. Ordinary keep-alives resume that window;
they do not open endless extra batches of older history.

The four standard XIAO companion builds include queue backpressure and retry
deduplication. The radio format and existing phone interface are unchanged.
Other room boards retain their original 32-post RAM implementation. Local
`*_legacy` regression targets are excluded from the six published images.

For a computer/Pi connected directly to the room USB, the
[USB bot interface](room_bot.md) additionally provides bounded reads of post
contents and retry-safe replies. It is not exported through remote administration.

## Commands

Use the room's USB text console or its authenticated remote **administrator**
command line. These are not local companion console commands. Guest posts that
look like commands remain ordinary text.

```text
get chat_ReplayAmount
set chat_ReplayAmount 200
get chat_HistoryAmount
set chat_HistoryAmount 2000
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

`history.playback` remains an alias of `chat_ReplayAmount`; `default` resets
replay to 100 or archive retention to 2,000 respectively. Setting names also
accept `chat.replayAmount` and `chat.historyAmount`, ignoring name case. Values
are whole numbers only. The two settings are independent: increasing replay
does not increase retention, and the available archive bounds any catch-up.

Settings are saved before `OK`; invalid values or a failed preference save
leave the previous values unchanged. A changed replay cap applies to the next
login session. Changing retention applies immediately. Reducing it expires old
posts; increasing it does **not** restore expired posts, including after reboot.
Physical bytes in a partly retained segment may remain until that segment is
pruned; retention is not secure erasure. A later journal or cleanup failure can
leave the new preference saved with history in recovery; inspect the error.

 Diagnostics report
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
the configured retained archive are unavailable. Explicit administrator replay
below can bypass the normal catch-up window, delivered watermark and join floor
for an existing, currently authenticated recipient.

The server saves matching delivery ACKs before advancing its durable cursor.
Storage failure stops durable progress and is diagnosed; retries may occur.
If tracking is full, new normal joins are rejected without evicting existing
members. Authenticated administration remains accessible for cleanup, though
that admin session may have no playback while tracking is unavailable/full.

## Saved aliases and administrator replay

Phone display names are not saved by the room. Assign a separate alias to the
full public-key identity, or address a member by its full key or a unique prefix
of at least 12 hex digits. These commands work in the room's **administrator
console or USB console**, never as commands typed into the room conversation:

```text
chat users
chat users 1
chat user.alias <full-public-key-or-unique-prefix> Falcz
/chat replay Falcz 500 200
chat replay.status Falcz
chat replay.cancel Falcz
chat aliases
chat aliases 1
chat user.alias <full-public-key> -
```

`chat users` and `chat aliases` return one row per zero-based page. Aliases are
saved across restarts, unique without regard to case, and limited to 31 ASCII
letters, digits, underscores or hyphens. An all-hex alias of 12 or more characters
is rejected to avoid confusion with key prefixes. `-` removes an alias. Up to
256 aliases are saved. Purging a member retains its alias but grants no access:
`chat aliases` marks it `member=no`, and its full key still allows alias removal.
Rejoining with that same key uses the alias again, with fresh normal membership.

`chat replay USER START_OFFSET END_OFFSET` counts actual retained posts from the
latest post **at request time**. Offset **0 is newest**. Both endpoints are
inclusive, and START must be at least END. `500 200` queues **301 posts**, oldest
first. `1999 0` requests the whole archive when 2,000 posts are retained. A range
outside the available archive is rejected. Sequence gaps do not change counting,
and new arrivals do not move an already queued range.

The target must be logged in with room access. An outstanding delivery ACK or
active replay returns `ERR delivery busy`; retry after delivery completes, or
cancel the replay. Alias assignment never grants permission. A queued range is
a temporary job, discarded on reboot, session expiry/relogin or member purge.
New arrivals or a retention reduction can expire queued posts before delivery;
status reports remaining/expired counts. Normal live/catch-up delivery resumes
when the manual job completes, expires or is cancelled.

Manual replay preserves original sequence IDs, authors, text and wire timestamps,
and can include the recipient's own posts, already delivered posts and pre-join
posts. Replay ACKs update only this temporary job: they neither advance nor rewind
the saved normal delivery cursor. Consequently, a manually replayed unread post
can also appear later through normal catch-up. Companion/app duplicate handling
may suppress a post already present on the phone or place it at its original date.

SQ7 enhanced companions accept older authenticated room posts while keeping
`sync_since` monotonic. Install the updated companion too if it currently runs
SQ4 history firmware: its stale-timestamp rejection would prevent backfill.
Immediate matching retries are ACKed without another queue entry. Nonconsecutive
old duplicates may reach the phone; the unchanged radio protocol cannot label a
frame as an intentional replay versus a delayed original.

## Reading the archive over USB

For a person using the USB text console:

```text
chat history 0 8
chat history <next-id-from-previous-page> 8
```

AFTER_ID is exclusive, and 0 starts at the oldest retained post. Each bounded
page contains 1-8 posts (default 4) and ends with `next`, `count`, `more` and `gap`.
Continue with the returned `next` value until `more=no`. Example output:

```text
@chat seq=500 timestamp=1791600000 author=<full-public-key> text=Hello
@chat end next=507 count=8 more=yes gap=yes
```

The journal's **64-bit sequence ID** increases for each reserved post; interrupted
writes can leave gaps. IDs are never reused or rewritten for playback. The wire
timestamp is separately kept increasing and is not always a trusted civil date.
Newlines, backslashes and terminal control characters in text are escaped.
Reading changes neither saved delivery progress nor message contents. Remote
administration returns `ERR USB only` for this command. For software, keep using
the existing bounded `bot.read` interface in the [USB bot guide](room_bot.md).

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
administrator `time <epoch>`/remote `clock sync` command, or enable optional
[Wi-Fi clock sync](wifi_time.md). `get history.clock` shows whether a trusted
civil-time anchor is available during this boot.

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
Two additional room-bound `/rha_a` and `/rha_b` alias snapshots use at most
32,896 bytes together; the alias RAM table adds at most 16 KiB. Checked writes
alternate copies and verify readback before reporting success. Public-key
commands still work if the optional alias table needs recovery.
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
post so retries/restarts cannot reuse a committed timestamp. Automatic expiry
is committed only after the replacement post is verified; a failed append cannot
prematurely discard the oldest committed post. This adds a checked control write
on eviction. Startup recovers a cut between post commit and expiry commit. Recoverable
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

SQ7 reads existing 64-byte v1 journal reservation controls and upgrades them to
80-byte v2 controls on the next reservation. V2 saves an exclusive retention
floor on both explicit reductions and automatic eviction, preventing expired
records from reappearing after retention is enlarged or power is lost. Existing
post/member records and identities remain unchanged.

**SQ6 and earlier history builds cannot read v2 controls.** Back up the device's
filesystem before upgrading. Rolling back after a v2 write requires restoring
that matching pre-upgrade filesystem backup as well as the old application;
application-only rollback will leave history in recovery. Restoring a backup
also loses posts and progress recorded since it. Older upstream room firmware
does not maintain these archive/member files. Do not manually delete reservation
files or copy another server's archive.

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
room receive-to-commit-to-ACK code with injected storage faults. SQ7 checks also
cover v1 control migration, retention reduction/growth/restart without resurrection,
all 256 saved aliases, corrupt/failed snapshots, 301-post offset replay across
sequence gaps, independent ACK cursors, cancellation/purge/rotation, USB pages
and authenticated older-frame acceptance with full companion queues. Physical
SQ7 targeted backfill and alias power-cycle testing remains pending. Delivery tests
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
and confirmed visible in the phone app. The numbered 200-post catch-up batch
has now been acknowledged through its last post by the companion; USB inspection
also confirmed all 200 stored records are complete and ordered. Phone arrival
pacing varied. This does not yet establish a complete, gap-free phone transcript.

Full 2,000-post physical capacity, large phone-queue draining, stock-companion
comparison, fresh-node provisioning, loaded internal heap/PSRAM/SPIFFS/GC timing,
and sacrificial-device power interruption DURING writes remain pending. Ordinary
power-cycle recovery does not prove those fault cases. Do not describe them as
passed from native tests or successful compilation.

### Optional Wi-Fi UTC time

The standard XIAO room can establish the trusted civil clock after restart using
one saved Wi-Fi network. It leaves Wi-Fi off between bounded sync attempts.
See the [Wi-Fi clock guide](wifi_time.md) for setup; the existing manual time
commands remain available. Stored journal timestamps are not rewritten by sync.

An SQ7 application-only hardware update on COM26 preserved all 223 prior posts,
identity, settings and member progress. Startup clock sync added one room-authored
announcement. The saved replay limit remained 200; archive retention defaulted
to 2,000. New alias/user diagnostics and eight readable USB posts beyond sequence
200 were verified, and the existing USB bot reconnected with its saved SQLite
state. Matched rollback application/filesystem and WAL-consistent bot backups
were retained locally. This verifies upgrade and USB reads, not targeted radio
backfill or alias persistence through a power interruption.
