// SPDX-License-Identifier: MIT
/**
* @file channel.tpp
*
* @brief Implementation of channel<Impl>::send<Packet>/try_receive<Packet>.
*
* @ingroup ecomm_channels ecomm::channels
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
* - 2026-05-26 Renamed from interfaces/interface.tpp; removed ETASK_BOARD_ID
*              filtering; Packet baked into type; `delegate_` -> `do_`.
* - 2026-05-27 send() now returns send_result::ok (was void).
*              try_receive() now returns std::optional<Packet> (was bool + out-param).
* - 2026-05-28 try_receive() filters on receiver_id for network-topology packets:
*              accepts ECOMM_BOARD_ID (unicast) and 255 (broadcast); drops others.
* - 2026-07-16 Packet moved from a class template parameter to a per-method one;
*              `_validator` member replaced by a local, stateless `validator<Packet>{}`
*              instantiated fresh in each call (validator has no state to persist).
* - 2026-07-21 Added `receive_raw(std::byte*, std::size_t)`, delegating to
*              `Impl::do_receive_raw`. A raw, unframed byte read that lets callers
*              do their own framing above the transport (used by `ecomm::router` to
*              reassemble partially-arrived packets instead of misframing them).
*/
#ifndef ECOMM_CHANNELS_CHANNEL_TPP_
#define ECOMM_CHANNELS_CHANNEL_TPP_

#include "channel.hpp"

namespace ecomm::channels {

    template<typename Impl>
    template<typename Packet>
    send_result channel<Impl>::send(Packet& packet) noexcept {
        protocol::validator<Packet>{}.seal(packet);
        static_cast<Impl*>(this)->do_send(packet);
        return send_result::ok;
    }

    template<typename Impl>
    template<typename Packet>
    std::optional<Packet> channel<Impl>::try_receive() noexcept {
        Packet out{};
        if (not static_cast<Impl*>(this)->do_try_receive(out)) return std::nullopt;
        if (not protocol::validator<Packet>{}.is_valid(out))   return std::nullopt;

        if constexpr (Packet::header_t::has_node_ids) {
            constexpr auto broadcast = static_cast<std::uint8_t>(0xFFu);
            const auto dest          = out.header.receiver_id;
            if (dest not_eq static_cast<std::uint8_t>(ECOMM_BOARD_ID) and
                dest not_eq broadcast)
            {
                return std::nullopt;
            }
        }

        return out;
    }

    template<typename Impl>
    std::size_t channel<Impl>::receive_raw(std::byte* dst, std::size_t max) noexcept {
        return static_cast<Impl*>(this)->do_receive_raw(dst, max);
    }

} // namespace ecomm::channels

#endif // ECOMM_CHANNELS_CHANNEL_TPP_
