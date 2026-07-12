// SPDX-License-Identifier: MIT
/**
* @file reliable_channel.hpp
*
* @brief Stop-and-wait reliable channel wrapper for ecomm.
*
* @ingroup ecomm_channels ecomm::channels
*
* @par BLOCKING
* **`send` is a blocking call.** It busy-polls the underlying channel for an
* acknowledgement, retransmitting up to `MaxRetries` times. The caller's
* thread (or Arduino loop) is fully occupied for up to
* `MaxRetries * ClockPolicy::timeout_ticks()` ticks with no yield, sleep, or
* cooperative scheduling. Do not use `reliable_channel` where preemption-free
* stalls are unacceptable. An asynchronous variant is not yet implemented.
*
* `reliable_channel<Impl, Packet, ClockPolicy, MaxRetries, BufferDepth>`
* wraps a `channel<Impl, Packet>` and adds stop-and-wait reliability:
*
* - `send` stamps `header.seq_num`, transmits the packet, then polls
*   for a matching ack. If no ack arrives within `ClockPolicy::timeout_ticks()`
*   the packet is retransmitted, up to `MaxRetries` times total. On exhaustion
*   `send_result::timeout` is returned.
*
* - `try_receive` dequeues from an internal staging buffer first, then polls
*   the underlying channel. Valid inbound data packets are automatically
*   acknowledged and placed in the staging buffer (or returned directly if
*   the buffer is empty). Duplicate packets (seq_num mismatch) are re-acked
*   and silently discarded. Ack packets (header_options::ack set) are consumed
*   internally and never surfaced to the caller.
*
* @par Constraints
* - `Packet::header_t::has_seq_num` must be `true`. A `static_assert` enforces
*   this at instantiation time.
* - `ClockPolicy` must provide:
*   ```cpp
*   using tick_type =  unsigned integral;
*   static tick_type now()           noexcept;
*   static tick_type timeout_ticks() noexcept;
*   ```
*   Tick arithmetic is performed with unsigned subtraction, which wraps
*   correctly on all targets without special handling.
* - `MaxRetries` is the total number of transmissions (initial + retries).
*   A value of 1 means no retransmission.
* - `BufferDepth` controls the number of slots in the inbound staging ring.
*   Depth 1 is sufficient for stop-and-wait; increase it if the caller's
*   `try_receive` rate lags behind the remote's send rate. No heap is used --
*   the buffer is a plain array member.
*
* @par Wire protocol
* - `seq_num` is a wrapping `std::uint8_t` counter, independent per direction.
* - An ack for a data packet with `seq_num == N` is sent with `seq_num == N`
*   and `header_options::ack` set. The ack uses the same `Packet` type;
*   the payload is zeroed.
* - The two counters (`_tx_seq` for outbound, `_rx_seq` for expected inbound)
*   are independent, eliminating any ambiguity between data and ack traffic.
*
* @par No dynamic allocation
* All storage is in-object: the contained `channel<Impl, Packet>`, a
* `Packet[BufferDepth]` staging array, and two `std::uint8_t` sequence
* counters.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-28
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-28 Initial creation.
*/
#ifndef ECOMM_CHANNELS_RELIABLE_CHANNEL_HPP_
#define ECOMM_CHANNELS_RELIABLE_CHANNEL_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>

#include "channel.hpp"
#include "send_result.hpp"

namespace ecomm::channels {

    /**
    * @class reliable_channel
    *
    * @brief Stop-and-wait reliable wrapper around `channel<Impl, Packet>`.
    *
    * Exposes the same `send` / `try_receive` surface as `channel` but adds
    * acknowledgement, retransmission, and duplicate filtering.
    *
    * @tparam Impl        Hardware transport. Must satisfy the same `do_send` /
    *                     `do_try_receive` contract as for `channel<Impl, Packet>`.
    * @tparam Packet      Fixed packet type. Must satisfy
    *                     `Packet::header_t::has_seq_num == true`.
    * @tparam ClockPolicy Provides `tick_type`, `now()`, and `timeout_ticks()`.
    * @tparam MaxRetries  Total transmission attempts before returning
    *                     `send_result::timeout`. Must be >= 1. Defaults to 3.
    * @tparam BufferDepth Number of inbound packet slots in the staging ring.
    *                     Defaults to 1.
    */
    template<
        typename Impl,
        typename Packet,
        typename ClockPolicy,
        std::size_t MaxRetries   = 3,
        std::size_t BufferDepth  = 1
    >
    class reliable_channel {

        static_assert(
            Packet::header_t::has_seq_num,
            "reliable_channel: Packet::header_t::has_seq_num must be true. "
            "Instantiate packet_header with SequencePolicy = sequenced."
        );
        static_assert(MaxRetries >= 1,
            "reliable_channel: MaxRetries must be at least 1.");
        static_assert(BufferDepth >= 1,
            "reliable_channel: BufferDepth must be at least 1.");

