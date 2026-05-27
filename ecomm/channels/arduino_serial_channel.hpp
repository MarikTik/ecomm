// SPDX-License-Identifier: BSL-1.1
/**
* @file arduino_serial_channel.hpp
*
* @brief Typed serial channel for packet-based messaging on Arduino platforms.
*
* @ingroup ecomm_channels ecomm::channels
*
* Provides `arduino_serial_channel<Packet, tag>`, a concrete `channel<>` for
* UART communication via Arduino's `HardwareSerial`. The channel reads and
* writes fixed-size packets as raw binary blobs. Validation and sealing are
* handled transparently by the `channel<>` base.
*
* @note Only compiled when the `ARDUINO` macro is defined.
*
* @see channel.hpp
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
* - 2026-05-26 Renamed from arduino_serial_interface; Packet baked into type;
*              `delegate_` -> `do_`.
*/
#ifndef ECOMM_ARDUINO_SERIAL_CHANNEL_HPP_
#define ECOMM_ARDUINO_SERIAL_CHANNEL_HPP_
#ifdef ARDUINO

#include <HardwareSerial.h>
#include <cstdint>
#include <type_traits>

#include "channel.hpp"

namespace ecomm::channels {

    /**
    * @class arduino_serial_channel
    *
    * @brief Serial UART channel for Arduino platforms.
    *
    * Wraps a `HardwareSerial` instance and exposes typed `send`/`try_receive`
    * via the `channel<>` base. Use the `tag` parameter to distinguish between
    * multiple serial ports (e.g. `Serial`, `Serial1`, `Serial2`).
    *
    * @tparam Packet Packet type this channel operates on.
    * @tparam tag    Compile-time tag to distinguish multiple instances.
    *
    * @warning Using two instances with the same `tag` and the same serial
    *          port leads to undefined behavior.
    */
    template<typename Packet, std::uint8_t tag = 0>
    class arduino_serial_channel
        : public channel<arduino_serial_channel<Packet, tag>, Packet>
    {
    public:
        /**
        * @brief Construct a serial channel bound to a hardware serial port.
        *
        * @param[in] serial Reference to the `HardwareSerial` object
        *                   (`Serial`, `Serial1`, ...) to use for communication.
        */
        explicit arduino_serial_channel(HardwareSerial& serial) noexcept;

    private:
        friend class channel<arduino_serial_channel<Packet, tag>, Packet>;

        /**
        * @brief Read one packet's worth of bytes from the serial port.
        *
        * Called by `channel::try_receive`. Returns `false` immediately if
        * fewer than `sizeof(Packet)` bytes are available; otherwise reads
        * exactly `sizeof(Packet)` bytes into `out`.
        *
        * @param[out] out Destination packet buffer.
        * @return `true` if a complete packet was read, `false` otherwise.
        */
        bool do_try_receive(Packet& out) noexcept;

        /**
        * @brief Write one packet's worth of bytes to the serial port.
        *
        * Called by `channel::send` after sealing. Writes exactly
        * `sizeof(Packet)` bytes.
        *
        * @param[in] packet Packet to transmit.
        */
        void do_send(const Packet& packet) noexcept;

        HardwareSerial& _serial; ///< Bound hardware serial port.
    };

} // namespace ecomm::channels

#include "arduino_serial_channel.tpp"
#endif // ARDUINO
#endif // ECOMM_ARDUINO_SERIAL_CHANNEL_HPP_
