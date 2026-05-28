// SPDX-License-Identifier: BSL-1.1
/**
* @file channel.tpp
*
* @brief Implementation of channel<Impl, Packet> send/try_receive.
*
* @ingroup ecomm_channels ecomm::channels
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
* - 2026-05-26 Renamed from interfaces/interface.tpp; removed ETASK_BOARD_ID
*              filtering; Packet baked into type; `delegate_` -> `do_`.
* - 2026-05-27 send() now returns send_result::ok (was void).
*              try_receive() now returns std::optional<Packet> (was bool + out-param).
* - 2026-05-28 try_receive() filters on receiver_id for network-topology packets:
*              accepts ECOMM_BOARD_ID (unicast) and 255 (broadcast); drops others.
*/
#ifndef ECOMM_CHANNELS_CHANNEL_TPP_
#define ECOMM_CHANNELS_CHANNEL_TPP_

#include "channel.hpp"

namespace ecomm::channels {

    template<typename Impl, typename Packet>
    send_result channel<Impl, Packet>::send(Packet& packet) noexcept {
        _validator.seal(packet);
        static_cast<Impl*>(this)->do_send(packet);
        return send_result::ok;
    }

    template<typename Impl, typename Packet>
    std::optional<Packet> channel<Impl, Packet>::try_receive() noexcept {
        Packet out{};
        if (not static_cast<Impl*>(this)->do_try_receive(out)) return std::nullopt;
        if (not _validator.is_valid(out))                      return std::nullopt;

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

} // namespace ecomm::channels

#endif // ECOMM_CHANNELS_CHANNEL_TPP_
