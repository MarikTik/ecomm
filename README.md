# ecomm — Flat Binary Communication Protocol for Embedded & Systems C++

**ecomm** is a header-only C++17 library for message-driven communication on Arduino-class
microcontrollers — primarily the ESP family (ESP32, ESP8266) — talking over serial or Wi-Fi to a
coordinator and to each other. It targets the specific shape of a robotics-style workload: short,
frequent, structured packets between a coordinator and a set of worker nodes, where internal
memory and compile-time configurability take precedence over runtime convenience.

The wire format is **flat, fixed-size, and template-configured**: every field's presence and width
is decided by the C++ compiler at the point a `packet<>` is instantiated, not negotiated at runtime.
Two peers are wire-compatible exactly when they instantiate the same template arguments — there are
no type tags, no length prefixes, no version negotiation on the wire. In exchange you get no
dynamic allocation, no exceptions, no RTTI anywhere in the protocol or channel layers, and a packet
size known at compile time.

```cpp
#include <ecomm/protocol/protocol.hpp>
#include <ecomm/channels/channels.hpp>

using namespace ecomm::protocol;
using namespace ecomm::channels;

// A 32-byte, point-to-point packet with a 16-bit CRC.
using my_packet = packet<32, topology::point_to_point, no_sequence, crc16>;

arduino_serial_channel<my_packet> link{Serial};   // named `link`, not `channel` -- that name is
                                                    // already ecomm::channels::channel<Impl, Packet>

my_packet out{header_type::data, header_options::none};
std::memcpy(out.payload, "hello", 5);
(void)link.send(out);   // seals (computes + writes the CRC), then writes the raw bytes -- [[nodiscard]]

if (auto in = link.try_receive()) {
    // *in is a structurally valid, checksum-verified my_packet
}
```

A byte-exact Python implementation of this same wire protocol lives alongside this library — see
[`ecomm-python/`](ecomm-python/README.md) — so a laptop, CI runner, or Raspberry Pi can speak to
this firmware directly.

---

## Table of Contents

