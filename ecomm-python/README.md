# ecomm — Python client for the ecomm wire protocol

**`ecomm`** (the Python distribution lives in this `ecomm-python/` directory, but the *package* is
named plainly `ecomm`) is a byte-exact, dependency-checked Python implementation of the
[ecomm](../README.md) wire protocol — the packet format, checksum algorithms, and channel framing
defined by the C++ `ecomm` library for message-driven communication with Arduino-class
microcontrollers (primarily the ESP family). It exists so a regular computer — a laptop, a CI
runner, a Raspberry Pi — can speak that exact protocol to real firmware, over a serial link or a
TCP/Wi-Fi link, with no intermediary translation layer and no drift between the two implementations.

Every algorithm and every byte layout in this package was **verified against the actual compiled
C++ output**, not re-derived from documentation or a spec. Where the C++ side uses compile-time
template parameters, this package uses a runtime configuration object (`PacketSchema`) that plays
the same role — resolved at object-construction time instead of compile time, because Python has no
template monomorphization.

```python
from ecomm.protocol import PacketSchema, Topology, SequencePolicy, ChecksumPolicy
from ecomm.protocol import HeaderType, HeaderOptions, Packet
from ecomm.channels import SerialChannel

schema = PacketSchema(
    packet_size=32,
    topology=Topology.NETWORK,
    sequence=SequencePolicy.SEQUENCED,
    checksum=ChecksumPolicy.CRC16,
    board_id=2,
)
packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
packet.header.receiver_id = 1
packet.payload[0:5] = b"hello"

with SerialChannel(schema, port="/dev/ttyUSB0", baudrate=115200) as ch:
    ch.send(packet)
    reply = ch.try_receive()
```

---

## Table of Contents

