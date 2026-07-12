// SPDX-License-Identifier: MIT
/**
* @file arduino_serial_channel.tpp
*
* @brief Implementation of arduino_serial_channel<Packet, tag>.
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
* - 2026-05-26 Renamed from arduino_serial_interface.tpp; `delegate_` -> `do_`;
*              fixed missing local variable in do_try_receive.
*/
#ifndef ECOMM_ARDUINO_SERIAL_CHANNEL_TPP_
#define ECOMM_ARDUINO_SERIAL_CHANNEL_TPP_
#ifdef ARDUINO

#include "arduino_serial_channel.hpp"

namespace ecomm::channels {

    template<typename Packet, std::uint8_t tag>
    arduino_serial_channel<Packet, tag>::arduino_serial_channel(
        HardwareSerial& serial
    ) noexcept
        : _serial{serial}
    {}

    template<typename Packet, std::uint8_t tag>
    bool arduino_serial_channel<Packet, tag>::do_try_receive(Packet& out) noexcept {
        static_assert(std::is_trivially_copyable_v<Packet>,
                      "Packet must be trivially copyable");
        if (static_cast<std::size_t>(_serial.available()) < sizeof(Packet))
            return false;
        _serial.readBytes(reinterpret_cast<std::uint8_t*>(&out), sizeof(Packet));
        return true;
    }

    template<typename Packet, std::uint8_t tag>
    void arduino_serial_channel<Packet, tag>::do_send(const Packet& packet) noexcept {
        static_assert(std::is_trivially_copyable_v<Packet>,
                      "Packet must be trivially copyable");
        _serial.write(reinterpret_cast<const std::uint8_t*>(&packet), sizeof(Packet));
    }

} // namespace ecomm::channels

#endif // ARDUINO
#endif // ECOMM_ARDUINO_SERIAL_CHANNEL_TPP_