- [Overview](#overview)
- [Requirements](#requirements)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Core Concepts](#core-concepts)
  - [Three independent policies](#three-independent-policies)
  - [Compile-time configuration, not runtime negotiation](#compile-time-configuration-not-runtime-negotiation)
- [The Protocol Layer (`ecomm::protocol`)](#the-protocol-layer-ecommprotocol)
  - [Wire format reference](#wire-format-reference)
  - [`packet_header<Topology, SequencePolicy, ChecksumPolicy>`](#packet_headertopology-sequencepolicy-checksumpolicy)
  - [`packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>`](#packetpacketsize-topology-sequencepolicy-checksumpolicy)
  - [Checksum policies](#checksum-policies)
  - [`validator<Packet>`](#validatorpacket)
  - [The error envelope](#the-error-envelope)
  - [Configuration macros (`config.hpp`)](#configuration-macros-confighpp)
- [The Channels Layer (`ecomm::channels`)](#the-channels-layer-ecommchannels)
  - [`channel<Impl, Packet>` — the CRTP base](#channelimpl-packet--the-crtp-base)
  - [`arduino_serial_channel`](#arduino_serial_channel)
  - [`arduino_wifi_channel`](#arduino_wifi_channel)
  - [`esp_async_wifi_channel`](#esp_async_wifi_channel)
  - [`reliable_channel` — acknowledgement and retry](#reliable_channel--acknowledgement-and-retry)
- [Design Philosophy](#design-philosophy)
  - [The three-tier error ladder](#the-three-tier-error-ladder)
  - [No dynamic allocation](#no-dynamic-allocation)
  - [Strategy via templates, not virtuals](#strategy-via-templates-not-virtuals)
- [The `hub` module (unmaintained)](#the-hub-module-unmaintained)
- [The Python Client](#the-python-client)
- [Examples](#examples)
- [Edge Cases & Behavior](#edge-cases--behavior)
- [Assumptions & Limitations](#assumptions--limitations)
- [When to Use ecomm (and When Not To)](#when-to-use-ecomm-and-when-not-to)
- [Building & Testing](#building--testing)
- [Project Layout](#project-layout)
- [License](#license)
- [Contributing](#contributing)
- [Contact](#contact)

---

## Overview

- **Header-only.** No separate compilation; `#include` what you need or pull the whole library via
  CMake `FetchContent`.
- **Compile-time sized and configured.** Every `packet<>` instantiation has a fixed `sizeof`, known
  to the compiler; there is no runtime schema negotiation.
- **No dynamic allocation, no exceptions, no RTTI** anywhere in the protocol or channel layers —
  suitable for `-fno-exceptions -fno-rtti` embedded builds. See
  [Design Philosophy](#design-philosophy).
- **Policy-based, not inheritance-based, configuration.** Topology, sequencing, and checksum
  algorithm are independent compile-time policies; see
  [Three independent policies](#three-independent-policies).
- **Multiple transports behind one API.** `arduino_serial_channel`, `arduino_wifi_channel`, and
  `esp_async_wifi_channel` all expose the same `send()`/`try_receive()` surface; swapping the
  transport does not touch application code. See [The Channels Layer](#the-channels-layer-ecommchannels).
- **A verified Python twin.** [`ecomm-python/`](ecomm-python/README.md) speaks the exact same wire
  protocol, checked byte-for-byte against this library's compiled output — not merely a
  compatible reimplementation.

Two library namespaces:

| Namespace | Contents |
|---|---|
| `ecomm::protocol` | The wire format: `packet_header`, `packet`, checksum policies and `compute<>`, `validator<Packet>`, the error envelope, and the plain enums (`header_type`, `header_options`, `topology`). |
| `ecomm::channels` | The transports: `channel<Impl, Packet>` (CRTP base), `arduino_serial_channel`, `arduino_wifi_channel`, `esp_async_wifi_channel`, `reliable_channel`, and `send_result`. |

`ecomm::protocol::details` and `ecomm::channels`' private members are implementation detail — not
part of the public API, and not stable across versions.

---

## Requirements

- **C++17**, with `<optional>`, `<string_view>`, `<cstddef>` (`std::byte`), and `if constexpr`.
- An **Arduino-compatible toolchain** for the channel layer (`HardwareSerial`, `WiFiServer`/
  `WiFiClient`, or AsyncTCP/ESPAsyncTCP for `esp_async_wifi_channel`). The protocol layer alone
  (packets, checksums, the error envelope) has no Arduino dependency and builds on a plain host —
  that's what the test suite does.
- Two dependencies, fetched automatically via CMake `FetchContent`:
  [`etools`](https://github.com/MarikTik/etools) (compile-time metaprogramming helpers — `typelist`,
  `smallest_uint_t`, flag-enum operators, `dispatch_factory`, ...) and, transitively,
  [`eser`](https://github.com/MarikTik/eser) (flat binary serialization, used internally by `etools`).
- **CMake 3.20+** for this repository's own build (the library itself, being header-only, imposes no
  CMake minimum on consumers beyond what `FetchContent` needs).

---

## Installation

```cmake
include(FetchContent)

FetchContent_Declare(
  ecomm
  GIT_REPOSITORY https://github.com/MarikTik/ecomm.git
  GIT_TAG        main   # or a specific release tag
)
FetchContent_MakeAvailable(ecomm)

target_link_libraries(your_target_name PRIVATE ecomm)   # ecomm is an INTERFACE target
```

`etools` (and, transitively, `eser`) are fetched and linked automatically — you do not declare them
yourself.

Include the aggregator headers for everything in a layer, or pull individual headers:

```cpp
#include <ecomm/protocol/protocol.hpp>   // packet_header, packet, checksum, validator, error envelope
#include <ecomm/channels/channels.hpp>   // channel<>, every concrete transport, reliable_channel
// or, selectively:
#include <ecomm/protocol/packet.hpp>
#include <ecomm/channels/arduino_serial_channel.hpp>
```

---

## Quick Start

```cpp
#include <Arduino.h>
#include <ecomm/protocol/protocol.hpp>
#include <ecomm/channels/channels.hpp>

using namespace ecomm::protocol;
using namespace ecomm::channels;

// A 32-byte, network-topology packet addressed by sender_id/receiver_id,
// with a 16-bit CRC. Both peers must instantiate this exact same alias.
using my_packet = packet<32, topology::network, no_sequence, crc16>;

arduino_serial_channel<my_packet> link{Serial};   // named `link`, not `channel` -- that name is
                                                    // already ecomm::channels::channel<Impl, Packet>

void setup() {
    Serial.begin(115200);
}

void loop() {
    my_packet out{header_type::data, header_options::none};
    out.header.receiver_id = 2;
    std::memcpy(out.payload, "ping", 4);
    (void)link.send(out);   // seals (computes + writes the CRC) then writes the raw bytes -- [[nodiscard]]

    if (auto in = link.try_receive()) {
        // *in passed validator<my_packet>::is_valid and is addressed to this board
        Serial.print("from board ");
        Serial.println(in->header.sender_id);
    }

    delay(1000);
}
```

---

## Core Concepts

### Three independent policies

Every packet and header in ecomm is parameterized by three independent compile-time policies. This
is the central design decision the whole library is built around:

| Policy | Type | Values | Wire effect |
|---|---|---|---|
| **Topology** | `topology` (enum) | `point_to_point`, `network` | `network` adds `sender_id` + `receiver_id` (1 byte each) to the header. |
| **Sequence** | tag type | `no_sequence`, `sequenced` | `sequenced` adds a 1-byte `seq_num` immediately after the protocol byte. Required by `reliable_channel`. |
| **Checksum** | tag type | `none`, `sum8`/`16`/`32`, `crc8`/`16`/`32`/`64`, `fletcher16`/`32`, `adler32`, `internet16` | Anything but `none` adds a trailing FCS field, `ChecksumPolicy::size` bytes wide. |

They are independent — any combination is a valid, distinct `packet<>` instantiation — and a single
device may use *different* combinations on different links simultaneously: a UART leaf link that is
strictly point-to-point, and a Wi-Fi link that is part of a multi-node mesh, in the same firmware.
Topology (and the other two policies) is a per-instantiation template parameter, not a build-wide
flag.

### Compile-time configuration, not runtime negotiation

`packet_header<Topology, SequencePolicy, ChecksumPolicy>` has **eight explicit partial
specializations** — one per combination of the three policies — each a distinct, standard-layout
type containing only the fields its combination needs. There is no runtime "does this packet have a
checksum" branch anywhere in the hot path; the compiler already knows, because the type says so.
`packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>` layers a fixed-size payload
(`PacketSize - sizeof(header)` bytes) on top of one of those eight header specializations.

**The practical consequence:** two peers are wire-compatible exactly when they instantiate `packet<>`
with identical template arguments. There is no version byte, no capability negotiation — mismatched
peers simply produce or expect the wrong bytes, silently. See
[Assumptions & Limitations](#assumptions--limitations).

---

## The Protocol Layer (`ecomm::protocol`)

### Wire format reference

A packet is a header immediately followed by a raw payload region:

```
+----------------------------------------------+-------------------------------+
|              packet_header                    |    payload (PacketSize        |
|  [proto(1B)] [seq(*s)] [ids(*n)] [fcs($)]      |       - sizeof(header) bytes) |
+----------------------------------------------+-------------------------------+
(*s) seq_num present only when SequencePolicy == sequenced (1 byte)
(*n) sender_id + receiver_id present only when Topology == network (2 bytes)
($)  fcs field present only when ChecksumPolicy != none (ChecksumPolicy::size bytes)
```

The eight header layouts this produces, in wire order (`_byte` — the packed protocol byte — is
always first; `fcs`, when present, is always last so it covers everything before it):

| Topology | Sequence | Checksum | Wire layout |
|---|---|---|---|
| `point_to_point` | `no_sequence` | `none` | `_byte` |
| `point_to_point` | `no_sequence` | *policy* | `_byte`, `fcs` |
| `point_to_point` | `sequenced` | `none` | `_byte`, `seq_num` |
| `point_to_point` | `sequenced` | *policy* | `_byte`, `seq_num`, `fcs` |
| `network` | `no_sequence` | `none` | `_byte`, `sender_id`, `receiver_id` |
| `network` | `no_sequence` | *policy* | `_byte`, `sender_id`, `receiver_id`, `fcs` |
| `network` | `sequenced` | `none` | `_byte`, `seq_num`, `sender_id`, `receiver_id` |
| `network` | `sequenced` | *policy* | `_byte`, `seq_num`, `sender_id`, `receiver_id`, `fcs` |

The protocol byte itself (`_byte`, always present, always first) packs three fields via explicit
shifts and masks — never bitfields, whose layout is implementation-defined and unsafe for a wire
protocol:

```
 7..5 : type      (3 bits)  -- header_type enum, 6 values assigned (2 reserved)
    4 : error     (1 bit)   -- header_options::error
    3 : ack       (1 bit)   -- header_options::ack
    2 : encrypted (1 bit)   -- header_options::encrypted
 1..0 : version   (2 bits)  -- ECOMM_PROTOCOL_VERSION, not a constructor parameter
```

`header_type` (6 assigned values; `0x6`/`0x7` are reserved and must not appear on the wire until
assigned): `data` (`0x0`), `control` (`0x1`), `auth` (`0x2`), `session` (`0x3`), `log` (`0x4`),
`firmware` (`0x5`).

`header_options` (opted into `|`/`&`/`^`/`~` via `etools::meta::enable_flags`): `none`, `error`,
`ack`, `encrypted`.

### `packet_header<Topology, SequencePolicy, ChecksumPolicy>`

A compact, standard-layout, data-only type — logic lives in `validator`, not here. Declares **no
data members of its own**; every field is inherited from a single `details::header_layout` base
(one of the eight specializations above), which is what makes `offsetof` well-defined and the
in-memory layout match the wire layout exactly. `_byte` itself is hidden behind private inheritance;
callers reach it only through typed accessors.

| Member | Kind | Meaning |
|---|---|---|
| `packet_header()` | constructor | Zero-initializes every field. |
| `packet_header(header_type, header_options)` | constructor | Packs `type` into bits 7..5, `opts` (masked) into bits 4..2, `ECOMM_PROTOCOL_VERSION` into bits 1..0. Other fields zero-initialized. |
| `type()` | `[[nodiscard]]` | Decoded `header_type` from bits 7..5. |
| `options()` | `[[nodiscard]]` | Decoded `header_options` from bits 4..2. |
| `has(header_options)` | `[[nodiscard]]` | `true` iff every bit in the argument is set. |
| `version()` | `[[nodiscard]]` | The 2-bit version field. |
| `raw()` | `[[nodiscard]]` | The full packed protocol byte. |
| `seq_num`, `sender_id`, `receiver_id`, `fcs` | member (where applicable) | Present only in the specializations whose policies call for them — see the wire layout table above. |

### `packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>`

A fixed-size, POD-aggregate wire packet: the templated header above, followed by a raw
`std::byte payload[payload_size]` — nothing else. Application-layer concepts (handler ids, task
ids, status codes) are not part of the packet; they live in the first bytes of the payload,
interpreted entirely by the layer above.

```cpp
using my_packet = packet<32, topology::network, sequenced, crc16>;

static_assert(my_packet::packet_size == 32);
static_assert(my_packet::payload_size == 32 - sizeof(my_packet::header_t));
```

Two `static_assert`s guard every instantiation: `PacketSize` must be word-aligned
(`PacketSize % sizeof(std::size_t) == 0` — DMA and serial drivers on embedded targets typically
require it), and `PacketSize` must be strictly greater than the header's `sizeof` so at least one
payload byte exists.

### Checksum policies

`checksum.hpp` defines twelve layout-only policy tags (`value_type` + `size`); `compute.hpp`/
`compute.tpp` define the matching `compute<Policy>` specializations that actually compute a value:

| Policy | FCS width | Notes |
|---|---|---|
| `none` | 0 bytes | No checksum; the header carries no FCS field at all. |
| `sum8` / `sum16` / `sum32` | 1 / 2 / 4 | Additive sum, wrapping at the accumulator width. |
| `crc8` / `crc16` / `crc32` / `crc64` | 1 / 2 / 4 / 8 | Table-driven, MSB-first, `initial=0`, `final_xor=0`. On ESP32 with `esp_crc.h` available, `crc8`/`crc32` use the hardware `esp_crc32_le` intrinsic instead of the software table (guarded by `__has_include`, falling back cleanly on host builds and other targets). |
| `fletcher16` / `fletcher32` | 2 / 4 | Fletcher checksum. |
| `adler32` | 4 | Modified Fletcher checksum (the same algorithm used by zlib, implemented independently here). |
| `internet16` | 2 | RFC 1071 one's-complement sum, as used by IP/TCP/UDP headers. |

### `validator<Packet>`

A stateless policy struct with two operations, specialized on whether `ChecksumPolicy` is `none`:

- **`seal(packet)`** — finalizes a packet before transmission: zero `packet.header.fcs`, compute the
  checksum over all `PacketSize` bytes, write the result back into `fcs`. A no-op when
  `ChecksumPolicy == none`.
- **`is_valid(packet)`** — zero a local copy's `fcs`, recompute, compare against the received value.
  Always `true` when `ChecksumPolicy == none`. Never mutates the caller's packet.

`channel<Impl, Packet>` calls both automatically — `seal` inside `send()`, `is_valid` inside
`try_receive()` — so application code never calls `validator` directly in the common case.

### The error envelope

When a packet's header has `header_options::error` set, its payload is reinterpreted as a
length-prefixed error record (`error.hpp`):

```
+----------------+----------------------------+---------------------+----------+
|   error_code   |   error_message_length_t   |    message bytes    |   pad    |
|   (uint16_t)   |  (uint8/16/32, compile-time)|    length bytes     |   rest   |
+----------------+----------------------------+---------------------+----------+
```

`error_message_length_t`'s width is selected at compile time — via
`etools::meta::smallest_uint_t<ECOMM_MAX_ERROR_MESSAGE_LENGTH>` — from the
`ECOMM_MAX_ERROR_MESSAGE_LENGTH` macro (default 65535): `<= 255` → `uint8_t`, `<= 65535` →
`uint16_t`, `<= 4294967295` → `uint32_t`.

```cpp
using pkt = packet<32, topology::point_to_point, no_sequence, none>;
pkt p{header_type::data, header_options::error};

error_envelope<pkt::payload_size>::write(p.payload, error_code::checksum_mismatch, "bad crc");

if (auto view = as_error(p)) {
    // view->code, view->message (not null-terminated), view->length
}
```

`error_code`'s top byte is a subsystem tag, so host-side dispatch can fan out without a giant
switch: `0x00xx` framing, `0x01xx` transport, `0x02xx` dispatch/hub, and `0x4000`+
(`error_code::user_range_begin`) reserved for application-defined codes. `as_error(packet)` asserts
(debug builds) that `header_options::error` is set; `as_error_unchecked(packet)` skips that
precondition. Both return `std::nullopt` — not an assertion failure — for a structurally malformed
envelope (declared length overruns the payload), because that is a wire condition, not a programmer
error.

**Endianness note:** the error envelope's `memcpy`-based encoding currently assumes a little-endian
host, enforced by a `static_assert` that fails loudly on a big-endian build rather than silently
producing wrong bytes. A protocol-wide endianness sweep is the tracked follow-up; until then,
building on a big-endian target is not supported.

### Configuration macros (`config.hpp`)

All overridable via a compiler flag (`-D...`) before including any ecomm header, each guarded by a
`static_assert` (or, for values needed at preprocess time, `#error`) naming the exact constraint
violated:

| Macro | Default | Valid range | Meaning |
|---|---|---|---|
| `ECOMM_PROTOCOL_VERSION` | `0` | `[0, 3]` | The 2-bit version stamped into every header byte. Not user-overridable in practice (it's a library constant, not meant to vary per build) but validated as if it were. |
| `ECOMM_BOARD_ID` | `1` | `[1, 254]` | This node's identity. `0` is reserved ("unassigned"); `255` is the broadcast address. |
| `ECOMM_DEVICE_N` | `2` | `[1, 254]` | Number of unicast devices in the system (excludes the broadcast address). |
| `ECOMM_MAX_ERROR_MESSAGE_LENGTH` | `65535` | `[1, 4294967295]` | Caps the error-envelope message length and selects `error_message_length_t`'s wire width — see [The error envelope](#the-error-envelope). |
| `ECOMM_DEFAULT_TOPOLOGY` | `ECOMM_TOPOLOGY_POINT_TO_POINT` | `ECOMM_TOPOLOGY_POINT_TO_POINT` or `ECOMM_TOPOLOGY_NETWORK` | The template default for `packet_header`'s and `packet`'s `Topology` parameter. Always overridable per-instantiation regardless of this default. |

---

## The Channels Layer (`ecomm::channels`)

### `channel<Impl, Packet>` — the CRTP base

A self-contained, typed, two-way endpoint bound to one `packet<>` configuration end-to-end. `Impl`
supplies the hardware-specific byte transport by implementing two methods:

- `void do_send(const Packet&) noexcept` — write raw bytes to the physical medium.
- `bool do_try_receive(Packet&) noexcept` — read raw bytes into the supplied packet; return `true`
  if a complete packet was read.

`channel` composes `validator<Packet>` around those primitives:

```
user code
    |  send(Packet&) / try_receive()
    v
channel<Impl, Packet>          <- validates, seals; never allocates
    |  do_send / do_try_receive
    v
Impl (e.g. arduino_serial_channel)   <- raw bytes to/from hardware
    v
hardware
```

| Member | Returns | Behavior |
|---|---|---|
| `send(Packet&)` | `send_result` | Seals the packet, then `do_send`s it. Always `send_result::ok` — the unreliable channel makes no delivery guarantee; `ok` means the bytes were handed to the transport, not that they arrived. |
| `try_receive()` | `std::optional<Packet>` | `do_try_receive`s; returns the packet only if it passes `validator::is_valid` and, for `network`-topology packets, its `receiver_id` is `ECOMM_BOARD_ID` or `0xFF` (broadcast). Disengaged otherwise — nothing available, corrupt, or misaddressed are indistinguishable at this layer. |

CRTP (rather than virtual dispatch) is deliberate here: it avoids the vtable and indirect-call cost
on a microcontroller, at the price of the transport type being fixed at compile time — an
acceptable trade on an embedded target where the transport genuinely doesn't change at runtime.

### `arduino_serial_channel`

```cpp
arduino_serial_channel<Packet, tag = 0> link{Serial};   // any HardwareSerial instance
```

Wraps `HardwareSerial`. Reads/writes packets as raw binary blobs — no framing, no sync bytes, just
`sizeof(Packet)` bytes back to back. `do_try_receive` returns `false` immediately if fewer than
`sizeof(Packet)` bytes are available (checked via `Serial.available()`); never blocks. The `tag`
parameter distinguishes multiple instances bound to different ports (`Serial`, `Serial1`, ...); two
instances sharing a tag and a port is undefined behavior. Only compiled when `ARDUINO` is defined.

### `arduino_wifi_channel`

```cpp
WiFiServer server{80};
arduino_wifi_channel<Packet, tag = 0> link{server};
```

Wraps a synchronous `WiFiServer`/`WiFiClient` pair: the *firmware* is the TCP server, so a PC/Pi
client connects to it. Only compiled when `<WiFi.h>` is available. On the first `try_receive`/`send`
the channel accepts an incoming client and reuses it; only one active client is tracked at a time.
Recommended with `ChecksumPolicy = none`, since TCP already guarantees delivery and integrity.

**On ESP32/ESP8266, prefer `esp_async_wifi_channel` instead** — the synchronous `WiFiServer` API
blocks the main loop waiting for clients and bytes, which `esp_async_wifi_channel` avoids entirely.

### `esp_async_wifi_channel`

```cpp
AsyncServer server{80};
esp_async_wifi_channel<Packet, QueueDepth = 4> link{server};
// ... later:
server.begin();
```

A non-blocking channel built on AsyncTCP (ESP32) / ESPAsyncTCP (ESP8266). AsyncTCP drives the TCP
stack from a background FreeRTOS task (ESP32) or from interrupt context (ESP8266) and fires data
callbacks as bytes arrive; the channel accumulates them into a staging buffer and promotes complete
packets into a fixed-depth ring queue, keeping the main loop always responsive:

```
[TCP task / ISR]  onData callback -> accumulate into _staging -> on complete packet: push into _queue
[main loop]       try_receive(out) -> pop from _queue -> validate via channel<> base
```

This is the **only** component in ecomm that requires synchronization — the need is
platform-imposed, not a general design choice. A minimal critical-section guard (`portMUX_TYPE`
spinlock on ESP32, `noInterrupts()`/`interrupts()` on single-core ESP8266) protects only the
ring-queue head/tail update; the staging buffer is touched exclusively from the callback and needs
no lock. `send`/`try_receive` must be called from the same execution context and are not safe to
call concurrently with each other. Only one active `AsyncClient` connection is managed at a time; a
second connection attempt while one is active is rejected. `QueueDepth` must be `>= 2` (one slot is
always kept empty to distinguish full from empty); overflow policy is drop-newest (the incoming
packet is dropped, not the oldest queued one).

### `reliable_channel` — acknowledgement and retry

```cpp
reliable_channel<Impl, Packet, ClockPolicy, MaxRetries = 3, BufferDepth = 1> link{/* Impl args */};
```

Wraps any `channel<Impl, Packet>`-compatible `Impl` and adds stop-and-wait reliability. Requires
`Packet::header_t::has_seq_num` (i.e. `SequencePolicy == sequenced`), enforced by a `static_assert`.

**`send` is a blocking call** — it busy-polls the underlying channel for an acknowledgement,
retransmitting up to `MaxRetries` times. Worst case, the caller's thread (or Arduino loop) is
occupied for `MaxRetries * ClockPolicy::timeout_ticks()` ticks with no yield, sleep, or cooperative
scheduling; there is no asynchronous variant.

- `seq_num` is a wrapping `std::uint8_t`, independent per direction (`_tx_seq` outbound, `_rx_seq`
  expected inbound) — no ambiguity between data and ack traffic.
- An ack for `seq_num == N` is sent with `seq_num == N` and `header_options::ack` set, using the
  same `Packet` type with a zeroed payload.
- `try_receive` dequeues from an internal staging buffer first, then polls the underlying channel.
  Valid inbound data packets are auto-acked and returned (or staged, if the caller isn't ready yet).
  Duplicate packets (stale `seq_num`) are re-acked and silently discarded. Ack packets are consumed
  internally and never surfaced.

`ClockPolicy` must provide `using tick_type = /* unsigned integral */;`,
`static tick_type now() noexcept;`, and `static tick_type timeout_ticks() noexcept;` — tick
arithmetic uses unsigned subtraction, which wraps correctly on every target with no special-casing.
All storage is in-object (the wrapped channel, a `Packet[BufferDepth]` staging array, two
`std::uint8_t` counters) — no heap.

---

## Design Philosophy

### The three-tier error ladder

ecomm never throws C++ exceptions, anywhere, ever. Errors are handled at the earliest possible tier:

1. **`static_assert` (compile time).** The first line of defense, used aggressively, with detailed
   messages — the message is the only thing a user sees when a build breaks. Packet-size
   word-alignment, minimum packet size, and checksum-underlying-type constraints are all enforced
   here.
2. **`assert` (debug builds).** For invariants that can't be checked at compile time but are treated
   as programmer errors (e.g. `as_error` called on a packet without `header_options::error` set).
   These vanish under `NDEBUG`, so they must never have side effects release builds depend on.
3. **Error codes carried in packets (runtime, release).** The last resort, for conditions that are
   part of the protocol's normal operation — checksum mismatch, malformed packet, peer timeout.
   Carried via the error envelope (see [above](#the-error-envelope)); strongly typed, stable across
   versions, easy to inject into a response without a separate allocation path.

### No dynamic allocation

No `new`, `malloc`, `std::vector`, or `std::string` inside anything under `ecomm/protocol/`,
`ecomm/channels/`, or `ecomm/hub/`. Every buffer is a `std::byte[N]` member or `std::array<std::byte,
N>`, sized by a `std::size_t` template parameter the compiler knows at every call site. The only
sanctioned exceptions: allocations inside vendor SDK calls (ESP-IDF, Arduino core) that can't be
avoided, and — when heap ownership is genuinely unavoidable in library code — a
`std::unique_ptr<T, Deleter>`, never a raw owning pointer.

### Strategy via templates, not virtuals

Behavior varies at compile time (policy template parameters, tag types, `if constexpr`), not at
runtime via inheritance — `validator<packet<..., none>>` vs. `validator<packet<..., crc16>>` is the
canonical example: two entirely different code paths selected by the compiler, with no runtime
branch anywhere. `channel<Impl, Packet>`'s CRTP base follows the same principle for the transport
layer.

---

## The `hub` module (unmaintained)

`ecomm/hub/hub.hpp` predates the 2026-05 protocol/channel rewrite and still references an `interface`
API (`send`/`try_receive` on a generic "interface" type) that no longer exists anywhere else in this
codebase — it has not been updated to the current `channel<Impl, Packet>` design, is not included by
`channels.hpp`, is not exercised by the test suite, and does not currently compile against the rest
of the library. It is not deleted (in case its dispatch-by-active-flag design is revived for a future
multi-transport `hub` built on `channel<>`), but treat it as historical, not usable, code today. If
you need to fan a packet out across several channels, do so explicitly in application code until a
rewritten `hub` lands.

---

## The Python Client

[`ecomm-python/`](ecomm-python/README.md) is a byte-exact, dependency-checked Python implementation
of this same wire protocol — the packet format, every checksum algorithm, and the channel framing —
so a laptop, CI runner, or Raspberry Pi can talk to real ecomm firmware directly. Every checksum
algorithm and header layout it implements was verified against **this library's actual compiled
output**, not re-derived from this document. Where this library uses compile-time template
parameters, the Python side uses a runtime configuration object (`PacketSchema`) that plays the same
role, resolved at object-construction time instead of compile time.

---

## Examples

**[`examples/async_tcp/`](examples/async_tcp/)** — a complete Raspberry Pi ⇄ ESP32 demo built on
`esp_async_wifi_channel` (firmware side) and the Python `AsyncTcpChannel` (Pi side): the Pi
serializes a small `{name, age}` record into a packet payload; the ESP32 parses it and replies with
a greeting. Includes the firmware `.cpp`, the Python client, and a README with the schema-matching
table required to keep two independently-written peers wire-compatible.

---

## Edge Cases & Behavior

| Situation | Behavior |
|---|---|
| `try_receive()` on any channel when nothing has arrived | Disengaged `std::optional` — indistinguishable from "corrupt" or "misaddressed" at this layer. |
| Received packet fails `validator::is_valid` | Silently dropped by `channel::try_receive` — never surfaced as an error, only as a disengaged optional. |
| `network`-topology packet's `receiver_id` doesn't match `ECOMM_BOARD_ID` or `0xFF` | Silently dropped by `channel::try_receive` (added 2026-05-28). |
| `seal()` called twice on the same packet without re-zeroing | Idempotent — `seal` always zeroes `fcs` before hashing, so a second call reproduces the same value. |
| `ChecksumPolicy == none` | `seal` is a no-op; `is_valid` always returns `true`. |
| `as_error(packet)` on a packet without `header_options::error` set | `assert` fires in debug builds (a programmer error); use `as_error_unchecked` to skip the check. |
| A structurally malformed error envelope (declared length overruns the payload) | `as_error`/`as_error_unchecked` return `std::nullopt` — a wire condition, not an assertion failure. |
| `reliable_channel::send` never receives an ack | Blocks for up to `MaxRetries * ClockPolicy::timeout_ticks()`, then returns `send_result::timeout`. |
| A duplicate (stale `seq_num`) packet arrives at `reliable_channel::try_receive` | Re-acked, discarded, not surfaced to the caller. |
| `esp_async_wifi_channel`'s ring queue is full when a new packet completes | The new packet is dropped (drop-newest, not drop-oldest) — matches the real-time workload where stale sensor data is worthless. |
| A second client connects to `esp_async_wifi_channel` while one is already active | Rejected; only one active `AsyncClient` at a time. |
| `PacketSize` not word-aligned, or too small for the header | Compile error (`static_assert`), not a runtime failure. |
| Building on a big-endian host | Compile error (`static_assert` in `error.hpp`) rather than silently wrong wire bytes. |

---

## Assumptions & Limitations

1. **No self-description on the wire.** There is no type tag, length prefix, or version negotiation
   at the protocol level beyond the fixed 2-bit version field. Two peers are wire-compatible exactly
   when they instantiate identical template arguments; a mismatch produces silently wrong bytes, not
   an error.
2. **Single-threaded by assumption**, except `esp_async_wifi_channel`'s narrow, platform-imposed
   critical section (see [above](#esp_async_wifi_channel)). No mutexes or atomics anywhere else in
   the library. If a feature genuinely requires broader synchronization, that is a design discussion,
   not something to bolt on locally.
3. **Little-endian only, currently.** The error envelope's `memcpy`-based encoding assumes a
   little-endian host (enforced by `static_assert`); the rest of the protocol has not yet had a
   dedicated endianness sweep. Building for a big-endian target is unsupported today.
4. **Trivially-copyable packets only.** `arduino_serial_channel`, `arduino_wifi_channel`, and
   `esp_async_wifi_channel` all `static_assert(std::is_trivially_copyable_v<Packet>)` — they move
   packets with raw `memcpy`/byte-array writes.
5. **The payload has no schema.** ecomm guarantees the header and a fixed byte count; how the
   payload's bytes are laid out is entirely the application's concern — see the error envelope for
   the one built-in example of imposing a sub-structure on it, and
   [`ecomm-python`'s README](ecomm-python/README.md#serialization-is-your-responsibility) for the
   general principle stated explicitly.
6. **`hub/` is unmaintained** — see [above](#the-hub-module-unmaintained). Do not build new work on
   it without first bringing it in line with the current `channel<>` design.
7. **Scoped to a small, fixed-size-packet, single-owner protocol.** Many conventions from general
   wire-protocol design — variable-length messages, streaming validation, heterogeneous-MTU links,
   multi-tenant routing — solve problems this protocol doesn't have. Don't assume they apply here
   without checking; see [`project/guidelines.md`](project/guidelines.md) for the fuller version of
   this principle.

---

## When to Use ecomm (and When Not To)

**Good fit**

- Firmware on ESP32/ESP8266 (or other Arduino-compatible boards) that needs compact, predictable,
  fixed-size packet exchange with a coordinator or with other nodes.
- A robotics-style topology: a small number of known peers, short frequent messages, latency and
  predictability mattering more than throughput.
- Projects that want compile-time-checked wire compatibility (mismatched template arguments are a
  build-time concern, not a runtime surprise) and are willing to recompile both ends together, or
  keep them in careful lockstep, when the packet format changes.

**Reach for something else when you need**

- A self-describing, versioned, or evolvable wire format across independently-deployed peers —
  Protocol Buffers, FlatBuffers, CBOR, etc.
- Variable-length messages, streaming/partial validation, or heterogeneous-MTU links — ecomm's fixed
  packet size and flat wire format don't fit that shape.
- A multi-transport dispatcher today — `hub/` exists but is unmaintained (see
  [above](#the-hub-module-unmaintained)); you'll need to fan out across channels yourself for now.

---

## Building & Testing

The project uses CMake. `BUILD_TESTING` (from the standard `CTest` module, default `ON` when this
is the top-level project) gates the test suite, which uses GoogleTest (fetched automatically):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
cd build && ctest --output-on-failure
# or, for an aggregated summary with failure locations:
tools/run_tests.sh
```

231 tests across eight binaries: `test_packet_header`, `test_packet`, `test_validator`, `test_error`
(protocol layer); `test_arduino_serial_channel`, `test_arduino_wifi_channel`,
`test_esp_async_wifi_channel`, `test_reliable_channel` (channel layer, against drop-in mock Arduino/
AsyncTCP headers so the test suite runs on a plain host with no hardware or Arduino toolchain).

---

## Project Layout

```
ecomm/
  CMakeLists.txt              # top-level build: fetches etools/eser, defines the `ecomm` INTERFACE target
  ecomm/
    protocol/
      protocol.hpp              # aggregator
      config.hpp                 # overridable macros (ECOMM_BOARD_ID, ECOMM_PROTOCOL_VERSION, ...)
      topology.hpp                # topology enum
      sequence.hpp                # no_sequence / sequenced tags
      node_ids.hpp                 # sender_id/receiver_id storage for network topology
      checksum.hpp                # the 12 checksum policy tags (layout only)
      compute.hpp/.tpp             # the checksum algorithms
      header_type.hpp              # header_type enum
      header_options.hpp           # header_options flag enum + bitmask
      header_layout.hpp/.tpp       # the 8 standard-layout header specializations
      packet_header.hpp/.tpp       # packet_header<Topology, SequencePolicy, ChecksumPolicy>
      packet.hpp/.tpp               # packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>
      validator.hpp/.tpp            # validator<Packet>: seal() / is_valid()
      error.hpp/.tpp                 # error_code, error_envelope<PayloadSize>, as_error()
    channels/
      channels.hpp                # aggregator (platform-conditional includes)
      send_result.hpp              # send_result enum
      channel.hpp/.tpp              # channel<Impl, Packet> CRTP base
      arduino_serial_channel.hpp/.tpp
      arduino_wifi_channel.hpp/.tpp
      esp_async_wifi_channel.hpp/.tpp
      reliable_channel.hpp/.tpp     # ack/retry wrapper around any channel<>
    hub/
      hub.hpp/.tpp                # unmaintained -- see "The hub module" above
  ecomm-python/                 # the Python client (see its own README)
  examples/
    async_tcp/                    # Raspberry Pi <-> ESP32 worked example (spans both languages)
  tests/                         # GoogleTest suite, mirrors ecomm/ (protocol/, channels/)
  tools/
    run_tests.sh                 # build + run with an aggregated pass/fail summary
  project/
    guidelines.md                 # contribution and style guide
```

---

## License

**MIT License** — permissive, no restriction on commercial use. See [`LICENSE`](LICENSE) for the
full text.

---

## Contributing

See [`project/guidelines.md`](project/guidelines.md) for the full contribution and style guide —
branching model, commit message format (three `-m` flags for source commits, including an API
schematic; two for test-only commits), the architectural principles summarized in
[Design Philosophy](#design-philosophy) above, and the naming/documentation conventions. In short:
read the file you're about to modify first (the codebase is heavily templated — wrong assumptions
about a type or signature break silently), question whether a "standard" convention actually applies
to this specific, small, fixed-size-packet protocol before reaching for it, and never fabricate a
rationale for a design choice you don't understand — ask instead.

---

## Contact

Open an issue on the [repository](https://github.com/MarikTik/ecomm), or reach out via
**mtik.philosopher@gmail.com** for questions and collaboration proposals.
