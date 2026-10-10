# Bots connected directly to a room by USB

The Enhanced XIAO room firmware provides a USB interface for a Raspberry Pi or
other computer to read room posts and publish replies. **No extra companion
radio is required.** The room still handles normal encrypted radio delivery to
its members. This interface is for the direct USB connection, not remote radio
administration, Ethernet CLI, or companion firmware.

Local room builds identify the feature as `v1.17.1-sq5-bot`. Other XIAO room
features, identity/settings, journal formats and the phone/radio protocol are
unchanged. Update the same room role without erasing data; see the
[installation guide](enhanced_builds.md).

## Run the supplied monitor or sample bot

The computer-side code is portable Python 3.9+ with `pyserial`; SQLite and the
other dependencies come from Python's standard library. A Raspberry Pi Zero
running a suitable Raspberry Pi OS should be able to run this lightweight USB
worker. Use its USB data/OTG port in host mode with an appropriate adapter and
provide adequate power for both boards; see the [Raspberry Pi setup guide](https://www.raspberrypi.com/documentation/computers/getting-started.html).
Windows USB/phone tests passed; execution
on an actual Pi Zero remains unverified. Heavy local AI inference is a separate
resource requirement; the bot can instead call a model hosted elsewhere.

Use a checkout containing this feature and Python 3.9 or later. On your Pi:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r tools/room_bot/requirements.txt
python -m tools.room_bot --port /dev/ttyACM0 --state ~/room-monitor.sqlite3 --mode monitor
```

Run from the repository root. Replace the port with the room's actual USB port;
on Windows the equivalent example is `--port COM26`. Linux USB permissions may
require your account to belong to the serial-device group. Close web
configuration/serial terminals before starting: one process owns the USB port.

To run the sample bot instead:

```sh
python -m tools.room_bot --port /dev/ttyACM0 --state ~/room-bot.sqlite3 --mode bot --name SquishyBot
```

From a normal logged-in room member, use:

| Message | Reply |
| --- | --- |
| `/bot ping` | `[SquishyBot] pong` |
| `/bot help` or `/bot` | Available commands |
| `/bot about` | Identifies the Python USB room bot |

Commands are case-insensitive and accept surrounding/repeated whitespace. Only
messages starting with a separate `/bot` token address this sample bot. Bare
`/ping`, `/help`, ordinary conversation and `/botping` get no reply. Unknown verbs
or extra arguments get a short help/usage response. `--mode ping` remains a CLI
alias for `--mode bot`; it uses the same new `/bot` message syntax. Replies
appear under the **room server's identity**; the prefix is a label, not a new
cryptographic identity. System/room-authored posts are ignored to prevent loops.

The default mode is a monitor that prints incoming user posts without replying.
First use of a new state file starts at the current archive end, so a newly
started bot does not unexpectedly answer old conversations. Existing state
resumes its saved position. Add `--replay` only if you intentionally want a
**new state file** to process retained history; it does not reset an existing
cursor. `--allow-gap` explicitly accepts missing posts before the retained
archive's oldest sequence. Otherwise a retention gap stops with an explanation.

Default polling is once per second in pages of four posts; maximum page size is
eight. Replies are spaced at least five seconds apart in a running process.
`--poll-interval` and `--reply-interval` can adjust these (minimum 0.2 and 1 second
respectively). USB polling does not itself transmit LoRa packets. Publishing a
reply creates a normal room post which the room sends to eligible members.

## Connect your own chatbot

Create an importable Python module, for example `my_chatbot.py` in the repository
root, containing:

```python
def reply(post):
    # post.sequence, post.timestamp, post.author (full public key), post.text,
    # post.sender_timestamp, post.kind and lossless post.raw_text are available.
    if post.text.strip().lower() == '/bot hello':
        return 'Hello from the connected computer!'
    return None
```

Start it with:

```sh
python -m tools.room_bot --port /dev/ttyACM0 --state ~/my-chatbot.sqlite3 --handler my_chatbot:reply --name MyBot
```

The callback returns `None`, one string, or a list/tuple of up to four strings.
Every reply including its prefix must fit **151 UTF-8 bytes**, not characters.
Empty replies and NUL bytes are rejected. A failed callback or invalid response
does not advance that input's cursor. You can call your selected local model or
AI service inside this callback; no AI provider, credentials or network calls
are included here. Make external side effects idempotent using `post.sequence`:
a crash before the callback's result is saved can cause it to run again.

For direct programmatic use:

```python
from tools.room_bot import RoomClient, SerialTransport, BotState, BotWorker

transport = SerialTransport('/dev/ttyACM0')
state = BotState('/home/pi/room-bot.sqlite3')
try:
    worker = BotWorker(RoomClient(transport), state, reply, prefix='[MyBot] ')
    worker.step()  # call repeatedly; the CLI supplies the polling/reconnect loop
finally:
    transport.close()
    state.close()
```

Keep one worker per state file and USB port. Multiple bot functions can be
combined in one callback; they cannot independently open the same physical port.

## Progress, retries and storage

The computer uses SQLite transactions to save input progress and pending replies
together, before attempting to send them. Pending replies drain before new
inputs. A lost USB receipt preserves the same request ID and text; retries
return the original committed post instead of publishing a duplicate, including
after a room or computer restart. Reply ordering is preserved within a callback.

Deduplication uses the room's existing retained journal, not an unbounded new
flash log. **It lasts only while the request's post remains in the retained
2,000-post archive.** If an ambiguous pending retry outlives that window, the
client stops for manual inspection instead of risking a duplicate. Reusing an
ID with different text reports `submission conflict`; state is not discarded.
USB I/O timeouts/disconnections reconnect with backoff. Protocol/storage/schema,
identity mismatch and rollback errors stop without clearing progress/outbox.

Use a separate state file for another room. If the room is reset or rolled back
behind your saved cursor, inspect it and choose a new state file intentionally;
the client never silently resets its cursor. Preserve the SQLite database and
any `-wal`/`-shm` files together while the process is running, or stop it first
for a backup. They can contain message replies in plaintext. SQLite and checked
room writes do not guarantee against every physical storage/power-loss fault.

## USB protocol v1

Commands end with CR or CRLF. Responses are ASCII JSON lines prefixed `@bot `;
ignore command echo, blank lines and ordinary diagnostic output. One request is
in flight at a time. All 64-bit sequences and request IDs are decimal strings.
Text is hex-encoded raw UTF-8 bytes, preserving quotes, newlines and non-ASCII
text without allowing it to become a console command.

| Command | Response |
| --- | --- |
| `bot.info` | `type=info`, `api=1`, full `room_key`, count, oldest/latest/high_water sequences, page/text limits |
| `bot.read <after> [limit]` | Zero to eight `type=post` rows, followed by one `type=end` page receipt |
| `bot.post <request-id> <text-hex>` | `type=posted`, matching request ID, committed sequence/timestamp and duplicate flag |

`bot.read` selects sequence **greater than** `after`, oldest first. The default
limit is four; accepted limits are 1..8. Each post includes full `author`,
`sequence`, server `timestamp`, `sender_timestamp`, `kind` and `text_hex`. The
end row includes `after`, `next`, `oldest`, `latest`, `high_water`, `count`, `gap`
and `more`. A page is valid only after its end receipt; discard a partial page
after timeout/error. Read-only polling never advances a room member's cursor.

`high_water` includes reserved sequence gaps; it may exceed the newest retained
post. A cursor above it is rejected. A gap indicates a cursor before the oldest
retained sequence minus one; sequence reservations can also create holes, so a
gap is conservative. Sequence IDs identify progress; timestamps are not relied
on for USB resume. They may be logical monotonic room timestamps when RTC time
has not been set.

Request IDs are nonzero unsigned 32-bit integers; text is 1..151 bytes without
NUL. The Python client allocates a random request ID and persists it before
sending. Bot submissions reuse the existing kind=0 room-author record, with
`sender_timestamp` holding the request ID. They remain readable by earlier
history firmware; there is no schema migration. Human `room.post` system records
still use kind=1. The worker ignores both by checking kind/full author.

Errors have `type=error` and an `error` string. Oversized/invalid input is discarded
through its line ending; it cannot execute a truncated command. Every page is
bounded and the radio loop runs between commands. USB output failure omits a
successful terminal response; the host retains pending work for retry.

Direct USB is an administrative connection and can read all retained posts.
Bot reads/posts are unavailable to RF/Ethernet CLI, including authenticated
radio admins. Ordinary command-looking guest posts remain plain room messages.
Existing history access/encryption and plaintext-at-rest limitations still apply.

## Verification

Firmware tests cover paging, retention gaps, maximum/control/Unicode text,
invalid inputs, storage faults, lost receipts, durable duplicates, and bounded
line parsing. Python tests cover default/replay resume, identity/rollback/gap
checks, lost receipts, transactional outbox, callback failures, reply ordering,
rate limiting and fragmented serial responses. Physical verification results
are recorded separately; software tests do not prove phone display or hardware
power interruption during writes.

Physical USB checks on the connected XIAO S3 room (`v1.17.1-sq5-bot`) preserved
identity/settings and all 204 existing posts, including the ordered 200-post
catch-up batch. A single labelled 151-byte bot post was read back byte-for-byte;
exact retries before and after soft reboot returned that same post (205 total).
The host's saved cursor resumed and ignored its own reply. Oversized input was
rejected without changing history. Hardware testing found the default 256-byte
ESP32-S3 USB RX buffer was too small for hex submissions; this build explicitly
allocates and checks a 1,024-byte buffer before starting serial.

These checks used the Python client on Windows. A subsequent real member's
`/ping` through the phone app produced `[SquishyBot] pong`; the companion
acknowledged it and the user confirmed it appeared in the room chat. Execution
on an actual Raspberry Pi remains unverified.

The original phone acceptance test used the earlier `/ping` spelling. Current
sample commands require `/bot ping` or `/bot help`; this change is entirely on
the computer and requires no room firmware update.

## XIAO USB reply completion

The pinned Arduino 2.x HWCDC driver can leave a final full 64-byte USB packet
pending until another write. The room uses `UsbReplyStream.h` to drain each
bounded JSON line, then wait at most 100 ms for the hardware FIFO to become
writable and flush the terminating packet. It then yields for two milliseconds
to let the legacy driver finish handling the acknowledgement before another
write. This follows [Espressif's S3 HAL
requirement](https://github.com/espressif/esp-idf/blob/master/components/esp_hal_usb/esp32s3/include/hal/usb_serial_jtag_ll.h).
The low-level step is guarded to USB-Serial/JTAG mode; it does not run for UART
or USB OTG. Setup checks an 8,192-byte transmit buffer allocation alongside the
existing 1,024-byte receive buffer and enters recovery on allocation failure.
The generic bot JSON, cursor checks and short-write failure behaviour remain
unchanged. This is an application-level adapter, not a modified toolchain cache.
