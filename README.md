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

arduino_serial_channel<> link{Serial};   // named `link`, not `channel` -- that name is
                                          // already ecomm::channels::channel<Impl>

my_packet out{header_type::data, header_options::none};
std::memcpy(out.payload, "hello", 5);
(void)link.send(out);   // Packet deduced; seals (computes + writes the CRC), then writes the raw bytes

if (auto in = link.try_receive<my_packet>()) {
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
  - [`channel<Impl>` — the CRTP base](#channelimpl--the-crtp-base)
  - [`arduino_serial_channel`](#arduino_serial_channel)
  - [`arduino_wifi_channel`](#arduino_wifi_channel)
  - [`esp_async_wifi_channel`](#esp_async_wifi_channel)
  - [`reliable_channel` — acknowledgement and retry](#reliable_channel--acknowledgement-and-retry)
- [Design Philosophy](#design-philosophy)
  - [The three-tier error ladder](#the-three-tier-error-ladder)
  - [No dynamic allocation](#no-dynamic-allocation)
  - [Strategy via templates, not virtuals](#strategy-via-templates-not-virtuals)
- [The `hub` module](#the-hub-module)
- [The `router` module](#the-router-module)
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

Three library namespaces:

| Namespace | Contents |
|---|---|
| `ecomm::protocol` | The wire format: `packet_header`, `packet`, checksum policies and `compute<>`, `validator<Packet>`, the error envelope, and the plain enums (`header_type`, `header_options`, `topology`). |
| `ecomm::channels` | The transports: `channel<Impl>` (CRTP base), `arduino_serial_channel`, `arduino_wifi_channel`, `esp_async_wifi_channel`, `reliable_channel`, `send_result`, and `role` (a channel's participation in a `hub`). |
| `ecomm::fabric` | `hub<Channels...>` (explicit, caller-known-`Packet` send/receive) and `router` (heterogeneous, handler-driven dispatch). |

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

arduino_serial_channel<> link{Serial};   // named `link`, not `channel` -- that name is
                                          // already ecomm::channels::channel<Impl>

void setup() {
    Serial.begin(115200);
}

void loop() {
    my_packet out{header_type::data, header_options::none};
    out.header.receiver_id = 2;
    std::memcpy(out.payload, "ping", 4);
    (void)link.send(out);   // Packet deduced; seals (computes + writes the CRC) then writes the raw bytes

    if (auto in = link.try_receive<my_packet>()) {
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

`channel<Impl>` calls both automatically — `seal` inside `send<Packet>()`, `is_valid` inside
`try_receive<Packet>()` — so application code never calls `validator` directly in the common case.

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

### `channel<Impl>` — the CRTP base

A self-contained, two-way endpoint. `Packet` is a template parameter of `send`/`try_receive`
themselves, not of the channel — one instance can carry as many distinct packet types as the caller
needs, each validated and sealed independently per call. `Impl` supplies the hardware-specific byte
transport by implementing two methods, for every `Packet` it wishes to support:

- `void do_send(const Packet&) noexcept` — write raw bytes to the physical medium.
- `bool do_try_receive(Packet&) noexcept` — read raw bytes into the supplied packet; return `true`
  if a complete packet was read.

`Impl` may provide these as ordinary methods fixed to one `Packet` (if it has a genuine per-packet
constraint — see `esp_async_wifi_channel` below), or as member templates over `Packet` for full
flexibility (`arduino_serial_channel`, `arduino_wifi_channel` — pure byte passthrough, no per-packet
state). `channel<Impl>` doesn't care which; ordinary overload resolution decides per call, and fails
to compile with a plain "no matching function" if `Impl` doesn't support the requested `Packet`.

`channel` composes `validator<Packet>` around those primitives:

```
user code
    |  send<Packet>(Packet&) / try_receive<Packet>()
    v
channel<Impl>                  <- validates, seals; never allocates
    |  do_send / do_try_receive
    v
Impl (e.g. arduino_serial_channel)   <- raw bytes to/from hardware
    v
hardware
```

| Member | Returns | Behavior |
|---|---|---|
| `send<Packet>(Packet&)` | `send_result` | `Packet` deduced from the argument — no explicit template argument needed. Seals the packet, then `do_send`s it. Always `send_result::ok` — the unreliable channel makes no delivery guarantee; `ok` means the bytes were handed to the transport, not that they arrived. |
| `try_receive<Packet>()` | `std::optional<Packet>` | `Packet` must be named explicitly — there's no argument to deduce a return type from, a hard C++ rule. `do_try_receive`s; returns the packet only if it passes `validator::is_valid` and, for `network`-topology packets, its `receiver_id` is `ECOMM_BOARD_ID` or `0xFF` (broadcast). Disengaged otherwise — nothing available, corrupt, or misaddressed are indistinguishable at this layer. |

CRTP (rather than virtual dispatch) is deliberate here: it avoids the vtable and indirect-call cost
on a microcontroller, at the price of the transport type being fixed at compile time — an
acceptable trade on an embedded target where the transport genuinely doesn't change at runtime.

### `arduino_serial_channel`

```cpp
arduino_serial_channel<tag = 0> link{Serial};   // any HardwareSerial instance
small_packet p{...};
(void)link.send(p);                             // Packet deduced
auto in = link.try_receive<big_packet>();        // a different packet type, same instance
```

Wraps `HardwareSerial`. Reads/writes packets as raw binary blobs — no framing, no sync bytes, just
`sizeof(Packet)` bytes back to back. `do_try_receive<Packet>` returns `false` immediately if fewer
than `sizeof(Packet)` bytes are available (checked via `Serial.available()`); never blocks. Holds no
per-packet state, so one instance can freely mix packet types call to call. The `tag` parameter
distinguishes multiple instances bound to different ports (`Serial`, `Serial1`, ...); two instances
sharing a tag and a port is undefined behavior. Only compiled when `ARDUINO` is defined.

### `arduino_wifi_channel`

```cpp
WiFiServer server{80};
arduino_wifi_channel<tag = 0> link{server};
```

Wraps a synchronous `WiFiServer`/`WiFiClient` pair: the *firmware* is the TCP server, so a PC/Pi
client connects to it. Only compiled when `<WiFi.h>` is available. On the first `try_receive`/`send`
the channel accepts an incoming client and reuses it; only one active client is tracked at a time.
Recommended with `ChecksumPolicy = none`, since TCP already guarantees delivery and integrity. Like
`arduino_serial_channel`, it holds no per-packet state, so one instance can carry several packet
types over the same connection.

**On ESP32/ESP8266, prefer `esp_async_wifi_channel` instead** — the synchronous `WiFiServer` API
blocks the main loop waiting for clients and bytes, which `esp_async_wifi_channel` avoids entirely.

### `esp_async_wifi_channel`

```cpp
AsyncServer server{80};
esp_async_wifi_channel<BufferCapacity> link{server};   // BufferCapacity in bytes
// ... later:
server.begin();
```

A non-blocking channel built on AsyncTCP (ESP32) / ESPAsyncTCP (ESP8266). AsyncTCP drives the TCP
stack from a background FreeRTOS task (ESP32) or from interrupt context (ESP8266) and fires data
callbacks as bytes arrive; the channel appends them into a fixed-capacity **byte** ring, keeping the
main loop always responsive. Unlike the synchronous channels above, this one genuinely cannot be
made packet-agnostic in the callback itself — but it doesn't need to be: the ring holds raw bytes,
not typed packets, so framing happens entirely on the read side, exactly like every other channel:

```
[TCP task / ISR]  onData callback -> append raw bytes into the byte ring (guarded)
[main loop]       try_receive<Packet>(out) -> if >= sizeof(Packet) bytes buffered, pop that many
```

This means one `esp_async_wifi_channel` instance can still carry several packet types — the fixed
part is `BufferCapacity` (a byte count), not a packet type. Size it to comfortably hold at least one
instance of every packet type you intend to receive (`try_receive<Packet>` `static_assert`s
`sizeof(Packet) < BufferCapacity`).

Two consequences of buffering undifferentiated bytes instead of typed packets, both driven by the
same cause — the ring has no packet-boundary information at buffer time:

- **Overflow resets to a clean run at offset 0.** A delivery that doesn't fit in the remaining free
  space can't be resolved packet-aligned (there's no way to know where the next packet boundary
  falls), so rather than truncating it in place wherever the ring's write position currently sits —
  which could itself straddle the ring's physical wraparound point — the whole ring is reset and the
  overflowing delivery is placed fresh at offset 0, discarding whatever was buffered and not yet
  read. In the common case (an existing backlog plus this delivery don't fit together, but the
  delivery alone does) the new delivery survives whole and untorn; only a single delivery larger than
  the entire ring gets truncated (its first `BufferCapacity - 1` bytes are kept). Reads after an
  overflow can still be misaligned if the *discarded* backlog didn't end on a packet boundary — size
  `BufferCapacity` generously relative to your packet size(s) and burst rate to make overflow itself
  vanishingly unlikely.
- **Disconnect clears the entire ring**, not just a trailing partial packet — with no
  packet-boundary information, there's no way to tell a complete-but-unread packet apart from a
  partial one, so the only way to guarantee two connections' bytes never blend into one corrupted
  phantom packet is to discard everything at the connection boundary.

This is the **only** component in ecomm that requires synchronization — the need is
platform-imposed, not a general design choice. A minimal critical-section guard (`portMUX_TYPE`
spinlock on ESP32, `noInterrupts()`/`interrupts()` on single-core ESP8266) protects only the ring's
head/tail index updates and the size check that precedes each copy; the copies themselves happen
outside the lock (the producer only ever writes into the not-yet-exposed free region, the consumer
only ever reads the already-committed region — the two never actually need the lock held
simultaneously). `send`/`try_receive` must be called from the same execution context and are not
safe to call concurrently with each other. Only one active `AsyncClient` connection is managed at a
time; a second connection attempt while one is active is rejected. `BufferCapacity` must be `>= 2`
(one byte is always kept empty to distinguish full from empty).

### `reliable_channel` — acknowledgement and retry

```cpp
reliable_channel<Impl, Packet, ClockPolicy, MaxRetries = 3, BufferDepth = 1> link{/* Impl args */};
```

Wraps an `Impl` (itself a `channel<Impl>`) and adds stop-and-wait reliability. Requires
`Packet::header_t::has_seq_num` (i.e. `SequencePolicy == sequenced`), enforced by a `static_assert`.

**Unlike `channel<Impl>`, `reliable_channel` fixes its own `Packet` per instance** — its ack/retry
sequence counters and inbound staging ring are per-instance state describing one ongoing packet
stream, not a fact about `Impl`, so they can't be multiplexed across packet types the way a stateless
`channel<Impl>` call can. Use one `reliable_channel` per packet type; since it owns its `Impl` by
value (constructed from whatever arguments you pass to `reliable_channel`'s own constructor), a
stateless `Impl` like `arduino_serial_channel<>` can back several `reliable_channel`s pointed at the
same physical port, one per packet type.

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
`ecomm/channels/`, or `ecomm/fabric/`. Every buffer is a `std::byte[N]` member or `std::array<std::byte,
N>`, sized by a `std::size_t` template parameter the compiler knows at every call site. The only
sanctioned exceptions: allocations inside vendor SDK calls (ESP-IDF, Arduino core) that can't be
avoided, and — when heap ownership is genuinely unavoidable in library code — a
`std::unique_ptr<T, Deleter>`, never a raw owning pointer.

### Strategy via templates, not virtuals

Behavior varies at compile time (policy template parameters, tag types, `if constexpr`), not at
runtime via inheritance — `validator<packet<..., none>>` vs. `validator<packet<..., crc16>>` is the
canonical example: two entirely different code paths selected by the compiler, with no runtime
branch anywhere. `channel<Impl>`'s CRTP base follows the same principle for the transport layer.

---

## The `hub` module

`ecomm::hub<Channels...>` (`ecomm/fabric/hub.hpp`) combines several channels behind one
`send<Packet>()`/`try_receive<Packet>()` surface, addressed with an explicit, caller-known `Packet`
type per call — a USB hub for communication links: plug in a UART leaf link and a Wi-Fi link, then
send/receive without caring which one a packet went out or came in on. Any mix of `channel<Impl>`-derived
types and `reliable_channel<Impl, Packet, ...>` instances is accepted.

`hub` is for when *you* know the packet type. When you don't — polling for whatever arrives and
routing it to the right handler — see [the `router` module](#the-router-module) below instead;
that's a different responsibility, with its own type. Nothing stops using both over the same channels
(they're held by reference). The names mirror real networking hardware: a hub repeats to every port
with no awareness of content, a router makes a forwarding decision based on what arrived — exactly
the difference between these two types.

**Routing is capability-based, not identity-based.** Since `channel<Impl>` moved `Packet` to a
per-call template parameter, most channels have no single fixed packet type to compare against —
`arduino_serial_channel<>` and `arduino_wifi_channel<>` accept any packet type per call, and
`esp_async_wifi_channel<BufferCapacity>` accepts any packet type that fits its byte ring.
`reliable_channel` is the one exception, staying fixed to one `Packet` (see its own docs for why).
`hub` reflects this: instead of asking "what is this channel's packet type," it asks "can this
channel handle *this* `Packet`, right now" — checked structurally, not via a `packet_t` alias. The
practical effect: `send<Packet>()` reaches *every* active sender that can handle `Packet`, which for
a hub of only flexible channels usually means all of them at once.

```cpp
arduino_serial_channel<> serial_link{Serial};   // e.g. the robot's own actuators
arduino_wifi_channel<>   wifi_link{server};     // e.g. the outside world

hub h{serial_link, wifi_link};   // CTAD deduces Channels... from the arguments

(void)h.send(out);                            // Packet deduced; reaches BOTH -- both are flexible
if (auto in = h.try_receive<small_packet>()) { /* first small_packet found */ }

h.set_role<decltype(wifi_link)>(role::receiver);   // no longer a sender
(void)h.send(out);                                  // now serial_link only, until re-enabled
```

`send()` deduces `Packet` from its argument like any function template. `try_receive<Packet>()`
cannot deduce `Packet` — there's no argument to deduce a return type from, a hard rule of C++
template argument deduction — so it must be named explicitly.

**Ownership: hub does not own its channels.** It stores references to channels the caller already
constructed and keeps alive elsewhere (typically as `static`/file-scope objects, exactly like every
channel already does for the hardware it wraps). This is deliberate: `esp_async_wifi_channel`
registers its own address with AsyncTCP at construction time, so an owning container that *moves* a
channel into itself after construction would leave AsyncTCP holding a dangling pointer. Reference
storage makes that bug structurally impossible — a channel is never relocated.

**Two `static_assert`s fire at `hub<...>`'s own instantiation point**, not deep inside some call
site, if misused: every channel must be recognizable as *some* kind of channel — either it derives
from `channel<Channel>` (the flexible, per-call-`Packet` shape), or it exposes `using packet_t =
SomePacket;` with matching non-template `send`/`try_receive` (the fixed shape, e.g.
`reliable_channel`) — and every channel type in the pack must be pairwise distinct (use each
channel's `tag` parameter to disambiguate two instances of the same transport). A third check is
per-call: `send<Packet>`/`try_receive<Packet>` each `static_assert` that at least one channel in the
hub can currently handle the requested `Packet` — naming a type no channel supports is a compile
error, not a silent no-op.

**`send<Packet>()` returns one `std::optional<send_result>` per matching channel**, in declaration
order among that subset, disengaged for a channel that wasn't an active sender — so a
`reliable_channel` timing out inside a hub is visible to the caller, not silently swallowed. Note
that mixing a blocking `reliable_channel` and a non-blocking channel that both handle the same packet
type means `send()`'s worst-case latency for that call is the sum of its matching members': hub calls
every active, matching sender in sequence and cannot parallelize a blocking one away.

**`set_role<Channel>(role)`** (`ecomm::channels::role`, `channels/role.hpp`) sets a channel's
participation to exactly one of four states — `role::sender`, `role::receiver`, `role::transceiver`
(both; every channel's starting state), or `role::none` (neither) — in a single call, rather than
toggling two independent flags with separate methods. This is the way to keep a technically-capable
channel out of calls for a packet type you don't want it to see. `try_receive<Packet>()` polls active,
matching receivers in declaration order and returns the first packet found — a channel that always
has data can starve later channels in the pack within a single call; call again to keep draining.

---

## The `router` module

`ecomm::router` (`ecomm/fabric/router.hpp`) polls a set of channels for whichever packet type
arrives first and routes it to the handler for that type — for when you *don't* know the packet type
in advance, the complement to `hub`'s explicit `send<Packet>`/`try_receive<Packet>`.

**Construct it once from its `on_channel(...)` groups, then poll it.** `router` owns a small
per-channel reassembly buffer (see below), so it must persist across polls — it is not a throwaway
temporary. Class template arguments are deduced from the groups (a deduction guide is provided), so
no explicit template arguments are needed:

```cpp
ecomm::router r{
    on_channel(serial_link,
        [](small_packet& in) { /* ... */ },
        [](big_packet& in)   { /* ... */ }
    )
};
while (r.try_receive_any()) { }   // returns bool -- drains until nothing is left
```

**Candidate types come from the handlers**, not from a template argument: each handler is a callable
taking one `Packet&`, and the packet types a group's handlers declare *are* the types polled for on
that group's channel. No packet type is ever named twice. Handlers must declare a concrete parameter
type; a generic `[](auto& p)` carries no type to poll for and is rejected with a `static_assert`.

**Why it reassembles instead of doing a plain typed read.** Streaming channels (everything derived
from `channel<Impl>` — serial, both Wi-Fi channels) deliver raw bytes with no framing, and a typed
`try_receive<Packet>()` *consumes* `sizeof(Packet)` bytes the moment that many are buffered, **before**
it can check validity. With two candidate sizes on one channel that is destructive: a 48-byte packet
that has only partially arrived (say 26 of 48 bytes) fails the 48-byte probe harmlessly, but then
satisfies a 16-byte probe on byte count alone — consuming 16 bytes of the still-arriving larger
packet, failing validation, and destroying it, with no way to recover since the transport has no
peek. No backlog is required; it happens on the very first poll of a mid-arrival packet, and draining
*more* often makes it *more* likely.

`router` avoids this entirely: for each streaming channel it pulls raw bytes (`channel::receive_raw`)
into a per-channel buffer sized to that group's largest candidate, and frames them itself —
validating a candidate *before* consuming it, and keeping anything that doesn't yet form a complete,
valid packet for the next poll. A partially-arrived packet simply waits in the buffer until the rest
arrives. `reliable_channel` is exempt: it is message-atomic (it delivers whole, already-framed
packets and carries a single packet type), so `router` polls it directly with no buffering.

**Candidates are framed largest-first, automatically**, decided at compile time (`etools::meta::sort_t`
with `etools::meta::size_greater`). A smaller candidate can validate against the leading bytes of a
larger queued packet only by an FCS collision (astronomically unlikely), so testing large-to-small
frames each packet correctly — **handler declaration order never affects behavior.** Concretely,
three whole 16-byte packets buffered ahead of a 48-byte candidate now drain as three 16-byte packets
(each 16-byte prefix passes its crc16 while the 48-byte crc32 interpretation fails), rather than being
mistaken for one 48-byte packet and destroyed — the destructive-read bug this design fixes.

**Two guardrails are enforced at compile time** for a streaming channel carrying more than one
candidate type: candidate sizes must be **pairwise distinct** (two equally-sized types can't be told
apart by prefix framing), and **every candidate must carry a checksum** (`ChecksumPolicy != none`) —
a checksum-less packet's `is_valid` is unconditionally `true`, so a torn read would dispatch as
genuine. A channel carrying a single packet type has neither hazard and may still use `none`.

When different channels carry different packet types, one `on_channel(...)` group per channel keeps
each framed only for the types it actually carries:

```cpp
ecomm::router r{
    on_channel(wifi_link,
        [](telemetry_packet& in) { /* ... */ },
        [](command_packet& in)   { /* ... */ }
    ),
    on_channel(serial_link,
        [](command_packet& in)   { /* ... */ }
    )
};
while (r.try_receive_any()) { }
```

`Channel` is deduced from the argument to `on_channel` — no explicit template argument needed. The
channels are referenced, not owned, and must outlive the router. Channels are polled in the order
their groups are written; the first that yields a packet dispatches it and the call returns. **A
channel not named in a router is never polled by it** — `router` has no persistent enable/disable
state the way `hub::set_role` does; which channels participate is fixed by the groups it was built
from.

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
| `esp_async_wifi_channel`'s byte ring doesn't have room for an entire incoming delivery | The ring is reset and the delivery is placed fresh at offset 0, discarding the prior backlog. The delivery survives whole unless it alone exceeds `BufferCapacity`, in which case only its first `BufferCapacity - 1` bytes are kept. |
| `esp_async_wifi_channel`'s client disconnects with unread data still buffered | The entire ring is cleared, including any complete-but-unread packets — not just a trailing partial one (the ring has no packet-boundary information at buffer time to tell the two apart). |
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
6. **`hub` composes channels with an explicit, caller-known `Packet` per call; it holds no
   packet-type-to-handler dispatch table.** It fans a packet out to every active sender and returns
   the first packet any active matching receiver has for the `Packet` *you* named — deciding what to
   *do* with a received packet, or routing whichever of several possibly-unknown types shows up, is
   `ecomm::router`'s job (see [The `router` module](#the-router-module)), not `hub`'s.
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
- Packet-type-to-handler dispatch across independently-deployed peers with unstable message shapes —
  `ecomm::router` handles the "which handler for this packet" routing, but only within this
  library's fixed-size, compile-time-typed packet model (see
  [The `router` module](#the-router-module)); it is not a general message bus.

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

277 tests across ten binaries: `test_packet_header`, `test_packet`, `test_validator`, `test_error`
(protocol layer); `test_arduino_serial_channel`, `test_arduino_wifi_channel`,
`test_esp_async_wifi_channel`, `test_reliable_channel` (channel layer, against drop-in mock Arduino/
AsyncTCP headers so the test suite runs on a plain host with no hardware or Arduino toolchain);
`test_hub`, `test_router` (pure-software mock transports, no hardware headers needed).

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
      role.hpp                      # channels::role enum -- a channel's participation in a hub
      channel.hpp/.tpp              # channel<Impl> CRTP base
      channel_traits.hpp            # shared capability-checking traits (used by hub and router)
      arduino_serial_channel.hpp/.tpp
      arduino_wifi_channel.hpp/.tpp
      esp_async_wifi_channel.hpp/.tpp
      reliable_channel.hpp/.tpp     # ack/retry wrapper around any channel<>
    fabric/
      hub.hpp/.tpp                # hub<Channels...>: explicit, caller-known-Packet send/try_receive
      router.hpp/.tpp              # router: heterogeneous, handler-driven try_receive_any
  ecomm-python/                 # the Python client (see its own README)
  examples/
    async_tcp/                    # Raspberry Pi <-> ESP32 worked example (spans both languages)
  tests/                         # GoogleTest suite, mirrors ecomm/ (protocol/, channels/, fabric/)
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
