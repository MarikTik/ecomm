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
* - 2026-07-21 Added do_receive_raw (backs channel::receive_raw): accepts a
*      client if needed, then reads min(max, available()) bytes, non-blocking.
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

    template<std::uint8_t tag>
    std::size_t arduino_wifi_channel<tag>::do_receive_raw(std::byte* dst, std::size_t max) noexcept {
        if (not _client) {
            _client = _server.available();
            if (not _client) return 0;
        }
        const std::size_t avail = static_cast<std::size_t>(_client.available());
        const std::size_t n = avail < max ? avail : max;
        if (n == 0) return 0;
        _client.read(reinterpret_cast<std::uint8_t*>(dst), n);
        return n;
    }

} // namespace ecomm::channels

#endif // ECOMM_NO_ARDUINO_WIFI_SUPPORT
#endif // ECOMM_ARDUINO_WIFI_CHANNEL_TPP_
