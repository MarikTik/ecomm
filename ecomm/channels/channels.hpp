// SPDX-License-Identifier: MIT
/**
* @file channels.hpp
*
* @brief Aggregates all channel implementations in the ecomm library.
*
* @defgroup ecomm_channels ecomm::channels
*
* A channel is a self-contained, two-way communication endpoint. The packet
* type is a template parameter of `send`/`try_receive` themselves, not of the
* channel -- most concrete channels here can carry several distinct `packet<>`
* configurations through one instance (see `channel.hpp`); `reliable_channel`
* is the one exception, fixing its own packet type per instance (see its own
* documentation for why). Validation (on receive) and sealing (on send) are
* handled transparently.
*
* Concrete channels are conditionally compiled based on platform capabilities:
* - `arduino_serial_channel`     --  UART via `HardwareSerial` (requires `ARDUINO`).
* - `arduino_wifi_channel`       --  Synchronous TCP via `WiFiServer` (requires a
*                                 board with `<WiFi.h>`; suitable for non-ESP or
*                                 low-throughput use cases).
* - `esp_async_wifi_channel`     --  Non-blocking TCP via AsyncTCP / ESPAsyncTCP
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
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-26 Renamed from interfaces/interfaces.hpp.
* - 2026-05-26 Added esp_async_wifi_channel for ESP32 / ESP8266.
* - 2026-05-27 Added send_result.hpp.
* - 2026-05-28 Added reliable_channel.hpp.
*/
#ifndef ECOMM_CHANNELS_HPP_
#define ECOMM_CHANNELS_HPP_

#include "send_result.hpp"
#include "channel.hpp"
#include "reliable_channel.hpp"

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
