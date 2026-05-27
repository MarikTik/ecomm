// SPDX-License-Identifier: BSL-1.1
/**
* @file arduino_wifi_channel.hpp
*
* @brief Typed Wi-Fi channel for packet-based messaging on Arduino platforms.
*
* @ingroup ecomm_channels ecomm::channels
*
* Provides `arduino_wifi_channel<Packet, tag>`, a concrete `channel<>` for
* TCP communication via Arduino's `WiFiServer`/`WiFiClient`. The channel reads
* and writes fixed-size packets as raw binary blobs over an active TCP
* connection. Validation and sealing are handled transparently by `channel<>`.
*
* Because TCP provides delivery and integrity guarantees, the recommended
* packet configuration for this channel uses `ChecksumPolicy = none`:
* ```cpp
* using wifi_packet = ecomm::protocol::packet<32, topology::network, none>;
* arduino_wifi_channel<wifi_packet> ch{server};
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
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2026 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-26 Renamed from arduino_wifi_interface; Packet baked into type;
*              `delegate_` -> `do_`.
* - 2026-05-26 Removed ESP8266/ESP32-specific includes; now uses __has_include(<WiFi.h>)
*              only. ESP targets should use esp_async_wifi_channel instead.
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
    * subsequent operations.
    *
    * @tparam Packet Packet type this channel operates on.
    * @tparam tag    Compile-time tag to distinguish multiple Wi-Fi channel instances.
    */
    template<typename Packet, std::uint8_t tag = 0>
    class arduino_wifi_channel
        : public channel<arduino_wifi_channel<Packet, tag>, Packet>
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
        friend class channel<arduino_wifi_channel<Packet, tag>, Packet>;

        /**
        * @brief Read one packet's worth of bytes from the active Wi-Fi client.
        *
        * Accepts a new client from the server if none is currently connected.
        * Returns `false` if no client is available or fewer than `sizeof(Packet)`
        * bytes have arrived.
        *
        * @param[out] out Destination packet buffer.
        * @return `true` if a complete packet was read, `false` otherwise.
        */
        bool do_try_receive(Packet& out) noexcept;

        /**
        * @brief Write one packet's worth of bytes to the active Wi-Fi client.
        *
        * Accepts a new client if none is connected. Returns without writing if
        * no client is available.
        *
        * @param[in] packet Packet to transmit.
        */
        void do_send(const Packet& packet) noexcept;

        WiFiServer& _server; ///< Server accepting incoming connections.
        WiFiClient  _client; ///< Currently active client (may be unconnected).
    };

} // namespace ecomm::channels

#include "arduino_wifi_channel.tpp"
#endif // ECOMM_NO_ARDUINO_WIFI_SUPPORT
#endif // ECOMM_ARDUINO_WIFI_CHANNEL_HPP_
