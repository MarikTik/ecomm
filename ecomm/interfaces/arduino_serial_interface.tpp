// SPDX-License-Identifier: BSL-1.1
/**
* @file serial_interface.tpp
*
* @brief implementation of serial_interface.tpp methods.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2025-07-03
*
* @copyright
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2025 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
* @par Changelog
* - 2025-07-03 
*      - Initial creation.
* - 2025-07-17
*      - Renamed `try_receive` and `send` methods to `delegate_try_receive` and `delegate_send` respectively to
*        enable CRTP delegation via base `interface<>` class.
* - 2024-08-12
*      - Moved validation details to `interface` base.
* - 2025-08-31
*      - renamed interface to `arduino_serial_interface` as it requires `Serial` objects from the Arduino
*        framework.
*/
#ifndef ECOMM_ARDUINO_SERIAL_INTERFACE_TPP_
#define ECOMM_ARDUINO_SERIAL_INTERFACE_TPP_
#ifdef ARDUINO
namespace ecomm::interfaces {

    template<uint8_t tag>
    arduino_serial_interface<tag>::arduino_serial_interface(HardwareSerial &serial)
        : _serial{serial}
    {
    }
    template<uint8_t tag>
    template<typename Packet>
    inline std::optional<Packet> arduino_serial_interface<tag>::delegate_try_receive() {
        static_assert(std::is_trivially_copyable_v<Packet>, "Packet must be trivially copyable");
        constexpr std::size_t packet_size = sizeof(Packet);
        if (_serial.available() < packet_size) return std::nullopt;

        _serial.readBytes(
            reinterpret_cast<std::uint8_t *>(&packet),
            packet_size
        );
        
        return packet;
    }

    template<uint8_t tag>
    template<typename Packet>
    inline void arduino_serial_interface<tag>::delegate_send(Packet &packet) {
        static_assert(std::is_trivially_copyable_v<Packet>, "Packet must be trivially copyable");
        _serial.write(reinterpret_cast<uint8_t*>(&packet), sizeof(Packet));
    }

} // namespace ecomm::interfaces

#endif // ARDUINO
#endif // ECOMM_ARDUINO_SERIAL_INTERFACE_TPP_