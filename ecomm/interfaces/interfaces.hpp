// SPDX-License-Identifier: BSL-1.1
/**
* @file interfaces.hpp
*
* @brief Aggregates all communication interface implementations in the `etask` library.
*
* @defgroup ecomm_interfaces ecomm::interfaces
*
* This file serves as a unified inclusion point for all available communication interface headers
* used by the `etask` task dispatching system. Each interface is implemented as a header-only
* class using CRTP (Curiously Recurring Template Pattern) and includes both declarations
* and inline definitions through corresponding `.tpp` files.
*
* The available interfaces include:
* - `serial_interface`: For UART-based communication over Arduino-compatible HardwareSerial.
* - `wifi_interface`: For Wi-Fi-based communication using TCP sockets (e.g., via `WiFiServer`).
*
* Interfaces are conditionally included based on platform capabilities (e.g., Arduino and Wi-Fi support).
* Each interface provides consistent APIs for sending and receiving fixed-size packets,
* and integrates with the `validator` module to enforce packet integrity through checksums or similar mechanisms.
*
* @note Interfaces are templated and designed to support different packet types and configurations
*       without requiring additional boilerplate code.
*
* @see interface.hpp
* @see serial_interface.hpp
* @see wifi_interface.hpp
* @see validator.hpp
*/
#ifndef ECOMM_INTERFACES_HPP_
#define ECOMM_INTERFACES_HPP_
#include "interface.hpp"

#if defined(ARDUINO)
    #include "serial_interface.hpp"
#endif

#if defined(ESP8266) || defined(ESP32) || __has_include(<WiFi.h>)
    #include "wifi_interface.hpp"
#endif

#endif // ECOMM_INTERFACES_HPP_