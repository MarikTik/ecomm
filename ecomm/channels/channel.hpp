// SPDX-License-Identifier: MIT
/**
* @file channel.hpp
*
* @brief CRTP base class for typed two-way communication channels.
*
* @ingroup ecomm_channels ecomm::channels
*
* A `channel<Impl, Packet>` is a self-contained, typed, two-way communication
* endpoint. It owns a fixed packet configuration (`Packet`) end-to-end and
* handles validation transparently on receive and sealing on send.
*
* Derived classes supply the hardware-specific byte transport by implementing
* two methods:
* - `do_send(const Packet&) noexcept`  --  write raw bytes to the physical medium.
* - `do_try_receive(Packet&) noexcept -> bool`  --  read raw bytes from the medium
*   into the supplied packet; return `true` if a complete packet was read.
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
* @par Layering
* ```
* user code
*     |  send(Packet&) / try_receive()
*     v
* channel<Impl, Packet>          <- validates, seals; never allocates
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
    * @brief CRTP base for a typed, validated two-way communication channel.
    *
    * `Impl` must provide:
    * ```cpp
    * void do_send(const Packet& packet) noexcept;
    * bool do_try_receive(Packet& packet) noexcept;
    * ```
    *
    * @tparam Impl   Derived class supplying the hardware byte transport.
    * @tparam Packet Fixed packet type this channel operates on.
    */
    template<typename Impl, typename Packet>
    class channel {
    public:
        /// @brief The fixed packet type this channel operates on. Lets generic
        ///        code (e.g. `ecomm::hub`) verify at compile time that several
        ///        channels share a packet type, without needing `Impl`.
        using packet_t = Packet;

        /**
        * @brief Send a packet.
        *
        * Seals the packet (computes and writes the FCS if the checksum policy
        * is not `none`) then delegates the raw byte write to `Impl::do_send`.
        * Always returns `send_result::ok`; the unreliable channel makes no
        * delivery guarantee.
        *
        * @param[in,out] packet Packet to send. The FCS field may be modified by
        *                       `validator::seal`; all other fields are preserved.
        *
        * @return `send_result::ok` unconditionally.
        *
        * @note The caller must not assume the packet is bitwise identical after
        *       this call returns (the FCS field is overwritten by `seal`).
        */
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
        * @return An engaged `std::optional<Packet>` holding the validated packet,
        *         or `std::nullopt` otherwise.
        */
        [[nodiscard]] std::optional<Packet> try_receive() noexcept;

    private:
        protocol::validator<Packet> _validator{};
    };

} // namespace ecomm::channels

#include "channel.tpp"
#endif // ECOMM_CHANNELS_CHANNEL_HPP_
