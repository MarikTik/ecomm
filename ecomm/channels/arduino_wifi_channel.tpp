// SPDX-License-Identifier: BSL-1.1
/**
* @file arduino_wifi_channel.tpp
*
* @brief Implementation of arduino_wifi_channel<Packet, tag>.
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
* - 2026-05-26 Renamed from arduino_wifi_interface.tpp; `delegate_` -> `do_`;
*              fixed missing local variable in do_try_receive.
*/
#ifndef ECOMM_ARDUINO_WIFI_CHANNEL_TPP_
#define ECOMM_ARDUINO_WIFI_CHANNEL_TPP_
#ifndef ECOMM_NO_ARDUINO_WIFI_SUPPORT

#include "arduino_wifi_channel.hpp"

namespace ecomm::channels {

    template<typename Packet, std::uint8_t tag>
    arduino_wifi_channel<Packet, tag>::arduino_wifi_channel(
        WiFiServer& server
    ) noexcept
        : _server{server}
    {}

    template<typename Packet, std::uint8_t tag>
    bool arduino_wifi_channel<Packet, tag>::do_try_receive(Packet& out) noexcept {
        static_assert(std::is_trivially_copyable_v<Packet>,
                      "Packet must be trivially copyable");
        if (not _client) {
            _client = _server.available();
            if (not _client) return false;
        }
        if (static_cast<std::size_t>(_client.available()) < sizeof(Packet))
            return false;
        _client.read(reinterpret_cast<std::uint8_t*>(&out), sizeof(Packet));
        return true;
    }

    template<typename Packet, std::uint8_t tag>
    void arduino_wifi_channel<Packet, tag>::do_send(const Packet& packet) noexcept {
        static_assert(std::is_trivially_copyable_v<Packet>,
                      "Packet must be trivially copyable");
        if (not _client) {
            _client = _server.available();
            if (not _client) return;
        }
        _client.write(reinterpret_cast<const std::uint8_t*>(&packet), sizeof(Packet));
    }

} // namespace ecomm::channels

#endif // ECOMM_NO_ARDUINO_WIFI_SUPPORT
#endif // ECOMM_ARDUINO_WIFI_CHANNEL_TPP_