- [Overview](#overview)
- [Requirements](#requirements)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Core Concepts](#core-concepts)
  - [`PacketSchema` — the runtime stand-in for C++ templates](#packetschema--the-runtime-stand-in-for-c-templates)
  - [Wire-compatibility means schema equality](#wire-compatibility-means-schema-equality)
- [The Protocol Layer](#the-protocol-layer)
  - [Wire format reference](#wire-format-reference)
  - [`PacketHeader`](#packetheader)
  - [`Packet`](#packet)
  - [Checksum policies](#checksum-policies)
  - [`validator`: seal and is_valid](#validator-seal-and-is_valid)
  - [The error envelope](#the-error-envelope)
- [The Channels Layer](#the-channels-layer)
  - [Synchronous channels: `Channel`, `SerialChannel`, `TcpChannel`](#synchronous-channels-channel-serialchannel-tcpchannel)
  - [Async channels: `AsyncChannel`, `AsyncTcpChannel`](#async-channels-asyncchannel-asynctcpchannel)
  - [`ReliableChannel`: acknowledgement and retry](#reliablechannel-acknowledgement-and-retry)
- [Contracts, Typing, and Errors](#contracts-typing-and-errors)
  - [Design-by-contract with icontract](#design-by-contract-with-icontract)
  - [Runtime typing with beartype](#runtime-typing-with-beartype)
  - [The exception hierarchy](#the-exception-hierarchy)
- [Serialization is your responsibility](#serialization-is-your-responsibility)
- [Examples](#examples)
- [Edge Cases & Behavior](#edge-cases--behavior)
- [Assumptions & Limitations](#assumptions--limitations)
- [When to Use This (and When Not To)](#when-to-use-this-and-when-not-to)
- [Testing](#testing)
- [Project Layout](#project-layout)
- [Using This From Another Project](#using-this-from-another-project)
- [License](#license)
- [Contributing](#contributing)
- [Contact](#contact)

---

## Overview

- **Byte-exact, not merely compatible.** Every checksum algorithm and every header layout is
  checked against bytes produced by the real, compiled C++ `compute<ChecksumPolicy>` and
  `packet_header<...>` — see [Assumptions & Limitations](#assumptions--limitations) for exactly what
  "verified" means and does not mean here.
- **A runtime configuration object stands in for C++ templates.** `PacketSchema` is the one thing
  you must get exactly right for two peers to understand each other; see
  [Core Concepts](#core-concepts).
- **Contracts and runtime types throughout.** Every public constructor and method is guarded by
  [icontract](https://github.com/Parquery/icontract) preconditions/postconditions/invariants and
  checked by [beartype](https://github.com/beartype/beartype); see
  [Contracts, Typing, and Errors](#contracts-typing-and-errors).
- **Both a synchronous and an asyncio-native channel stack.** `SerialChannel`/`TcpChannel` poll;
  `AsyncTcpChannel` suspends on the event loop — no busy-waiting, no thread per connection. See
  [The Channels Layer](#the-channels-layer).
- **No dynamic allocation on the C++ side; no such constraint here.** This package runs on a
  general-purpose OS, so unlike the embedded C++ library it uses ordinary Python exceptions where
  they are the right tool — see [The exception hierarchy](#the-exception-hierarchy).

Two top-level packages:

| Package | Contents |
|---|---|
| `ecomm.protocol` | The wire format: `PacketSchema`, `PacketHeader`, `Packet`, checksum policies and algorithms, the validator, the error envelope, and the plain enums (`HeaderType`, `HeaderOptions`, `Topology`, `SequencePolicy`). |
| `ecomm.channels` | The transports: `Channel` (sync base), `SerialChannel`, `TcpChannel`, `AsyncChannel` (async base), `AsyncTcpChannel`, `ReliableChannel`, and `SendResult`. |

`ecomm.errors` (imported as `EcommError`, `MalformedPacketError` from the top-level `ecomm` package)
holds the small exception hierarchy described in
[The exception hierarchy](#the-exception-hierarchy). `ecomm._typing` is a private module holding the
package-wide `beartype` configuration — you never import it directly.

---

## Requirements

- **Python 3.10+.** The codebase uses `X | Y` union syntax and modern `dataclass`/typing features
  throughout.
- Three runtime dependencies, installed automatically: [`icontract`](https://pypi.org/project/icontract/)
  (contracts), [`beartype`](https://pypi.org/project/beartype/) (runtime type checking), and
  [`pyserial`](https://pypi.org/project/pyserial/) (`SerialChannel`'s transport).
- **[`uv`](https://docs.astral.sh/uv/)** is the tooling this package is built and tested with. Plain
  `pip install` from a built wheel also works — there is nothing `uv`-specific in the package
  itself, only in this repository's development workflow.

---

## Installation

```bash
uv sync
```

This creates (or reuses) a project-local virtual environment and installs `ecomm` in editable mode
plus its runtime dependencies. See [Using This From Another Project](#using-this-from-another-project)
for how to depend on it from a separate project.

---

## Quick Start

The opening example at the top of this file is the canonical "smallest complete program": build a
`PacketSchema`, build a `Packet` against it, send it over a channel, and (optionally) receive a
reply. Everything else in this README is either more detail on one of those four steps, or a
different channel to plug into the last one.

---

## Core Concepts

### `PacketSchema` — the runtime stand-in for C++ templates

On the C++ side, `packet_header<Topology, SequencePolicy, ChecksumPolicy>` and
`packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>` are compile-time template
instantiations: the compiler generates one concrete struct layout per unique combination of
arguments, and `sizeof(header)` / the payload size fall out as compile-time constants. There is no
way, in C++, to construct a `packet<>` without the compiler already knowing its exact wire layout.

Python has no template monomorphization, so `PacketSchema` plays the same role at
*object-construction* time instead:

```python
from ecomm.protocol import PacketSchema, Topology, SequencePolicy, ChecksumPolicy

schema = PacketSchema(
    packet_size=32,                          # total wire size, header included
    topology=Topology.NETWORK,               # sender_id / receiver_id present
    sequence=SequencePolicy.SEQUENCED,        # seq_num present (for ReliableChannel)
    checksum=ChecksumPolicy.CRC16,            # 2-byte FCS present
    board_id=2,                              # this node's identity
)
```

`PacketSchema` is a frozen (`@dataclass(frozen=True)`), `icontract`-validated value with two derived
properties:

| Property | Meaning |
|---|---|
| `schema.header_size` | `1` (protocol byte) `+ sequence.size + (2 if topology is NETWORK else 0) + checksum.size` |
| `schema.payload_size` | `packet_size - header_size` |

Constructing a `PacketSchema` enforces (via `icontract` invariants, so violations raise
`icontract.ViolationError` with the exact offending values in the message):

- `board_id` is in `[1, 254]` — `0` is reserved ("unassigned"), `255` is the broadcast address.
- `packet_size > header_size`, so at least one payload byte exists — this mirrors the
  `static_assert` in the C++ `packet.hpp`.

One thing the C++ side enforces that this package deliberately does **not**: word-alignment of
`PacketSize` (`PacketSize % sizeof(std::size_t) == 0`). That constraint exists because DMA and
serial drivers on the embedded target typically require it — it is about the *firmware's* memory
bus, not the wire format, and `sizeof(std::size_t)` differs between a 32-bit ESP target and a 64-bit
host anyway. Any `packet_size` that compiled successfully in the C++ firmware already satisfies it;
just pass the same value through.

### Wire-compatibility means schema equality

**Two `Packet`/`PacketHeader` instances are wire-compatible if and only if they were built from
*equal* schemas** — same field presence, same checksum algorithm, same total size. This mirrors the
C++ requirement that both peers link against `packet<>` instantiated with identical template
arguments. `PacketSchema` equality is ordinary dataclass field-by-field equality (two schemas built
with the same arguments are equal even if they are different objects), and it is enforced, not just
documented: every channel's `send()` has a precondition that `packet.schema == channel.schema`, and
every decode path (`Packet.from_bytes`, channel `receive()`/`try_receive()`) postconditions that the
result is bound to the schema it was decoded against. Getting this wrong is the single most common
way to misuse this package — see the row on schema mismatches in
[Edge Cases & Behavior](#edge-cases--behavior).

---

## The Protocol Layer

### Wire format reference

Every packet is a header immediately followed by a raw payload region — nothing else:

```
+------------------+-------------+------------------+-----------------+-------------------------------+
| proto byte  (1B) | seq_num (*s)| sender_id (*n)   | receiver_id (*n)| fcs  (checksum.size bytes $)  |
+------------------+-------------+------------------+-----------------+-------------------------------+
(*s) only present when schema.sequence == SequencePolicy.SEQUENCED
(*n) only present when schema.topology == Topology.NETWORK
($)  only present when schema.checksum != ChecksumPolicy.NONE
```

The protocol byte itself packs three fields:

```
 7..5 : type      (3 bits)  -- HeaderType enum, 6 values assigned (2 reserved)
    4 : error     (1 bit)   -- HeaderOptions.ERROR
    3 : ack       (1 bit)   -- HeaderOptions.ACK
    2 : encrypted (1 bit)   -- HeaderOptions.ENCRYPTED
 1..0 : version   (2 bits)  -- PROTOCOL_VERSION (currently 0), not caller-settable
```

`HeaderType` (6 assigned values; `0x6`/`0x7` are reserved and not decodable — see
[Edge Cases](#edge-cases--behavior)):

| Member | Value | Meaning |
|---|---|---|
| `DATA` | `0x0` | Generic application data. Most packets use this. |
| `CONTROL` | `0x1` | Protocol-level commands (reset, sync, configuration). |
| `AUTH` | `0x2` | Authentication or credential exchange. |
| `SESSION` | `0x3` | Session lifecycle: initiation, teardown, handshake. |
| `LOG` | `0x4` | Diagnostic log messages or telemetry. |
| `FIRMWARE` | `0x5` | Firmware image chunks or update-related payloads. |

`HeaderOptions` (an `IntFlag`, combinable with `|`): `NONE`, `ERROR`, `ACK`, `ENCRYPTED`.

`Topology`: `POINT_TO_POINT` (no addressing fields) or `NETWORK` (adds `sender_id`/`receiver_id`,
one byte each — `sender_id` defaults to `schema.board_id`, `receiver_id` defaults to `0` and must be
set by the caller, or left as `config.BROADCAST_ADDRESS` (`255`) to address every node on the link).

`SequencePolicy`: `NO_SEQUENCE` (no field) or `SEQUENCED` (adds a one-byte `seq_num`, required by
`ReliableChannel`, wrapping at 255).

### `PacketHeader`

`PacketHeader(schema, type_=HeaderType.DATA, options=HeaderOptions.NONE)` builds a header bound to
`schema`. Fields that don't apply to `schema` (e.g. `seq_num` when `schema.sequence` is
`NO_SEQUENCE`) still exist as ordinary Python attributes — always zero-initialized, mirroring the
C++ default constructor's postconditions — but `to_bytes()`/`from_bytes()` simply never touch them.

| Member | Kind | Meaning |
|---|---|---|
| `type` | property | Decoded `HeaderType` from bits 7..5. Raises `MalformedPacketError` for the two reserved encodings. |
| `raw_type` | property | The raw 3-bit field as `int` — never raises, use this to inspect a possibly-reserved type. |
| `options` | property | Decoded `HeaderOptions` from bits 4..2. |
| `has(option)` | method | `True` iff every bit in `option` is set. |
| `version` | property | The 2-bit version field. |
| `raw` | property | The full packed protocol byte. |
| `seq_num`, `sender_id`, `receiver_id`, `fcs` | attributes | Mutable; meaningful only when `schema` includes the corresponding field. |
| `to_bytes()` | method | Serialize to exactly `schema.header_size` bytes. |
| `PacketHeader.from_bytes(schema, data)` | classmethod | Deserialize from exactly `schema.header_size` bytes. |

### `Packet`

`Packet(schema, type_=HeaderType.DATA, options=HeaderOptions.NONE)` builds a header (as above) plus
a zero-initialized `payload: bytearray` of exactly `schema.payload_size` bytes.

| Member | Kind | Meaning |
|---|---|---|
| `schema`, `header`, `payload` | attributes | `payload` is a mutable `bytearray`; write into it with slice assignment. |
| `to_bytes()` | method | Serialize to exactly `schema.packet_size` bytes: header then payload. |
| `Packet.from_bytes(schema, data)` | classmethod | Deserialize from exactly `schema.packet_size` bytes. **Does not validate the checksum** — see [`validator`](#validator-seal-and-is_valid) — but every channel's `receive()`/`try_receive()` validates automatically. |

The payload has no schema of its own beyond its byte length — "the application may overlay any
structure on this region... the packet itself imposes no schema," exactly as in the C++
`packet.hpp`. See [Serialization is your responsibility](#serialization-is-your-responsibility).

### Checksum policies

`ChecksumPolicy` selects the algorithm that produces the header's trailing FCS field:

| Member | FCS width | Notes |
|---|---|---|
| `NONE` | 0 bytes | No checksum. Appropriate over a transport that already guarantees integrity (e.g. TCP). |
| `SUM8` / `SUM16` / `SUM32` | 1 / 2 / 4 | Additive sum, wrapping at the accumulator width. |
| `CRC8` / `CRC16` / `CRC32` / `CRC64` | 1 / 2 / 4 / 8 | Table-driven, MSB-first, `initial=0`, `final_xor=0` — **not** the same parameterization as `zlib.crc32` or any other named CRC variant; see below. |
| `FLETCHER16` / `FLETCHER32` | 2 / 4 | Fletcher checksum. |
| `ADLER32` | 4 | Modified Fletcher checksum, same algorithm as `zlib.adler32` (checked, matches bit-for-bit — but implemented independently; see [Assumptions & Limitations](#assumptions--limitations)). |
| `INTERNET16` | 2 | RFC 1071 one's-complement sum, as used by IP/TCP/UDP headers. |

**Why not delegate to a library?** CRC in particular has no single canonical form — it's a family
parameterized by polynomial, initial value, bit reflection, and final XOR. `zlib.crc32` implements
the common *reflected* IEEE 802.3 variant (poly `0xEDB88320`, init `0xFFFFFFFF`); ecomm's C++
`compute<crc32>` uses a *non-reflected* variant (poly `0x04C11DB7`, init `0`, no final XOR) with its
own literal lookup table. These are different checksums that happen to share a name and width —
verified by direct comparison, not assumed. Every algorithm in `ecomm.protocol.compute` is therefore
implemented directly and checked against the compiled C++ output, so correctness never depends on a
third-party library's version or parameter choices matching ecomm's exact variant.

### `validator`: seal and is_valid

Two free functions in `ecomm.protocol.validator`, mirroring `validator<Packet>` in `validator.hpp`:

- **`seal(packet)`** — computes and writes the FCS into `packet.header.fcs`. A no-op when
  `schema.checksum` is `NONE`. Must be called on every outgoing packet before it reaches a
  transport; every channel's `send()` calls it automatically.
- **`is_valid(packet) -> bool`** — recomputes the FCS over the packet with `fcs` treated as zero and
  compares it to the received value. Always `True` when `schema.checksum` is `NONE`. Leaves the
  packet unmodified (works on a local copy internally). Every channel's `receive()`/`try_receive()`
  calls this automatically and discards packets that fail.

The sealing contract, reproduced exactly from `validator.hpp`: zero `header.fcs`, hash all
`packet_size` bytes, write the result back. Because sealing always re-zeroes first, calling `seal()`
twice in a row on an unchanged packet is idempotent (produces the same FCS both times) — it is
**not** a "seal again to get a different value" operation.

### The error envelope

When a packet's header has `HeaderOptions.ERROR` set, its payload is reinterpreted as a length-
prefixed error record — a distinct, secondary use of the opaque payload region, defined by
`ecomm.protocol.error`:

```
+----------------+----------------------------+---------------------+----------+
|   error_code   |   length field             |    message bytes    |   pad    |
|   (uint16 LE)  |  (uint8/16/32 LE)           |    length bytes     |   ...    |
+----------------+----------------------------+---------------------+----------+
        2                  1, 2, or 4                  length          rest of P
```

The length-field width is selected from `max_message_length` (default `65535`, mirroring
`ECOMM_MAX_ERROR_MESSAGE_LENGTH`): `<= 255` → 1 byte, `<= 65535` → 2 bytes, `<= 4294967295` → 4
bytes. Both ends must agree on `max_message_length` for the width to match.

```python
from ecomm.protocol import HeaderType, HeaderOptions, Packet
from ecomm.protocol.error import ErrorCode, write_error, read_error

packet = Packet(schema, HeaderType.DATA, HeaderOptions.ERROR)
write_error(packet, ErrorCode.CHECKSUM_MISMATCH, b"bad crc")

view = read_error(packet)              # -> ErrorView | None
if view is not None:
    print(view.code, view.message)     # ErrorCode.CHECKSUM_MISMATCH, b"bad crc"
```

`ErrorCode` is an `IntEnum` whose top byte is a subsystem tag, exactly matching `error_code` in
`error.hpp`: `0x00xx` framing, `0x01xx` transport, `0x02xx` dispatch, and `0x4000`+
(`ErrorCode.USER_RANGE_BEGIN`) reserved for application-defined codes — `write_error`/`read_error`
accept a plain `int` there too, since no `ErrorCode` member exists for it.

`read_error(packet, require_error_flag=True)` requires (as an `icontract` precondition — a
programmer error, not a wire condition) that `packet.header.has(HeaderOptions.ERROR)`; pass
`require_error_flag=False` to decode without that check. A **malformed envelope** (declared length
overruns the available payload) is different: that's data that arrived off the wire, not a caller
mistake, so `read_error` returns `None` for it rather than raising — see
[The exception hierarchy](#the-exception-hierarchy) for why that distinction is drawn deliberately
throughout this package.

---

## The Channels Layer

A channel is a two-way, typed endpoint bound to one `PacketSchema`. Every channel composes the same
two things around a transport-specific byte primitive: **seal on send**, **validate + address-filter
on receive**. There are two independent hierarchies — synchronous and asyncio-native — because their
non-blocking-receive primitives have genuinely different shapes (see
[Async channels](#async-channels-asyncchannel-asynctcpchannel)), not because of a missing
abstraction.

### Synchronous channels: `Channel`, `SerialChannel`, `TcpChannel`

`Channel` is an abstract base (`ecomm.channels.base`) that concrete transports implement by
providing two primitives:

- `_do_send(data: bytes) -> None`
- `_do_try_receive(size: int) -> bytes | None` — return exactly `size` bytes if that many are
  already available, else `None` immediately. Never blocks, never returns a partial read.

On top of those, `Channel` provides the public surface:

| Method | Behavior |
|---|---|
| `send(packet) -> SendResult` | `seal()`s the packet, writes it, always returns `SendResult.OK`. Precondition: `packet.schema == self.schema`. |
| `try_receive() -> Packet \| None` | Reads if a complete packet is available; returns it only if it passes `is_valid()` and (for `NETWORK` topology) is addressed to `schema.board_id` or the broadcast address. `None` otherwise — nothing available, corrupt, or misaddressed are indistinguishable at this layer. |
| `close()` | Releases the transport resource. |
| `with channel: ...` | `__enter__`/`__exit__` call `close()` on block exit. |

Two concrete transports:

- **`SerialChannel(schema, port, baudrate=115200, **serial_kwargs)`** — wraps `pyserial`. Mirrors
  `arduino_serial_channel`: raw fixed-size bytes over the wire, no framing, no sync bytes. Checks
  `port.in_waiting` before reading so it never blocks.
- **`TcpChannel(schema, host, port, connect_timeout=10.0, **socket_kwargs)`** — a TCP **client**
  (the C++ `arduino_wifi_channel` it targets is a `WiFiServer`, so the firmware is the server and
  a PC/Pi connects to it). Since TCP is a byte stream with no built-in message boundaries, it
  maintains an internal accumulation buffer and polls with a zero-timeout `select.select()` — never
  blocks, never returns a partial packet.

### Async channels: `AsyncChannel`, `AsyncTcpChannel`

`AsyncChannel` (`ecomm.channels.async_base`) is the asyncio counterpart. Where the sync `Channel`
expects a caller to poll `try_receive()` in a loop, `AsyncChannel` lets a caller `await` many
channels concurrently (`asyncio.gather`, a `TaskGroup`) with no thread or busy-poll per connection —
each coroutine only consumes CPU when it actually has work to do. Concrete transports implement
three primitives instead of two:

- `_do_send(data: bytes) -> None`
- `_do_receive(size: int) -> bytes` — **suspend** until exactly `size` bytes are available, then
  return them. Never a partial read.
- `_do_try_receive(size: int) -> bytes | None` — return exactly `size` bytes if *already* buffered,
  else `None` immediately, **without suspending**.

Public surface: `send()` (same contract as the sync version), `receive()` (the recommended way to
consume a channel — suspends efficiently, no polling), `try_receive()` (non-blocking check within a
single event-loop tick), `close()`, and `async with channel: ...`.

`AsyncTcpChannel.connect(schema, host, port, **open_connection_kwargs)` is the async factory (use
this, not `__init__`, unless you already have a connected `(reader, writer)` pair). Internally it
runs one small background task per channel that continuously drains the `asyncio.StreamReader` into
a plain `bytearray` the class owns directly. This exists because the more obvious approach —
`asyncio.wait_for(reader.readexactly(size), timeout=0)` for a non-blocking peek — **does not work**:
it can raise `asyncio.TimeoutError` even when the requested bytes are already fully buffered, which
would silently drop already-arrived data from `try_receive()`'s point of view. The background-pump
design was chosen after that approach was tried and empirically disproved.

```python
import asyncio
from ecomm.protocol import PacketSchema, Topology, ChecksumPolicy, HeaderType, HeaderOptions, Packet
from ecomm.channels import AsyncTcpChannel

async def main():
    schema = PacketSchema(32, Topology.NETWORK, checksum=ChecksumPolicy.NONE, board_id=2)
    async with await AsyncTcpChannel.connect(schema, "192.168.1.42", 8080) as ch:
        packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
        packet.header.receiver_id = 1
        await ch.send(packet)
        reply = await ch.receive()   # suspends until a full packet arrives, no polling

asyncio.run(main())
```

For several boards at once, `await`ing multiple `AsyncTcpChannel.receive()` calls via
`asyncio.gather` drives them all off one thread — see
`tests/channels/test_async_tcp_channel.py::test_two_channels_can_be_awaited_concurrently` for a
worked example.

### `ReliableChannel`: acknowledgement and retry

`ReliableChannel(channel, clock=None, max_retries=3, buffer_depth=1, poll_interval_seconds=0.001)`
wraps a **synchronous** `Channel` and adds stop-and-wait reliability — a byte-for-byte port of
`reliable_channel.tpp`'s wire protocol (same `seq_num` semantics, same ack framing, same staging
ring), so a Python peer interoperates with a C++ `reliable_channel<>` on real firmware.

Requires `channel.schema.sequence is SequencePolicy.SEQUENCED` (an `icontract` precondition on
construction, re-checked as a class invariant for the object's whole lifetime).

- **`send(packet)`** stamps `packet.header.seq_num` with the outbound counter, transmits, then polls
  for a matching ack. If none arrives within `clock.timeout_seconds`, it retransmits — up to
  `max_retries` attempts total — and returns `SendResult.TIMEOUT` on exhaustion, `SendResult.OK`
  once acked. **This call blocks**; worst case it holds for `max_retries * clock.timeout_seconds`
  seconds.
- **`try_receive()`** checks an internal staging ring first, then polls the wrapped channel. A data
  packet whose `seq_num` matches the expected counter is auto-acked and returned; a stale `seq_num`
  (a duplicate retransmit) is re-acked and discarded; ack packets are consumed internally and never
  surfaced to the caller.
- **`close()`** and **`with ...:`** delegate to the wrapped channel.

`ClockPolicy` is an abstract timing source (`now()`, `timeout_seconds`); `MonotonicClockPolicy`
(the default, 0.5s timeout) backs it with `time.monotonic()`. Supply your own `ClockPolicy`
subclass for tests or a simulated clock.

**The one deliberate behavioral difference from the C++ side:** `send()`'s inner wait loop calls
`time.sleep(poll_interval_seconds)` between poll attempts. The C++ implementation busy-polls with no
yield, which is correct on bare-metal firmware with no OS to hand control back to, but would peg a
CPU core at 100% on a general-purpose OS — the sleep changes nothing about the wire protocol or the
timeout semantics, only how the waiting is spent.

---

## Contracts, Typing, and Errors

### Design-by-contract with icontract

Every public constructor and most public methods are guarded with
[`icontract`](https://github.com/Parquery/icontract) `@require` (preconditions), `@ensure`
(postconditions), and `@invariant` (class-level, checked before/after every public method call).
Violations raise `icontract.ViolationError` — a subclass of `AssertionError`, not `ValueError` —
with a message that names every relevant local variable's value, not just the condition that failed.

Two placement rules worth internalizing if you read the source:

- **`@require`/`@invariant` are not interchangeable**, even when they'd catch the same bad
  constructor argument. `@require` on `__init__` validates the argument *once, at construction*.
  `@invariant` on the *class* enforces the same property for the object's entire lifetime — it also
  catches a later direct mutation (e.g. `reliable_channel.max_retries = 0`) on the next public method
  call. `ReliableChannel` and `MonotonicClockPolicy` use both, for exactly this reason.
- **`@require`/`@ensure` must go on methods, never on the class itself.** They are function
  decorators; stacking one directly on a class wraps the *class* in a plain function, silently
  breaking `isinstance`/`issubclass`/subclassing (a real bug this package's own test suite caught
  during development — `ReliableChannel` briefly wasn't a class at all). Only `@invariant` is
  class-aware.

### Runtime typing with beartype

Every module imports a package-wide preconfigured `beartype` from `ecomm._typing` rather than the
stock decorator, so the whole library shares one policy: PEP 484's implicit numeric tower is
enabled, meaning an `int` is accepted anywhere a `float` is annotated (`ReliableChannel(...,
poll_interval_seconds=0)` works, not just `=0.0`) while genuinely wrong types (`str`, `None`) are
still rejected. Without this, beartype's strict default would reject an integer literal passed for a
duration — a real papercut this package hit and fixed once, centrally, rather than leaving every
call site to discover it independently.

### The exception hierarchy

The C++ side never throws — `static_assert`, `assert`, and error codes carried in packets are the
whole error model, because exceptions cost too much on a microcontroller. This package runs on a PC
or Raspberry Pi, where exceptions are cheap and idiomatic, so it draws a different, deliberate line:

- **Contracts** (`icontract.ViolationError`) guard *programmer errors* — calling an API with
  arguments that violate its documented precondition (a too-small packet, an out-of-range id,
  mismatched schemas).
- **`ecomm.errors.EcommError`** (and its one current subclass, `MalformedPacketError`) signal
  *runtime wire conditions* — bytes that were received and decoded but do not form a structurally
  valid packet. `PacketHeader.type` raises `MalformedPacketError` for the two reserved `HeaderType`
  encodings, naming the raw value and pointing at `raw_type` as the non-raising alternative.
- **A malformed error envelope stays a `None` return**, not an exception — mirroring `as_error`'s
  `std::optional` on the C++ side, because "there is no valid envelope here" is a routine,
  expected outcome for a decoder, not an error condition.

Both `EcommError` and `MalformedPacketError` are exported from the top-level `ecomm` package.

---

## Serialization is your responsibility

**A packet's payload is opaque bytes.** ecomm guarantees the header and hands you a raw
`payload_size`-byte region; how you lay that region out is entirely your application's choice — the
protocol layer imposes no schema on it whatsoever. This is a deliberate design inherited from the
C++ side (`packet.hpp`: "the application may overlay any structure on this region... the packet
itself imposes no schema").

In practice this means you write your own small encode/decode functions for whatever structured data
you're sending, using `struct.pack`/`struct.unpack` (matching whatever endianness and field widths
your firmware expects) or plain slicing for fixed-width fields. `ecomm.protocol.error`'s
length-prefixed record is one worked example of exactly this pattern, applied to one specific,
built-in use (the error envelope) — see [the async_tcp example](#examples) for a second, applied to
an entirely custom `{name, age}` record.

---

## Examples

**[`examples/async_tcp/`](../examples/async_tcp/)** (at the repository root, since it spans both
languages) — a complete Raspberry Pi ⇄ ESP32 demo: the Pi serializes a small `{name, age}` record
into a payload using `AsyncTcpChannel`; the ESP32 (running the C++ `esp_async_wifi_channel`) parses
it and replies with a greeting. Includes the Arduino-framework firmware, the Python client, and a
README with the schema-matching table that keeps the two sides wire-compatible.

---

## Edge Cases & Behavior

| Situation | Behavior |
|---|---|
| Decoded header's type field holds a reserved encoding (`0x6`/`0x7`) | `.type` raises `MalformedPacketError` naming the raw value; `.raw_type` returns the int without raising. |
| `packet.schema != channel.schema` passed to `send()` | `icontract.ViolationError` — precondition violated. Two schemas are equal by *value*, not identity, so distinct `PacketSchema(...)` calls with the same arguments are interchangeable. |
| `Packet.from_bytes(schema, data)` with `len(data) != schema.packet_size` | `icontract.ViolationError` — precondition on the byte count. |
| Received packet fails checksum validation | `try_receive()`/`receive()` return `None`. Indistinguishable from "nothing available" or "misaddressed" at this layer — the channel doesn't tell you *why* nothing came back. |
| `schema.checksum is ChecksumPolicy.NONE` | `seal()` is a no-op; `is_valid()` always returns `True` (there is no FCS to check). |
| `seal(packet)` called twice in a row, unchanged packet | Idempotent — same FCS both times (sealing always re-zeroes `fcs` before hashing). |
| `write_error(...)` with a message too long for the payload, or an out-of-range `max_message_length` | `icontract.ViolationError`. |
| `read_error(packet)` on a packet without `HeaderOptions.ERROR` set | `icontract.ViolationError` by default; pass `require_error_flag=False` to skip the check. |
| A structurally-malformed error envelope (declared length overruns the payload) | `read_error()` returns `None` — a wire condition, not an exception. |
| `AsyncTcpChannel.try_receive()` with a peer that has closed the connection | Raises `ConnectionError` once the buffered bytes are exhausted. |
| Sync `TcpChannel`/`SerialChannel` transport disconnects mid-read | Raises `ConnectionError` (TCP) from the next `try_receive()` once the buffer can't be topped up. |
| `ReliableChannel` constructed with a `NO_SEQUENCE` schema | `icontract.ViolationError` at construction. |
| `ReliableChannel.send()` never receives an ack | Blocks for up to `max_retries * clock.timeout_seconds` seconds, then returns `SendResult.TIMEOUT`. |
| A duplicate (stale `seq_num`) packet arrives at `ReliableChannel.try_receive()` | Re-acked, discarded, not surfaced to the caller. |
| The `ReliableChannel` staging ring is full when a non-ack packet arrives during an ack poll | Silently dropped (matches `stage_push`'s overflow policy in the C++ source). |

---

## Assumptions & Limitations

1. **Verified against compiled C++ output, not re-derived from the spec.** Every checksum algorithm
   and wire layout in this package was checked by compiling the real `ecomm` C++ headers with `g++`
   and diffing actual bytes/values against this package's output — not by reading documentation and
   hoping. This is a strong guarantee for the specific checksum policies, header layouts, and error
   envelope shapes exercised during that verification (see `tests/protocol/test_compute.py` and
   `tests/protocol/test_header.py` for the pinned reference vectors); it is **not** a guarantee that
   every possible `PacketSchema` combination, or future changes to either side, stay in sync
   automatically. If you change one side, re-verify the other.
2. **Wire-compatibility is schema equality, enforced but not automatic.** Nothing prevents you from
   constructing a `PacketSchema` that simply doesn't match your firmware's `packet<>` template
   arguments — the enforcement (`icontract` preconditions comparing `packet.schema` to
   `channel.schema`) only catches *internal* Python-side mismatches, not "does this actually match
   the C++ side," which is on you to get right (see the [async_tcp example](#examples)'s
   schema-matching table for the discipline this requires).
3. **The payload has no schema.** ecomm (both languages) guarantees the header and a byte count,
   nothing about the payload's internal structure. All of the guarantees above stop at the payload
   boundary — see [Serialization is your responsibility](#serialization-is-your-responsibility).
4. **`ReliableChannel` wraps a synchronous `Channel` only.** There is no async reliable-delivery
   wrapper; `AsyncChannel`'s `receive()`/`send()` make no delivery guarantee beyond "the transport
   accepted the bytes" (`TcpChannel`/`AsyncTcpChannel` build on TCP, which guarantees delivery and
   ordering at the transport layer already — reliability at the ecomm protocol layer is primarily
   useful over an inherently lossy transport, e.g. serial or a raw UDP-like link).
5. **Word-alignment of `packet_size` is not enforced here** (see
   [`PacketSchema`](#packetschema--the-runtime-stand-in-for-c-templates)) — it's a firmware/DMA
   concern the C++ side already checked at compile time.
6. **Single-process, single-owner assumptions carried over from the C++ side.** ecomm is designed
   for a small, fixed-size-packet, single-owner protocol between a coordinator and worker nodes —
   not a multi-tenant, self-describing, or streaming-validation wire format. This package inherits
   that scope; it is not trying to be a general-purpose serialization framework.
7. **No dynamic allocation is *not* a constraint here.** Unlike the embedded C++ library, this
   package runs on a general-purpose OS and allocates freely (`bytearray`, Python objects, etc.) —
   that C++ constraint does not apply and was never a design goal on this side.

---

## When to Use This (and When Not To)

**Good fit**

- A PC, Raspberry Pi, or CI runner that needs to talk to real `ecomm` firmware over serial or
  TCP/Wi-Fi, using the exact wire protocol the firmware speaks.
- Host-side tooling, test harnesses, or bring-up scripts for an ecomm-based embedded project, where
  you want the same packet format on both ends without hand-rolling a parser.
- Applications that want contracts and runtime type-checking on the protocol boundary, not just
  static type hints.

**Reach for something else when you need**

- A general-purpose serialization framework with no relationship to ecomm's specific packet
  format — Protocol Buffers, FlatBuffers, msgpack, etc.
- To talk to firmware that has *diverged* from this package's verified wire format (see
  [Assumptions & Limitations](#assumptions--limitations), point 1) without re-verifying first.
- Reliable delivery over an async transport — `ReliableChannel` only wraps synchronous channels
  today (see point 4 above).

---

## Testing

```bash
uv run pytest
```

168 tests across `tests/protocol/` (wire format: header packing for every topology × sequence ×
checksum combination, checksum algorithms pinned against C++-derived reference vectors, packet and
validator round-trips, error envelope encode/decode, and edge cases) and `tests/channels/` (sync and
async channel send/receive, address filtering, `ReliableChannel` ack/retry/duplicate-handling, and
edge cases) — including tests against real OS resources where it matters (real TCP sockets for
`TcpChannel`/`AsyncTcpChannel`, a real asyncio event loop) rather than doubles for everything.

---

## Project Layout

```
ecomm-python/
  pyproject.toml           # uv-managed project; distribution name "ecomm"
  LICENSE                  # copy of the repository's MIT license (required for the wheel to include it)
  README.md                # this file
  src/ecomm/
    __init__.py             # top-level re-exports (PacketSchema, Packet, EcommError, ...)
    errors.py                # EcommError, MalformedPacketError
    _typing.py                # package-wide preconfigured beartype (private)
    protocol/
      __init__.py             # aggregator re-exports
      config.py                 # PROTOCOL_VERSION, board-id range, broadcast address
      topology.py               # Topology enum
      sequence.py               # SequencePolicy enum
      checksum.py                # ChecksumPolicy enum + wire metadata
      compute.py                  # the checksum algorithms themselves
      _crc_tables.py               # CRC8/16/32/64 lookup tables, transcribed from compute.tpp
      header_type.py             # HeaderType enum
      header_options.py          # HeaderOptions flag enum + bitmask
      header.py                  # PacketHeader
      schema.py                  # PacketSchema
      packet.py                  # Packet
      validator.py                # seal(), is_valid()
      error.py                    # ErrorCode, ErrorView, write_error(), read_error()
    channels/
      __init__.py               # aggregator re-exports
      result.py                  # SendResult enum
      _decode.py                  # shared decode/validate/address-filter helper (sync + async)
      base.py                    # Channel (sync abstract base)
      serial_channel.py          # SerialChannel
      tcp_channel.py              # TcpChannel
      async_base.py              # AsyncChannel (async abstract base)
      async_tcp_channel.py        # AsyncTcpChannel
      reliable.py                # ReliableChannel, ClockPolicy, MonotonicClockPolicy
  tests/
    protocol/                 # mirrors src/ecomm/protocol/
    channels/                 # mirrors src/ecomm/channels/, plus conftest.py test doubles
```

---

## Using This From Another Project

The distribution is named **`ecomm`** (same as the import). It is not published to PyPI yet, so once
it is, installation will be simply:

```bash
uv add ecomm
```

Until then, `uv` can add it straight from this repository or a local checkout. Note that while the
*package* is named `ecomm`, it lives in the `ecomm-python/` subdirectory of this monorepo (the
repository root's `ecomm/` folder holds the C++ headers), so Git installs point at that subdirectory:

**From a local checkout** (e.g. developing this and a consumer project side by side):

```bash
uv add --editable /path/to/ecomm/ecomm-python
```

**Straight from Git**, no local checkout needed:

```bash
uv add "ecomm @ git+https://github.com/MarikTik/ecomm.git#subdirectory=ecomm-python"
```

Pin to a tag or commit for reproducibility by appending `@<ref>` to the Git URL (before the
`#subdirectory=...` fragment), e.g.
`git+https://github.com/MarikTik/ecomm.git@v0.1.0#subdirectory=ecomm-python`.

Either way the import is the same:

```python
import ecomm
```

---

## License

**MIT License** — permissive, no restriction on commercial use. Same terms as the parent `ecomm`
C++ project; see [`LICENSE`](LICENSE) (a copy of the repository root's license, included here so it
ships inside the built wheel/sdist).

---

## Contributing

- Read [`../project/guidelines.md`](../project/guidelines.md) for the contribution and style guide
  shared with the C++ side (branching, commit format, architectural philosophy).
- Python-specific conventions — contracts via `icontract`, typing via `beartype`, the exception
  hierarchy's contract-vs-error split — are documented in each module's docstring and summarized in
  [Contracts, Typing, and Errors](#contracts-typing-and-errors) above; follow the existing pattern
  rather than introducing a new one.
- Any change to a checksum algorithm, header layout, or channel framing rule must be re-verified
  against the compiled C++ output (see [Assumptions & Limitations](#assumptions--limitations), point
  1) before it's trustworthy — a matching Python implementation that merely "looks right" is not
  sufficient given how this package is meant to be used.
- Run `uv run pytest` before submitting; all 168 tests are expected to pass.

---

## Contact

Open an issue on the [repository](https://github.com/MarikTik/ecomm), or reach out via
**mtik.philosopher@gmail.com** for questions and collaboration proposals.
