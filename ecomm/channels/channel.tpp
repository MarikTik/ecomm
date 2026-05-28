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
        if (!static_cast<Impl*>(this)->do_try_receive(out)) return std::nullopt;
        if (!_validator.is_valid(out))                      return std::nullopt;
        return out;
    }

} // namespace ecomm::channels

#endif // ECOMM_CHANNELS_CHANNEL_TPP_
