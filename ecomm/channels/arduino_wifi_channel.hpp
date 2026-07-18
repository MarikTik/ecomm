// SPDX-License-Identifier: MIT
/**
* @file arduino_wifi_channel.hpp
*
* @brief Synchronous Wi-Fi channel for packet-based messaging on Arduino platforms.
*
* @ingroup ecomm_channels ecomm::channels
*
* Provides `arduino_wifi_channel<tag>`, a concrete `channel<>` for TCP
* communication via Arduino's `WiFiServer`/`WiFiClient`. The channel reads and
* writes packets as raw binary blobs over an active TCP connection; the
* packet type is a template parameter of `send`/`try_receive` themselves
* (inherited from `channel<>`), not of this class, so one instance can carry
* as many distinct packet types as the caller needs over the same connection.
* Validation and sealing are handled transparently by `channel<>`.
*
* Because TCP provides delivery and integrity guarantees, the recommended
* packet configuration for this channel uses `ChecksumPolicy = none`:
* ```cpp
* using wifi_packet = ecomm::protocol::packet<32, topology::network, none>;
* arduino_wifi_channel<> ch{server};
* wifi_packet p{...};
* (void)ch.send(p);
* ```
*
* Only one active `WiFiClient` is tracked at a time.
*
* @note Only compiled when `<WiFi.h>` is available on the target. On ESP32
*       or ESP8266 prefer `esp_async_wifi_channel` instead  --  the synchronous
*       `WiFiServer` API blocks the main loop waiting for clients and bytes.
*
* @see channel.hpp
* @see esp_async_wifi_channel.hpp
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
* - 2026-05-26 Renamed from arduino_wifi_interface; Packet baked into type;
*              `delegate_` -> `do_`.
* - 2026-05-26 Removed ESP8266/ESP32-specific includes; now uses __has_include(<WiFi.h>)
*              only. ESP targets should use esp_async_wifi_channel instead.
* - 2026-07-16 Dropped the class-level `Packet` parameter -- `do_send`/
*      `do_try_receive` are now member templates over `Packet`, matching
*      `channel<Impl>`'s per-call packet type. This channel has no internal
*      state sized to a packet, so nothing about it required fixing one
*      packet type in the first place.
*/
#ifndef ECOMM_ARDUINO_WIFI_CHANNEL_HPP_
#define ECOMM_ARDUINO_WIFI_CHANNEL_HPP_

#if __has_include(<WiFi.h>)
    #include <WiFi.h>
#else
    #define ECOMM_NO_ARDUINO_WIFI_SUPPORT
    #pragma message "No <WiFi.h> found. arduino_wifi_channel will not be compiled."
#endif

#ifndef ECOMM_NO_ARDUINO_WIFI_SUPPORT

#include <cstdint>
#include <type_traits>

#include "channel.hpp"

namespace ecomm::channels {

    /**
    * @class arduino_wifi_channel
    *
    * @brief Synchronous Wi-Fi TCP channel for Arduino boards with `<WiFi.h>`.
    *
    * Wraps a `WiFiServer` instance. On the first `do_try_receive` or `do_send`
    * call the channel accepts an incoming `WiFiClient` and reuses it for
    * subsequent operations. Exposes typed `send<Packet>`/`try_receive<Packet>`
    * via the `channel<>` base for any `Packet` type the caller names -- this
    * class holds no per-packet state.
    *
    * @tparam tag Compile-time tag to distinguish multiple Wi-Fi channel instances.
    */
    template<std::uint8_t tag = 0>
    class arduino_wifi_channel
        : public channel<arduino_wifi_channel<tag>>
    {
    public:
        /**
        * @brief Construct a Wi-Fi channel bound to a server instance.
        *
        * @param[in] server Reference to the `WiFiServer` that accepts incoming
        *                   client connections.
        */
        explicit arduino_wifi_channel(WiFiServer& server) noexcept;

    private:
        friend class channel<arduino_wifi_channel<tag>>;

        /**
        * @brief Read one packet's worth of bytes from the active Wi-Fi client.
        *
        * Accepts a new client from the server if none is currently connected.
        * Returns `false` if no client is available or fewer than `sizeof(Packet)`
        * bytes have arrived.
        *
        * @tparam Packet Deduced from `out`'s type.
        * @param[out] out Destination packet buffer.
        * @return `true` if a complete packet was read, `false` otherwise.
        */
        template<typename Packet>
        bool do_try_receive(Packet& out) noexcept;

        /**
        * @brief Write one packet's worth of bytes to the active Wi-Fi client.
        *
        * Accepts a new client if none is connected. Returns without writing if
        * no client is available.
        *
        * @tparam Packet Deduced from `packet`'s type.
        * @param[in] packet Packet to transmit.
        */
        template<typename Packet>
        void do_send(const Packet& packet) noexcept;

        WiFiServer& _server; ///< Server accepting incoming connections.
        WiFiClient  _client; ///< Currently active client (may be unconnected).
    };

} // namespace ecomm::channels

#include "arduino_wifi_channel.tpp"
#endif // ECOMM_NO_ARDUINO_WIFI_SUPPORT
#endif // ECOMM_ARDUINO_WIFI_CHANNEL_HPP_
