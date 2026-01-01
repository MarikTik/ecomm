// SPDX-License-Identifier: BSL-1.1
/**
* @file arduino_wifi_interface.hpp
*
* @brief Defines a wifi communication interface for packet-based task dispatching over Wi-Fi.
*
* @ingroup ecomm_interfaces ecomm::interfaces
*
* This file declares the `wifi_interface` class, a CRTP-based network communication interface
* for microcontrollers supporting the Arduino platform with built-in or external Wi-Fi modules
* (e.g., ESP32, ESP8266). It provides reliable, fixed-size packet transmission and reception
* over a wifi connection using a `WiFiServer`.
*
* The interface supports only a single active wifi client at a time, simplifying session
* management and aligning with the current design of the `etask` library focused on
* one-to-one communication.
*
* It reuses the validation pipeline defined in `validator.hpp` to automatically check and seal
* packets based on the checksum policy associated with the packet type.
*
* @note This interface is conditionally compiled based on platform capabilities.
*       If no supported Wi-Fi library is found, it will not be included.
*
* @see interface.hpp
* @see validator.hpp
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
*
* @par Changelog
* - 2025-07-03 
*      - Initial creation.
* - 2025-07-17
*      - renamed `try_receive` and `send` methods to `delegate_try_receive` and `delegate_send` respectively to
*        enable CRTP delegation via base `interface<>` class.
* - 2025-08-31
*      - renamed interface to `arduino_wifi_interface` as it requires objects from the Arduino
*        framework.
*/
#ifndef ECOMM_ARDUINO_WIFI_INTERFACE_HPP_
#define ECOMM_ARDUINO_WIFI_INTERFACE_HPP_

#if defined(ESP8266)
    #include <ESP8266WiFi.h>
#elif defined(ESP32)
    #include <WiFi.h>
#elif __has_include(<WiFi.h>)
    #include <WiFi.h>
    #warning "Using first available WiFi library, which is not guaranteed to be compatible with standard WiFi server API."
#else 
    #define COMM_NO_ARDUINO_WIFI_SUPPORT
    #pragma message "No Arduino WiFi support available. wifi_interface will not be included."
#endif

#ifndef COMM_NO_ARDUINO_WIFI_SUPPORT
#include <cstdint>
#include <optional>
#include "interface.hpp"

namespace ecomm::interfaces{

    /**
    * @class arduino_wifi_interface
    * @brief wifi-based communication interface for packet transmission over Wi-Fi.
    *
    * The `arduino_wifi_interface` class implements a communication interface using wifi sockets over Wi-Fi.
    * It is designed for use on Arduino-compatible platforms with Wi-Fi capabilities (e.g., ESP32, ESP8266),
    * and follows the CRTP model to allow packet-type-agnostic communication via a shared base class.
    *
    * The interface supports one active wifi client session at a time, simplifying logic
    * and minimizing overhead. Packets are validated on reception and sealed before transmission
    * using the associated `validator<Packet>` policy.
    *
    * @tparam tag A compile-time tag value to distinguish between multiple interface instances.
    *
    * @see interface
    * @see validator
    */
    template<std::uint8_t tag = 0>
    class arduino_wifi_interface : public interface<arduino_wifi_interface<tag>> {
    public:
        arduino_wifi_interface(WiFiServer& server);
        /**
        * @brief Attempts to receive a packet from the wifi server.
        *
        * This method reads data from the wifi server and attempts to construct
        * a packet. If a complete packet is received, it is returned; otherwise,
        * std::nullopt is returned.
        *
        * @tparam Packet The type of packet to attempt receiving.
        *
        * @return An optional packet if successfully received, or std::nullopt if not.
        */
        template<typename Packet>
        [[nodiscard]] inline std::optional<Packet> delegate_try_receive();
    
        /**
        * @brief Sends a packet over the wifi server.
        *
        * This method sends the specified packet over the wifi server.
        *
        * @param packet The reference of the packet to send.
        *
        * @tparam Packet The type of packet to send.
        *
        * @note The caller must not assume that the packet remains bitwise identical after sending.
        */
        template<typename Packet>
        inline void delegate_send(Packet& packet);
    private:
        WiFiServer& _server; ///< Reference to the WiFi server instance
        WiFiClient _client; ///< Active client connection
    };

} // namespace ecomm::interfaces

#include "wifi_interface.tpp" // Implementation of the wifi_interface class
#endif // COMM_NO_ARDUINO_WIFI_SUPPORT
#endif // ECOMM_ARDUINO_WIFI_INTERFACE_HPP_