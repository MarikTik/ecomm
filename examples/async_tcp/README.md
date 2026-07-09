# Async TCP example — Raspberry Pi ⇄ ESP32

An end-to-end demo of the ecomm protocol over an asynchronous TCP link,
carrying **serialized structured data**. An **ESP32 runs the server**; a
**Raspberry Pi (or any computer) runs the client**. The Pi serializes a small
`{name, age}` record into a packet; the ESP32 parses it and replies with a
greeting that uses the name.

```
  Raspberry Pi / PC                              ESP32
  ─────────────────                             ───────
  ecomm.channels                                ecomm::channels
  AsyncTcpChannel   ── TCP connect ──▶           esp_async_wifi_channel
   (pi_client.py)   ── {name:"Mark", age:22} ──▶  (esp32_server.cpp)
                    ◀── "hello Mark, it's your esp" ──
```

Both sides speak the **same fixed-size ecomm packets** — no framing bytes, no
length prefix, just `packet_size` bytes back to back. The ESP32's
`esp_async_wifi_channel` accepts one client at a time and drives the TCP
stack from a background task; the Pi's `AsyncTcpChannel` suspends on the
event loop until a full packet arrives (no polling). Neither side blocks.

ecomm carries the packet **payload as opaque bytes** — it guarantees the
header and hands you a raw payload region; *how you lay out that region is
your application's choice*. This example shows exactly that: a hand-rolled
little record format packed on the Pi and parsed on the ESP32.

## Files

| File | Runs on | Role |
|------|---------|------|
| [`esp32_server.cpp`](esp32_server.cpp) | ESP32 (Arduino framework) | TCP server: parse record, reply greeting |
| [`pi_client.py`](pi_client.py) | Raspberry Pi / any computer | TCP client: serialize record, print greeting |
| `README.md` | — | this file |

## The application payload layout

This is *our* format, layered inside ecomm's opaque payload (little-endian,
matching ecomm's wire convention):

```
request  (Pi → ESP32):   [ name_len : u8 ][ name : name_len bytes ][ age : u8 ]
reply    (ESP32 → Pi):   [ greeting_len : u8 ][ greeting : greeting_len bytes ]
```

For `name="Mark", age=22` the request payload is 6 bytes —
`04 4D 61 72 6B 16` (`len=4`, `"Mark"`, `age=22`) — and the rest of the
64-byte packet's payload stays zero. The ESP32 **bounds-checks every field
against the payload size before reading it** (see `parse_person` in the
firmware); never trust lengths that came off the wire. A wider age field
would just swap the `u8` for a little-endian `u16`/`u32` on both sides
(`struct.pack("<H", …)` in Python, a `memcpy` of the integer in C++).

## The one rule that matters: matching schemas

The two sides are wire-compatible **only if their packet configuration
matches exactly**. On the C++ side this is the `packet<...>` template; on the
Python side it is the `PacketSchema(...)`. They must agree field-for-field:

| Property | C++ (`esp32_server.cpp`) | Python (`pi_client.py`) |
|----------|--------------------------|-------------------------|
| total size | `packet<64, ...>` | `packet_size=64` |
| topology | `topology::network` | `Topology.NETWORK` |
| sequence | `no_sequence` | `SequencePolicy.NO_SEQUENCE` |
| checksum | `none` | `ChecksumPolicy.NONE` |
| ESP board id | `ECOMM_BOARD_ID` (default `1`) | `ESP32_BOARD_ID = 1` |
| Pi board id | (the sender it replies to) | `PI_BOARD_ID = 2`, `board_id=…` |

Notes:

- **Why `none` checksum?** TCP already guarantees delivery and integrity, so
  a per-packet FCS is redundant overhead. (Over a lossy link — e.g. serial —
  you would use `crc16` on both sides instead.)
- **Why `network` topology?** It carries `sender_id` / `receiver_id`, so the
  ESP32 knows who to reply to and each side can filter packets addressed
  elsewhere. The Pi addresses `receiver_id = 1` (the ESP); the ESP replies
  with `receiver_id = 2` (the Pi).
- **Word alignment:** `packet<>` on the ESP32 `static_assert`s that the size
  is a multiple of the 4-byte word. `64` satisfies that; if you change it,
  keep it a multiple of 4.

Change any row on one side and you must change it on the other, or the bytes
will be misinterpreted.

## Running the ESP32 server

It's an Arduino-framework sketch (written as `.cpp`). With
[PlatformIO](https://platformio.org):

1. Create a project for your ESP32 board and drop `esp32_server.cpp` into
   `src/`.
2. Edit `WIFI_SSID` / `WIFI_PASSWORD` at the top of the file.
3. Use a `platformio.ini` like:

   ```ini
   [env:esp32dev]
   platform = espressif32
   board = esp32dev
   framework = arduino
   lib_ldf_mode = deep+          ; trace the header-only includes
   lib_deps =
       https://github.com/MarikTik/ecomm.git
       https://github.com/MarikTik/etools.git   ; ecomm depends on these
       https://github.com/MarikTik/eser.git     ; two header-only libs
       esp32async/AsyncTCP                        ; the ESP32 async TCP stack
   ```

   (ecomm is header-only and pulls in `etools` + `eser`; all three are
   included via `#include <ecomm/...>` / `<etools/...>` once on the include
   path.)
4. Build & upload, then open the serial monitor at **115200 baud**. On boot
   it prints the IP address and port to point the client at:

   ```
   [ecomm async_tcp] connected. Point the Pi client at 192.168.1.50:3333
   [ecomm async_tcp] listening as board id 1
   ```

## Running the Pi client

Install the `ecomm` Python package (see the top-level
[`ecomm-python/README.md`](../../ecomm-python/README.md) — until it's on
PyPI, add it from this repo), then run the client with the IP the ESP32
printed:

```bash
python pi_client.py --host 192.168.1.50 --name Mark --age 22
```

(`--name` and `--age` default to `Mark` / `22`.) Expected output on the Pi:

```
[ecomm async_tcp] connected to 192.168.1.50:3333 as board 2
[send] to board 1: name="Mark", age=22 (6 payload bytes)
[recv] from board 1: "hello Mark, it's your esp"
[ecomm async_tcp] done
```

and on the ESP32 serial console:

```
[recv] from board 2: name="Mark" age=22
[send] to board 2: "hello Mark, it's your esp"
```

## Verifying the client without an ESP32

You don't need hardware to check that the client half works. A tiny local
`asyncio` server that speaks the same wire schema stands in for the ESP32
perfectly — decode each 64-byte packet with `Packet.from_bytes(SCHEMA, raw)`,
parse the `[name_len][name][age]` record, reply with a length-prefixed
`hello <name>, it's your esp` packet addressed back to `header.sender_id`,
and point `pi_client.py --host 127.0.0.1 --port <that server>` at it. This
example's client was validated exactly that way (record in, greeting out,
`{name: "Mark", age: 22}` parsed correctly) before being committed.
