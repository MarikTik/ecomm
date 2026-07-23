// SPDX-License-Identifier: MIT
/**
* @file arduino_serial_channel.tpp
*
* @brief Implementation of arduino_serial_channel<tag>.
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
* - 2026-07-16 do_send/do_try_receive became member templates over Packet,
*      matching the class-level Packet parameter's removal.
* - 2026-07-21 Added do_receive_raw (backs channel::receive_raw): reads
*      min(max, available()) bytes, non-blocking.
*/
#ifndef ECOMM_ARDUINO_SERIAL_CHANNEL_TPP_
#define ECOMM_ARDUINO_SERIAL_CHANNEL_TPP_
#ifdef ARDUINO

#include "arduino_serial_channel.hpp"

namespace ecomm::channels {

    template<std::uint8_t tag>
    arduino_serial_channel<tag>::arduino_serial_channel(
        HardwareSerial& serial
    ) noexcept
        : _serial{serial}
    {}

    template<std::uint8_t tag>
    template<typename Packet>
    bool arduino_serial_channel<tag>::do_try_receive(Packet& out) noexcept {
        static_assert(std::is_trivially_copyable_v<Packet>,
                      "Packet must be trivially copyable");
        if (static_cast<std::size_t>(_serial.available()) < sizeof(Packet))
            return false;
        _serial.readBytes(reinterpret_cast<std::uint8_t*>(&out), sizeof(Packet));
        return true;
    }

    template<std::uint8_t tag>
    template<typename Packet>
    void arduino_serial_channel<tag>::do_send(const Packet& packet) noexcept {
        static_assert(std::is_trivially_copyable_v<Packet>,
                      "Packet must be trivially copyable");
        _serial.write(reinterpret_cast<const std::uint8_t*>(&packet), sizeof(Packet));
    }

    template<std::uint8_t tag>
    std::size_t arduino_serial_channel<tag>::do_receive_raw(std::byte* dst, std::size_t max) noexcept {
        const std::size_t avail = static_cast<std::size_t>(_serial.available());
        const std::size_t n = avail < max ? avail : max;
        if (n == 0) return 0;
        _serial.readBytes(reinterpret_cast<std::uint8_t*>(dst), n);
        return n;
    }

} // namespace ecomm::channels

#endif // ARDUINO
#endif // ECOMM_ARDUINO_SERIAL_CHANNEL_TPP_