    public:

        // -----------------------------------------------------------------
        // Type aliases
        // -----------------------------------------------------------------

        /// @brief The underlying unreliable channel type (Impl derives from it).
        using channel_t  = Impl;

        /// @brief Tick type supplied by ClockPolicy.
        using tick_type  = typename ClockPolicy::tick_type;

        // -----------------------------------------------------------------
        // Constructor
        // -----------------------------------------------------------------

        /**
        * @brief Construct a reliable_channel, forwarding args to `Impl`.
        *
        * All arguments are forwarded to the `Impl` constructor. Example:
        * ```cpp
        * reliable_channel<my_impl, my_packet, my_clock> ch{serial_port};
        * ```
        *
        * @tparam Args Constructor argument types for `Impl`.
        * @param  args Arguments forwarded to `Impl`.
        */
        template<typename... Args>
        explicit reliable_channel(Args&&... args) noexcept;

        // -----------------------------------------------------------------
        // Public API  --  mirrors channel<Impl, Packet>
        // -----------------------------------------------------------------

        /**
        * @brief Send a packet with stop-and-wait acknowledgement.
        *
        * @par Blocking
        * **This call busy-polls until an ack is received or the retry budget is
        * exhausted.** Worst-case hold time is
        * `MaxRetries * ClockPolicy::timeout_ticks()` ticks.
        *
        * Stamps `packet.header.seq_num` with the current outbound counter,
        * transmits via the inner channel, then polls for a matching ack.
        * If no ack arrives within `ClockPolicy::timeout_ticks()` the packet
        * is retransmitted. At most `MaxRetries` transmission attempts are made.
        *
        * On success the outbound sequence counter is incremented (wrapping at
        * 255). The FCS field is recomputed on each transmission attempt.
        *
        * @param[in,out] packet Packet to send. `header.seq_num` is overwritten.
        *                       `header.fcs` is overwritten on each attempt.
        *
        * @return `send_result::ok`      -- ack received within retry budget.
        * @return `send_result::timeout` -- all retransmit attempts exhausted.
        */
        [[nodiscard]] send_result send(Packet& packet) noexcept;

        /**
        * @brief Attempt to receive a data packet.
        *
        * Checks the internal staging buffer first, then polls the inner
        * channel. Inbound data packets (ack bit clear) whose `seq_num` matches
        * the expected inbound counter are accepted: an ack is sent back
        * automatically and the packet is returned to the caller. Packets with
        * a stale `seq_num` (duplicate retransmit from the remote) are re-acked
        * and discarded. Ack packets (ack bit set) are consumed internally.
        *
        * @return An engaged `std::optional<Packet>` holding the received data
        *         packet, or `std::nullopt` if nothing was available.
        */
        [[nodiscard]] std::optional<Packet> try_receive() noexcept;

    private:

        // -----------------------------------------------------------------
        // Internal helpers
        // -----------------------------------------------------------------

        /// @brief Send an ack for the packet with the given seq_num.
        void send_ack(std::uint8_t seq) noexcept;

        /// @brief Poll the inner channel once for an ack matching `seq`.
        /// @return `true` if a matching ack was received.
        bool poll_ack(std::uint8_t seq) noexcept;

        /// @brief Push a packet into the staging ring.
        /// @return `true` if there was room; `false` if the ring was full
        ///         (packet silently dropped).
        bool stage_push(const Packet& pkt) noexcept;

        /// @brief Pop a packet from the staging ring into `out`.
        /// @return `true` if a packet was available.
        bool stage_pop(Packet& out) noexcept;

        // -----------------------------------------------------------------
        // Data members
        // -----------------------------------------------------------------

        /// @brief The wrapped unreliable channel (owns the Impl instance).
        channel_t _channel;

        /// @brief Outbound sequence counter. Incremented after each acked send.
        std::uint8_t _tx_seq{0};

        /// @brief Expected inbound sequence counter. Incremented on each
        ///        accepted data packet.
        std::uint8_t _rx_seq{0};

        // Staging ring buffer  --  no heap, fixed size at compile time.
        // Uses head + count (rather than the one-slot-wasted head/tail scheme)
        // so all BufferDepth slots are usable.

        /// @brief Staging ring storage: received data packets awaiting pop.
        Packet _stage[BufferDepth]{};

        /// @brief Index of the next write slot.
        std::size_t _stage_head{0};

        /// @brief Number of packets currently in the ring.
        std::size_t _stage_count{0};
    };

} // namespace ecomm::channels

#include "reliable_channel.tpp"
#endif // ECOMM_CHANNELS_RELIABLE_CHANNEL_HPP_
