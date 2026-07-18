// SPDX-License-Identifier: MIT
/**
* @file esp_async_wifi_channel.hpp
*
* @brief Async Wi-Fi channel for ESP32 and ESP8266 using AsyncTCP / ESPAsyncTCP.
*
* @ingroup ecomm_channels ecomm::channels
*
* Provides `esp_async_wifi_channel<BufferCapacity>`, a non-blocking channel
* built on top of the AsyncTCP (ESP32) or ESPAsyncTCP (ESP8266) library. The
* public API is identical to every other `channel<>` concrete implementation:
* `send<Packet>(Packet&)` and `try_receive<Packet>()`.
*
* @par Why async
* The synchronous `WiFiServer` / `WiFiClient` API blocks until a client connects
* or bytes arrive, which stalls the entire microcontroller loop. AsyncTCP drives
* the TCP stack from a background FreeRTOS task (ESP32) or from interrupt context
* (ESP8266) and fires data callbacks as bytes arrive. The channel accumulates
* incoming bytes in a fixed-capacity byte ring buffer, keeping the main loop
* always responsive.
*
* @par A byte ring, not a packet ring -- Packet is per-call, like every other channel<>
* Earlier versions of this channel fixed one `Packet` type at construction and
* kept a ring of `Packet`-sized slots, because the AsyncTCP data callback
* needed to know a packet's byte length to decide when a complete unit had
* arrived. This version instead buffers **raw bytes** in a ring of
* `BufferCapacity` bytes -- the callback has no notion of "a packet" at all,
* it simply appends whatever bytes AsyncTCP delivers. Framing happens entirely
* on the read side: `try_receive<Packet>()` (like every other `channel<>`)
* checks whether at least `sizeof(Packet)` bytes are currently buffered and,
* if so, pops exactly that many. This is what lets one `esp_async_wifi_channel`
* instance carry several distinct `Packet` types over the same TCP connection,
* consistent with `arduino_serial_channel` and `arduino_wifi_channel`.
*
* @par Internal architecture
* ```
* [TCP task / ISR]
*     onData callback
*         -> append raw bytes into the byte ring (guarded by critical section)
*
* [main loop]
*     try_receive<Packet>(out)
*         -> if >= sizeof(Packet) bytes buffered, pop that many (guarded)
*         -> validate via channel<> base
* ```
*
* @par Overflow resets to a clean run at offset 0
* Because the ring holds undifferentiated bytes, there is no way to know, at
* buffer time, where the next packet boundary falls -- so an overflow (this
* delivery doesn't fit in the remaining free space) cannot be resolved by
* dropping precisely one packet's worth of bytes the way the old Packet-typed
* ring could. Instead, `on_data` resets the ring to a clean run starting at
* offset 0 and places this delivery there, discarding whatever was buffered
* and not yet read. In the overwhelmingly common case -- an existing backlog
* plus this delivery together don't fit, but the delivery alone does -- the
* new delivery is kept whole and untorn; only in the rarer case of a single
* delivery larger than the entire ring is it itself truncated (its first
* `BufferCapacity - 1` bytes are kept). Either way, whether the retained
* bytes start exactly on a packet boundary depends on whether the discarded
* backlog itself ended cleanly on one, which `on_data` has no way to know;
* `try_receive<Packet>()` calls after an overflow may therefore still read
* misaligned data until enough bytes have flowed past to resync (rejected by
* `validator<Packet>::is_valid`, so nothing structurally invalid is ever
* handed to the caller). Size `BufferCapacity` generously relative to your
* packet size and expected burst rate to make overflow itself vanishingly
* unlikely in practice.
*
* @par Disconnect clears the entire ring
* For the same reason, a disconnect clears **all** buffered bytes, including
* any complete-but-not-yet-read packets -- not just a trailing partial one.
* With no packet-boundary information at buffer time, there is no way to tell
* "a complete packet awaiting `try_receive`" apart from "a partial packet cut
* short by the disconnect", so the only way to guarantee bytes from two
* different connections never blend into one corrupted phantom packet is to
* discard everything at the connection boundary. (The previous Packet-typed
* ring could and did preserve already-completed packets across a disconnect;
* this version trades that away for the flexibility of not fixing a packet
* type in the first place.)
*
* @par No dynamic allocation
* All storage is compile-time fixed: `_ring` is a `std::array<std::byte,
* BufferCapacity>`. No heap allocation ever occurs in this channel.
*
* @par Concurrency note
* This channel is the *only* component in ecomm that requires synchronisation.
* The need is platform-imposed: AsyncTCP delivers data on a task/ISR that runs
* concurrently with user code. A minimal critical-section guard (platform-selected
* at compile time) protects only the ring's head/tail index updates and the
* size check that precedes each copy -- the copies themselves happen outside
* the lock (see `on_data`/`do_try_receive` for the invariant that makes this
* safe: the producer only ever writes into the not-yet-exposed free region,
* and the consumer only ever reads the already-committed region, so the two
* never touch the same bytes without a lock actually being needed).
*
* On ESP32 (dual-core): `taskENTER_CRITICAL` / `taskEXIT_CRITICAL` with a
* `portMUX_TYPE` spinlock  --  safe across cores.
* On ESP8266 (single-core): `noInterrupts()` / `interrupts()`  --  sufficient since
* the TCP callback fires from interrupt context, not a parallel core.
*
* @par Platform guard
* Compiled only when `ESP32` or `ESP8266` is defined. Include via the aggregator
* `channels.hpp` or directly  --  the guard is self-contained.
*
* @see channel.hpp
* @see arduino_wifi_channel.hpp  --  synchronous fallback for non-ESP Arduino boards.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-26
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-26 Initial creation.
* - 2026-07-16 Replaced the fixed-Packet, fixed-depth `std::array<Packet,
*      QueueDepth>` ring (plus a separate `Packet`-sized staging accumulator)
*      with a single `std::array<std::byte, BufferCapacity>` byte ring.
*      `Packet` moved from a class-level parameter to a per-call one on
*      `do_send`/`do_try_receive` (matching `channel<Impl>`'s new contract),
*      so one instance can now carry several distinct packet types over the
*      same connection. This requires clearing the entire ring (not just a
*      partial tail) on disconnect, since the ring no longer has any
*      packet-boundary information at buffer time -- see the file-level
*      "Disconnect clears the entire ring" note.
* - 2026-07-17 `on_data`'s overflow handling changed from "truncate the tail
*      of the incoming delivery in place, keep the existing backlog" to
*      "reset to a clean run at offset 0 and place this delivery there,
*      discarding the existing backlog" -- avoids ever writing a truncated
*      chunk straddling the ring's physical wrap boundary, and in the common
*      case (an existing backlog plus this delivery don't fit together, but
*      the delivery alone does) keeps the new delivery whole and untorn
*      instead of splitting it. See the file-level "Overflow resets to a
*      clean run at offset 0" note (renamed from "Overflow desyncs framing
*      until reconnect").
*/
#ifndef ECOMM_CHANNELS_ESP_ASYNC_WIFI_CHANNEL_HPP_
#define ECOMM_CHANNELS_ESP_ASYNC_WIFI_CHANNEL_HPP_

