// SPDX-License-Identifier: BSL-1.1
/**
* @file channels.hpp
*
* @brief Aggregates all channel implementations in the ecomm library.
*
* @defgroup ecomm_channels ecomm::channels
*
* A channel is a self-contained, typed, two-way communication endpoint.
* Each channel is bound to a single `packet<>` configuration and handles
* validation (on receive) and sealing (on send) transparently.
*
* Concrete channels are conditionally compiled based on platform capabilities:
* - `arduino_serial_channel`    — UART via `HardwareSerial` (requires `ARDUINO`).
* - `arduino_wifi_channel`      — Synchronous TCP via `WiFiServer` (requires a
*                                 board with `<WiFi.h>`; suitable for non-ESP or
*                                 low-throughput use cases).
* - `esp_async_wifi_channel`    — Non-blocking TCP via AsyncTCP / ESPAsyncTCP
*                                 (requires ESP32 or ESP8266). Preferred over
*                                 `arduino_wifi_channel` on ESP targets.
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
* - 2026-05-26 Renamed from interfaces/interfaces.hpp.
* - 2026-05-26 Added esp_async_wifi_channel for ESP32 / ESP8266.
*/
#ifndef ECOMM_CHANNELS_HPP_
#define ECOMM_CHANNELS_HPP_

#include "channel.hpp"

#if defined(ARDUINO)
    #include "arduino_serial_channel.hpp"
#endif

#if defined(ESP8266) || defined(ESP32) || __has_include(<WiFi.h>)
    #include "arduino_wifi_channel.hpp"
#endif

#if defined(ESP32) || defined(ESP8266)
    #include "esp_async_wifi_channel.hpp"
#endif

#endif // ECOMM_CHANNELS_HPP_
