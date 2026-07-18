// SPDX-License-Identifier: MIT
/**
* @file arduino_serial_channel.hpp
*
* @brief Serial channel for packet-based messaging on Arduino platforms.
*
* @ingroup ecomm_channels ecomm::channels
*
* Provides `arduino_serial_channel<tag>`, a concrete `channel<>` for UART
* communication via Arduino's `HardwareSerial`. The channel reads and writes
* packets as raw binary blobs; the packet type is a template parameter of
* `send`/`try_receive` themselves (inherited from `channel<>`), not of this
* class, so one instance can carry as many distinct packet types as the
* caller needs over the same physical UART. Validation and sealing are
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
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-26 Renamed from arduino_serial_interface; Packet baked into type;
*              `delegate_` -> `do_`.
* - 2026-07-16 Dropped the class-level `Packet` parameter -- `do_send`/
*      `do_try_receive` are now member templates over `Packet`, matching
*      `channel<Impl>`'s per-call packet type. This channel has no internal
*      state sized to a packet (it is a pure byte passthrough), so nothing
*      about it required fixing one packet type in the first place.
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
    * Wraps a `HardwareSerial` instance and exposes typed `send<Packet>`/
    * `try_receive<Packet>` via the `channel<>` base, for any `Packet` type the
    * caller names -- this class holds no per-packet state. Use the `tag`
    * parameter to distinguish between multiple serial ports (e.g. `Serial`,
    * `Serial1`, `Serial2`).
    *
    * @tparam tag Compile-time tag to distinguish multiple instances.
    *
    * @warning Using two instances with the same `tag` and the same serial
    *          port leads to undefined behavior.
    */
    template<std::uint8_t tag = 0>
    class arduino_serial_channel
        : public channel<arduino_serial_channel<tag>>
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
        friend class channel<arduino_serial_channel<tag>>;

        /**
        * @brief Read one packet's worth of bytes from the serial port.
        *
        * Called by `channel::try_receive`. Returns `false` immediately if
        * fewer than `sizeof(Packet)` bytes are available; otherwise reads
        * exactly `sizeof(Packet)` bytes into `out`.
        *
        * @tparam Packet Deduced from `out`'s type.
        * @param[out] out Destination packet buffer.
        * @return `true` if a complete packet was read, `false` otherwise.
        */
        template<typename Packet>
        bool do_try_receive(Packet& out) noexcept;

        /**
        * @brief Write one packet's worth of bytes to the serial port.
        *
        * Called by `channel::send` after sealing. Writes exactly
        * `sizeof(Packet)` bytes.
        *
        * @tparam Packet Deduced from `packet`'s type.
        * @param[in] packet Packet to transmit.
        */
        template<typename Packet>
        void do_send(const Packet& packet) noexcept;

        HardwareSerial& _serial; ///< Bound hardware serial port.
    };

} // namespace ecomm::channels

#include "arduino_serial_channel.tpp"
#endif // ARDUINO
#endif // ECOMM_ARDUINO_SERIAL_CHANNEL_HPP_