#if defined(ESP32)
    #include <AsyncTCP.h>
#elif defined(ESP8266)
    #include <ESPAsyncTCP.h>
#else
    #define ECOMM_NO_ESP_ASYNC_WIFI_SUPPORT
    #pragma message "esp_async_wifi_channel requires ESP32 or ESP8266. This file will not be compiled."
#endif

#ifndef ECOMM_NO_ESP_ASYNC_WIFI_SUPPORT

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "channel.hpp"

namespace ecomm::channels {

    /**
    * @class esp_async_wifi_channel
    *
    * @brief Non-blocking Wi-Fi channel for ESP32 / ESP8266 via AsyncTCP.
    *
    * Wraps an `AsyncServer` and maintains a fixed-capacity byte ring of
    * incoming data. Call `try_receive<Packet>()` from your main loop  --  it
    * returns immediately whether or not a complete packet is waiting. Bytes
    * are appended to the ring by the library's internal data callback;
    * framing into packets happens entirely on the read side, so the same
    * channel instance can serve several distinct `Packet` types.
    *
    * Only one active `AsyncClient` connection is managed at a time. If a second
    * client connects while one is already active, the new connection is rejected.
    *
    * @tparam BufferCapacity Size, in bytes, of the internal ring buffer. Must
    *                        be large enough to hold at least one instance of
    *                        every `Packet` type you intend to receive through
    *                        this channel (`do_try_receive<Packet>` static_asserts
    *                        `sizeof(Packet) < BufferCapacity` -- one byte is
    *                        always kept empty to distinguish full from empty).
    *                        Size generously relative to your packet size(s)
    *                        and expected burst rate: see the file-level
    *                        "Overflow resets to a clean run at offset 0" note
    *                        for why running out of room is worse here than
    *                        simply losing one packet.
    *
    * @warning `send` and `try_receive` must be called from the same execution
    *          context (your main loop / task). They are not safe to call
    *          concurrently with each other. The internal data callback is the only
    *          code that runs on a separate context, and it is guarded appropriately.
    */
    template<std::size_t BufferCapacity>
    class esp_async_wifi_channel
        : public channel<esp_async_wifi_channel<BufferCapacity>>
    {
        static_assert(BufferCapacity >= 2,
            "esp_async_wifi_channel: BufferCapacity must be >= 2. "
            "One byte is always kept empty to distinguish full from empty; "
            "a capacity of 1 would make the ring permanently appear full.");

    public:
        /**
        * @brief Construct the channel and begin listening for connections.
        *
        * Registers `AsyncServer` callbacks immediately. The server starts
        * accepting connections as soon as the Arduino WiFi stack is up  --  the
        * caller is responsible for calling `WiFi.begin(...)` and waiting for
        * connection before constructing this channel.
        *
        * @param[in] server Reference to an already-constructed `AsyncServer`.
        *                   The server must outlive this channel object.
        *
        * @pre The `AsyncServer` must not have had `begin()` called on it yet, or
        *      must be in a state where registering a new `onClient` callback is
        *      safe. Typically: construct `AsyncServer port(N)`, then construct
        *      this channel, then call `server.begin()`.
        */
        explicit esp_async_wifi_channel(AsyncServer& server) noexcept;

    private:
        friend class channel<esp_async_wifi_channel<BufferCapacity>>;

        /**
        * @brief Write a sealed packet to the active client.
        *
        * Called by `channel::send` after the packet has been sealed. If no
        * client is currently connected the packet is silently discarded  --  the
        * caller observes this as a no-op send (same behaviour as
        * `arduino_wifi_channel` when no client is available).
        *
        * @tparam Packet Deduced from `packet`'s type.
        * @param[in] packet Sealed packet to transmit. Written as a raw byte
        *                   block of `sizeof(Packet)` bytes.
        *
        * @note AsyncTCP buffers the write internally; bytes may not be on the
        *       wire before this call returns.
        */
        template<typename Packet>
        void do_send(const Packet& packet) noexcept;

        /**
        * @brief Pop one complete packet's worth of bytes from the ring.
        *
        * Called by `channel::try_receive`. Returns `false` immediately if
        * fewer than `sizeof(Packet)` bytes are currently buffered
        * (non-blocking). If enough bytes are available they are copied into
        * `out` and the ring's read position is advanced by `sizeof(Packet)`.
        *
        * @tparam Packet Deduced from `out`'s type. `static_assert`s that
        *                `sizeof(Packet) < BufferCapacity`.
        * @param[out] out Destination packet. Written only on `true` return.
        * @return `true`   --  `sizeof(Packet)` bytes were copied into `out`.
        * @return `false`  --  fewer than `sizeof(Packet)` bytes were buffered;
        *                      `out` is unchanged.
        *
        * @note The copy is intentionally performed outside the critical
        *       section. The read position is the exclusive domain of the main
        *       task (only this method advances it), and the producer
        *       (`on_data`) only ever writes into the not-yet-exposed free
        *       region ahead of the write position. Since enough bytes were
        *       verified buffered under lock before the copy, the region being
        *       read is guaranteed stable for the duration of the copy.
        */
        template<typename Packet>
        bool do_try_receive(Packet& out) noexcept;

        // --- Internal callback handlers (called from TCP task / ISR) --------

        /**
        * @brief Called by AsyncTCP when a new client connects.
        *
        * Accepts the first connection and registers per-client callbacks.
        * Subsequent connections while a client is active are rejected.
        *
        * @param[in] client Pointer to the newly connected `AsyncClient`.
        *                   Owned by AsyncTCP; do not delete.
        */
        void on_client(AsyncClient* client) noexcept;

        /**
        * @brief Called by AsyncTCP when bytes arrive from the active client.
        *
        * If this delivery fits in the remaining free space, appends it to the
        * ring, wrapping through it as needed. If it doesn't, resets the ring
        * to a clean run starting at offset 0 and places this delivery there
        * instead, discarding whatever was buffered and not yet read -- see
        * the file-level "Overflow resets to a clean run at offset 0" note.
        *
        * @param[in] data  Pointer to the received byte block.
        * @param[in] len   Number of bytes in this delivery.
        *
        * @note Runs on the TCP task (ESP32) or from interrupt context (ESP8266).
        *       Reads the current free space under the critical section, then
        *       copies into either the pre-cleared free region (normal path)
        *       or the whole ring from offset 0 (overflow path) without
        *       holding the lock, then commits the new write position under
        *       the lock -- symmetric with `do_try_receive`'s locking
        *       discipline.
        */
        void on_data(const void* data, std::size_t len) noexcept;

        /**
        * @brief Called by AsyncTCP when the active client disconnects.
        *
        * Clears `_client` and the entire ring buffer -- see the file-level
        * "Disconnect clears the entire ring" note for why a partial clear is
        * not possible here.
        *
        * @param[in] client Pointer to the disconnecting client (for identification).
        */
        void on_disconnect(AsyncClient* client) noexcept;

        // --- Ring buffer helpers (byte-oriented) -----------------------------

        /**
        * @brief Number of bytes currently buffered and available to read.
        *
        * @warning Reads `_head`/`_tail`; call only while holding `_cs`, or
        *          treat the result as a racy snapshot for diagnostics only.
        */
        [[nodiscard]] std::size_t bytes_available() const noexcept;

        /**
        * @brief Number of bytes of free space remaining before the ring is full.
        *
        * @warning Same locking requirement as `bytes_available`.
        */
        [[nodiscard]] std::size_t bytes_free() const noexcept;

        // --- Critical section (platform-selected at compile time) -----------

        /**
        * @struct critical_section
        *
        * @brief Minimal RAII guard that serialises access to the ring buffer's
        *        head/tail indices between the TCP callback and the main loop.
        *
        * On ESP32 (dual-core) uses a `portMUX_TYPE` spinlock via
        * `taskENTER_CRITICAL` / `taskEXIT_CRITICAL`.
        * On ESP8266 (single-core ISR) disables interrupts via
        * `noInterrupts()` / `interrupts()`.
        */
        struct critical_section {
#if defined(ESP32)
            portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
            void enter() noexcept { taskENTER_CRITICAL(&_mux); }
            void exit()  noexcept { taskEXIT_CRITICAL(&_mux);  }
#else // ESP8266
            void enter() noexcept { noInterrupts(); }
            void exit()  noexcept { interrupts();   }
#endif
        };

        // --- Data members ---------------------------------------------------

        AsyncServer&  _server;                        ///< Bound async TCP server.
        AsyncClient*  _client  = nullptr;             ///< Active connection; null when idle.

        /// Fixed-capacity byte ring. Valid, unread bytes occupy [_tail, _head)
        /// (mod BufferCapacity); one byte is always kept empty to distinguish
        /// full from empty without a separate counter.
        std::array<std::byte, BufferCapacity> _ring{};
        std::size_t _head = 0;    ///< Next write index (producer: TCP callback).
        std::size_t _tail = 0;    ///< Next read  index (consumer: try_receive).

        critical_section _cs{};   ///< Guards head/tail updates and their reads.
    };

} // namespace ecomm::channels

#include "esp_async_wifi_channel.tpp"
#endif // ECOMM_NO_ESP_ASYNC_WIFI_SUPPORT
#endif // ECOMM_CHANNELS_ESP_ASYNC_WIFI_CHANNEL_HPP_
