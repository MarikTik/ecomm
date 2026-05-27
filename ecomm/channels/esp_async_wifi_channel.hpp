// SPDX-License-Identifier: BSL-1.1
/**
* @file esp_async_wifi_channel.hpp
*
* @brief Async Wi-Fi channel for ESP32 and ESP8266 using AsyncTCP / ESPAsyncTCP.
*
* @ingroup ecomm_channels ecomm::channels
*
* Provides `esp_async_wifi_channel<Packet, QueueDepth>`, a non-blocking typed
* channel built on top of the AsyncTCP (ESP32) or ESPAsyncTCP (ESP8266) library.
* The public API is identical to every other `channel<>` concrete implementation:
* `send(Packet&)` and `try_receive(Packet&)`.
*
* @par Why async
* The synchronous `WiFiServer` / `WiFiClient` API blocks until a client connects
* or bytes arrive, which stalls the entire microcontroller loop. AsyncTCP drives
* the TCP stack from a background FreeRTOS task (ESP32) or from interrupt context
* (ESP8266) and fires data callbacks as bytes arrive. The channel accumulates
* incoming bytes in a staging buffer and moves complete packets into a fixed-depth
* ring queue, keeping the main loop always responsive.
*
* @par Internal architecture
* ```
* [TCP task / ISR]
*     onData callback
*         -> accumulate into _staging (sizeof(Packet) bytes)
*         -> on complete packet: push into _queue (guarded by critical section)
*
* [main loop]
*     try_receive(out)
*         -> pop from _queue (guarded by critical section)
*         -> validate via channel<> base
* ```
*
* @par Concurrency note
* This channel is the *only* component in ecomm that requires synchronisation.
* The need is platform-imposed: AsyncTCP delivers data on a task/ISR that runs
* concurrently with user code. A minimal critical-section guard (platform-selected
* at compile time) protects only the ring-queue head/tail update  --  the staging
* buffer is touched exclusively from the callback and needs no lock.
*
* On ESP32 (dual-core): `taskENTER_CRITICAL` / `taskEXIT_CRITICAL` with a
* `portMUX_TYPE` spinlock  --  safe across cores.
* On ESP8266 (single-core): `noInterrupts()` / `interrupts()`  --  sufficient since
* the TCP callback fires from interrupt context, not a parallel core.
*
* @par No dynamic allocation
* All storage is compile-time fixed:
* - `_slots`  --  `std::array<Packet, QueueDepth>` ring buffer.
* - `_staging`  --  `std::byte[sizeof(Packet)]` partial-packet accumulator.
* Both live as class members; no heap allocation ever occurs in this channel.
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
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2026 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-26 Initial creation.
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
    * Wraps an `AsyncServer` and maintains a fixed-depth ring queue of complete
    * received packets. Call `try_receive` from your main loop  --  it returns
    * immediately whether or not a packet is waiting. Incoming bytes are
    * accumulated in a staging buffer by the library's internal data callback and
    * promoted to the ring queue one packet at a time.
    *
    * Only one active `AsyncClient` connection is managed at a time. If a second
    * client connects while one is already active, the new connection is rejected.
    *
    * @tparam Packet     The fixed packet type this channel operates on. Must be
    *                    trivially copyable  --  it is copied by value into the ring
    *                    buffer and out of it.
    * @tparam QueueDepth Number of complete packets the ring buffer can hold.
    *                    Must be >= 2 (one slot is always kept empty to distinguish
    *                    full from empty). A value of 2 - 4 is typical; increase only
    *                    if your packet rate can outpace your `try_receive` call rate.
    *
    * @warning `send` and `try_receive` must be called from the same execution
    *          context (your main loop / task). They are not safe to call
    *          concurrently with each other. The internal data callback is the only
    *          code that runs on a separate context, and it is guarded appropriately.
    */
    template<typename Packet, std::size_t QueueDepth = 4>
    class esp_async_wifi_channel
        : public channel<esp_async_wifi_channel<Packet, QueueDepth>, Packet>
    {
        static_assert(std::is_trivially_copyable_v<Packet>,
            "esp_async_wifi_channel: Packet must be trivially copyable; "
            "memcpy is used to move packets in and out of the ring buffer.");

        static_assert(QueueDepth >= 2,
            "esp_async_wifi_channel: QueueDepth must be >= 2. "
            "One slot is always kept empty to distinguish full from empty; "
            "a depth of 1 would make the queue permanently appear full.");

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
        friend class channel<esp_async_wifi_channel<Packet, QueueDepth>, Packet>;

        /**
        * @brief Write a sealed packet to the active client.
        *
        * Called by `channel::send` after the packet has been sealed. If no
        * client is currently connected the packet is silently discarded  --  the
        * caller observes this as a no-op send (same behaviour as
        * `arduino_wifi_channel` when no client is available).
        *
        * @param[in] packet Sealed packet to transmit. Written as a raw byte
        *                   block of `sizeof(Packet)` bytes.
        *
        * @note AsyncTCP buffers the write internally; bytes may not be on the
        *       wire before this call returns.
        */
        void do_send(const Packet& packet) noexcept;

        /**
        * @brief Pop one complete packet from the ring queue.
        *
        * Called by `channel::try_receive`. Returns `false` immediately if the
        * queue is empty (non-blocking). If a packet is available it is copied
        * into `out` and the tail index is advanced.
        *
        * @param[out] out Destination packet. Written only on `true` return.
        * @return `true`   --  a complete packet was copied into `out`.
        * @return `false`  --  the queue is empty; `out` is unchanged.
        *
        * @note On `false` return `out` is guaranteed unchanged  --  unlike the
        *       synchronous channels which read before validating, this channel
        *       only writes `out` after the packet is already in the queue.
        */
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
        * Accumulates bytes into `_staging`. When `sizeof(Packet)` bytes have
        * been collected the complete packet is pushed into the ring queue.
        * Excess bytes beyond a packet boundary are carried over into the next
        * accumulation cycle  --  this handles the case where the TCP layer
        * delivers multiple packets in a single callback.
        *
        * @param[in] data  Pointer to the received byte block.
        * @param[in] len   Number of bytes in this delivery.
        *
        * @note Runs on the TCP task (ESP32) or from interrupt context (ESP8266).
        *       Only the ring-queue head update is guarded by the critical section;
        *       `_staging` is written exclusively from this callback.
        */
        void on_data(const void* data, std::size_t len) noexcept;

        /**
        * @brief Called by AsyncTCP when the active client disconnects.
        *
        * Clears `_client` and resets the staging buffer so the channel is
        * ready to accept a new connection cleanly.
        *
        * @param[in] client Pointer to the disconnecting client (for identification).
        */
        void on_disconnect(AsyncClient* client) noexcept;

        // --- Ring queue helpers ---------------------------------------------

        /**
        * @brief Push a complete packet from `_staging` into the ring queue.
        *
        * Called from `on_data` after a full packet has been accumulated.
        * Silently drops the packet if the queue is full (overflow policy:
        * drop oldest-waiting, keep newest  --  matches the real-time workload
        * where stale sensor data is worthless).
        *
        * @note Guarded by the critical section  --  may run on TCP task / ISR.
        */
        void push_packet() noexcept;

        /**
        * @brief Return `true` if the ring queue is empty.
        *
        * @note Read of `_head` from the consumer side is guarded by the
        *       critical section on ESP32 (where it may change on another core).
        */
        [[nodiscard]] bool queue_empty() const noexcept;

        /**
        * @brief Return `true` if the ring queue is full.
        *
        * Full is defined as `(_head + 1) % QueueDepth == _tail`. One slot is
        * always kept empty to distinguish full from empty without a counter.
        */
        [[nodiscard]] bool queue_full() const noexcept;

        // --- Critical section (platform-selected at compile time) -----------

        /**
        * @struct critical_section
        *
        * @brief Minimal RAII guard that serialises access to the ring-queue
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

        /// Fixed ring buffer of complete packets.
        std::array<Packet, QueueDepth> _slots{};
        std::size_t _head = 0;    ///< Next write index (producer: TCP callback).
        std::size_t _tail = 0;    ///< Next read  index (consumer: try_receive).

        /// Partial-packet accumulator  --  written exclusively from the data callback.
        std::byte   _staging[sizeof(Packet)]{};
        std::size_t _staging_used = 0;

        critical_section _cs{};   ///< Guards head/tail updates.
    };

} // namespace ecomm::channels

#include "esp_async_wifi_channel.tpp"
#endif // ECOMM_NO_ESP_ASYNC_WIFI_SUPPORT
#endif // ECOMM_CHANNELS_ESP_ASYNC_WIFI_CHANNEL_HPP_
