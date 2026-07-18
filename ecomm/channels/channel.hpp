// SPDX-License-Identifier: MIT
/**
* @file channel.hpp
*
* @brief CRTP base class for validated two-way communication channels.
*
* @ingroup ecomm_channels ecomm::channels
*
* A `channel<Impl>` is a self-contained, two-way communication endpoint. It
* depends only on `Impl` -- the packet type is a template parameter of
* `send`/`try_receive` themselves, not of the channel. One channel instance
* (one `HardwareSerial&`, one TCP connection, ...) can therefore carry as many
* distinct `Packet` types as the caller needs, each validated and sealed
* independently on every call.
*
* Derived classes supply the hardware-specific byte transport by implementing:
* - `do_send(const Packet&) noexcept`  --  write raw bytes to the physical medium.
* - `do_try_receive(Packet&) noexcept -> bool`  --  read raw bytes from the medium
*   into the supplied packet; return `true` if a complete packet was read.
*
* `Impl` may implement these as ordinary methods for one fixed `Packet` (see
* `esp_async_wifi_channel`, which has a genuine per-packet-type constraint --
* its async ring buffer and staging accumulator are sized for one packet type
* chosen at construction, independent of any `send`/`try_receive` call), or as
* member templates over `Packet` for full flexibility (see
* `arduino_serial_channel`, `arduino_wifi_channel` -- pure byte passthrough
* with no per-packet state, so nothing stops one instance handling several
* packet types). `channel<Impl>` itself does not care which; ordinary C++
* overload resolution on `do_send`/`do_try_receive` decides per call, and
* fails to compile with a plain "no matching function" if `Impl` does not
* support the requested `Packet`.
*
* The base `channel` then composes validation (`validator<Packet>`) around those
* primitives so that callers always work with structurally valid packets.
*
* @par Return types
* - `send` returns `send_result::ok` unconditionally. The unreliable channel
*   makes no delivery guarantee; `ok` means the bytes were handed to the
*   transport, not that they were received. `reliable_channel` can return
*   `send_result::timeout`.
* - `try_receive` returns `std::optional<Packet>`. A disengaged optional means
*   nothing was available or the packet failed validation. An engaged optional
*   holds the validated packet by value.
*
* @par Packet is per-call, not per-channel
* `send<Packet>` deduces `Packet` from its argument, exactly like any function
* template -- `ch.send(my_packet)` needs no explicit template argument.
* `try_receive<Packet>` cannot deduce it (there is no argument to deduce a
* return type from, a hard rule of C++ template argument deduction), so
* `Packet` must be named explicitly: `ch.try_receive<my_packet>()`.
*
* @par Layering
* ```
* user code
*     |  send<Packet>(Packet&) / try_receive<Packet>()
*     v
* channel<Impl>                  <- validates, seals; never allocates
*     |  do_send / do_try_receive
*     v
* Impl (e.g. arduino_serial_channel)   <- raw bytes to/from hardware
*     v
* hardware
* ```
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
* - 2026-05-26 Renamed from interfaces/interface.hpp; Packet baked into the
*              channel type; `delegate_` prefix replaced with `do_`.
* - 2026-05-27 send() now returns send_result (was void).
*              try_receive() now returns std::optional<Packet> (was bool + out-param).
* - 2026-07-14 Added public `packet_t` alias for generic code (`ecomm::hub`).
* - 2026-07-16 Dropped the class-level `Packet` parameter: `channel<Impl, Packet>`
*      became `channel<Impl>`, with `send`/`try_receive` templated on `Packet` per
*      call instead. A channel with no genuine per-packet-type state (e.g.
*      `arduino_serial_channel`, a pure byte passthrough) can now carry several
*      distinct packet types through one instance; a channel with a real
*      constraint (e.g. `esp_async_wifi_channel`'s fixed-size async ring buffer)
*      keeps itself single-packet by simply not providing `do_send`/`do_try_receive`
*      for any other `Packet` -- the restriction lives in `Impl`, not in this base.
*      Removed the `packet_t` alias (there is no single packet type to name anymore)
*      and the `_validator` member (stateless; instantiated locally per call instead).
*/
#ifndef ECOMM_CHANNELS_CHANNEL_HPP_
#define ECOMM_CHANNELS_CHANNEL_HPP_

#include <cstddef>
#include <optional>

#include "../protocol/validator.hpp"
#include "send_result.hpp"

namespace ecomm::channels {

    /**
    * @class channel
    *
    * @brief CRTP base for a validated two-way communication channel.
    *
    * `Impl` must provide, for every `Packet` it wishes to support:
    * ```cpp
    * void do_send(const Packet& packet) noexcept;
    * bool do_try_receive(Packet& packet) noexcept;
    * ```
    * either as ordinary methods (fixed to one `Packet`) or as member templates
    * over `Packet` (supporting any number of packet types through one
    * instance). See the file-level documentation for which shape each
    * concrete channel in this library chooses and why.
    *
    * @tparam Impl Derived class supplying the hardware byte transport.
    */
    template<typename Impl>
    class channel {
    public:
        /**
        * @brief Send a packet.
        *
        * Seals the packet (computes and writes the FCS if the checksum policy
        * is not `none`) then delegates the raw byte write to `Impl::do_send`.
        * Always returns `send_result::ok`; the unreliable channel makes no
        * delivery guarantee.
        *
        * @tparam Packet Deduced from `packet`'s type -- no explicit template
        *                argument is needed.
        * @param[in,out] packet Packet to send. The FCS field may be modified by
        *                       `validator::seal`; all other fields are preserved.
        *
        * @return `send_result::ok` unconditionally.
        *
        * @note The caller must not assume the packet is bitwise identical after
        *       this call returns (the FCS field is overwritten by `seal`).
        */
        template<typename Packet>
        [[nodiscard]] send_result send(Packet& packet) noexcept;

        /**
        * @brief Attempt to receive a packet.
        *
        * Delegates to `Impl::do_try_receive`. If a complete packet was read, it
        * passes `validator::is_valid`, and (for network-topology packets) its
        * `receiver_id` is either `ECOMM_BOARD_ID` or `0xFF` (broadcast), the
        * packet is returned in an engaged `std::optional`. Returns `std::nullopt`
        * if nothing is available, the packet is corrupt, or it is addressed to
        * a different node.
        *
        * @tparam Packet Must be named explicitly -- there is no argument to
        *                deduce a return type from, a hard rule of C++ template
        *                argument deduction (not a design choice this class
        *                could avoid).
        * @return An engaged `std::optional<Packet>` holding the validated packet,
        *         or `std::nullopt` otherwise.
        */
        template<typename Packet>
        [[nodiscard]] std::optional<Packet> try_receive() noexcept;
    };

} // namespace ecomm::channels

#include "channel.tpp"
#endif // ECOMM_CHANNELS_CHANNEL_HPP_
