// SPDX-License-Identifier: BSL-1.1
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
* - `do_send(const Packet&) noexcept` — write raw bytes to the physical medium.
* - `do_try_receive(Packet&) noexcept → bool` — read raw bytes from the medium
*   into the supplied packet; return `true` if a complete packet was read.
*
* The base `channel` then composes validation (`validator<Packet>`) around those
* primitives so that callers always work with structurally valid packets.
*
* @par Layering
* ```
* user code
*     │  send(Packet&) / try_receive(Packet&)
*     ▼
* channel<Impl, Packet>          ← validates, seals; never allocates
*     │  do_send / do_try_receive
*     ▼
* Impl (e.g. arduino_serial_channel)   ← raw bytes to/from hardware
*     ▼
* hardware
* ```
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
* - 2026-05-26 Renamed from interfaces/interface.hpp; Packet baked into the
*              channel type; `delegate_` prefix replaced with `do_`.
*/
#ifndef ECOMM_CHANNELS_CHANNEL_HPP_
#define ECOMM_CHANNELS_CHANNEL_HPP_

#include <cstddef>

#include "../protocol/validator.hpp"

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
        /**
        * @brief Send a packet.
        *
        * Seals the packet (computes and writes the FCS if the checksum policy
        * is not `none`) then delegates the raw byte write to `Impl::do_send`.
        *
        * @param[in,out] packet Packet to send. The FCS field may be modified by
        *                       `validator::seal`; all other fields are preserved.
        *
        * @note The caller must not assume the packet is bitwise identical after
        *       this call returns.
        */
        void send(Packet& packet) noexcept;

        /**
        * @brief Attempt to receive a packet.
        *
        * Delegates to `Impl::do_try_receive`. If a complete packet was read and
        * it passes `validator::is_valid`, writes it into `out` and returns `true`.
        * Returns `false` if no complete packet is available or the packet is corrupt.
        *
        * @param[out] out Destination for the received packet. Only written on
        *                 success (`true` return).
        *
        * @return `true`  — a valid packet was written into `out`.
        * @return `false` — nothing available or packet failed validation.
        */
        [[nodiscard]] bool try_receive(Packet& out) noexcept;

    private:
        protocol::validator<Packet> _validator{};
    };

} // namespace ecomm::channels

#include "channel.tpp"
#endif // ECOMM_CHANNELS_CHANNEL_HPP_
