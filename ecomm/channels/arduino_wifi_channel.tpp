// SPDX-License-Identifier: MIT
/**
* @file arduino_wifi_channel.tpp
*
* @brief Implementation of arduino_wifi_channel<tag>.
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
* - 2026-05-26 Renamed from arduino_wifi_interface.tpp; `delegate_` -> `do_`;
*              fixed missing local variable in do_try_receive.
* - 2026-07-16 do_send/do_try_receive became member templates over Packet,
*      matching the class-level Packet parameter's removal.
*/
#ifndef ECOMM_ARDUINO_WIFI_CHANNEL_TPP_
#define ECOMM_ARDUINO_WIFI_CHANNEL_TPP_
#ifndef ECOMM_NO_ARDUINO_WIFI_SUPPORT

#include "arduino_wifi_channel.hpp"

namespace ecomm::channels {

    template<std::uint8_t tag>
    arduino_wifi_channel<tag>::arduino_wifi_channel(
        WiFiServer& server
    ) noexcept
        : _server{server}
    {}

    template<std::uint8_t tag>
    template<typename Packet>
    bool arduino_wifi_channel<tag>::do_try_receive(Packet& out) noexcept {
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

    template<std::uint8_t tag>
    template<typename Packet>
    void arduino_wifi_channel<tag>::do_send(const Packet& packet) noexcept {
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
