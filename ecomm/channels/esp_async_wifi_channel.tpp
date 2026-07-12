// SPDX-License-Identifier: MIT
/**
* @file esp_async_wifi_channel.tpp
*
* @brief Implementation of esp_async_wifi_channel<Packet, QueueDepth>.
*
* @ingroup ecomm_channels ecomm::channels
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
* - 2026-05-26 Initial creation.
*/
#ifndef ECOMM_CHANNELS_ESP_ASYNC_WIFI_CHANNEL_TPP_
#define ECOMM_CHANNELS_ESP_ASYNC_WIFI_CHANNEL_TPP_
#ifndef ECOMM_NO_ESP_ASYNC_WIFI_SUPPORT

#include "esp_async_wifi_channel.hpp"

namespace ecomm::channels {

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------

    template<typename Packet, std::size_t QueueDepth>
    esp_async_wifi_channel<Packet, QueueDepth>::esp_async_wifi_channel(
        AsyncServer& server
    ) noexcept
        : _server{server}
    {
        _server.onClient(
            [](void* arg, AsyncClient* client) {
                static_cast<esp_async_wifi_channel*>(arg)->on_client(client);
            },
            this
        );
    }

    // -------------------------------------------------------------------------
    // channel<> CRTP contract
    // -------------------------------------------------------------------------

    template<typename Packet, std::size_t QueueDepth>
    void esp_async_wifi_channel<Packet, QueueDepth>::do_send(
        const Packet& packet
    ) noexcept {
        if (not _client) return;
        _client->write(
            reinterpret_cast<const char*>(&packet),
            sizeof(Packet)
        );
    }

    template<typename Packet, std::size_t QueueDepth>
    bool esp_async_wifi_channel<Packet, QueueDepth>::do_try_receive(
        Packet& out
    ) noexcept {
        _cs.enter();
        const bool empty = queue_empty();
        _cs.exit();

        if (empty) return false;

        std::memcpy(&out, &_slots[_tail], sizeof(Packet));

        _cs.enter();
        _tail = (_tail + 1) % QueueDepth;
        _cs.exit();

        return true;
    }

    // -------------------------------------------------------------------------
    // AsyncTCP callbacks
    // -------------------------------------------------------------------------

    template<typename Packet, std::size_t QueueDepth>
    void esp_async_wifi_channel<Packet, QueueDepth>::on_client(
        AsyncClient* client
    ) noexcept {
        if (_client) {
            client->close(true);
            return;
        }

        _client = client;

        client->onData(
            [](void* arg, AsyncClient*, void* data, std::size_t len) {
                static_cast<esp_async_wifi_channel*>(arg)->on_data(data, len);
            },
            this
        );

        client->onDisconnect(
            [](void* arg, AsyncClient* c) {
                static_cast<esp_async_wifi_channel*>(arg)->on_disconnect(c);
            },
            this
        );
    }

    template<typename Packet, std::size_t QueueDepth>
    void esp_async_wifi_channel<Packet, QueueDepth>::on_data(
        const void* data, std::size_t len
    ) noexcept {
        const auto* bytes = static_cast<const std::byte*>(data);
        std::size_t consumed = 0;

        while (consumed < len) {
            const std::size_t needed    = sizeof(Packet) - _staging_used;
            const std::size_t available = len - consumed;
            const std::size_t to_copy   = available < needed ? available : needed;

            std::memcpy(_staging + _staging_used, bytes + consumed, to_copy);
            _staging_used += to_copy;
            consumed      += to_copy;

            if (_staging_used == sizeof(Packet)) {
                push_packet();
                _staging_used = 0;
            }
        }
    }

    template<typename Packet, std::size_t QueueDepth>
    void esp_async_wifi_channel<Packet, QueueDepth>::on_disconnect(
        AsyncClient* client
    ) noexcept {
        if (client == _client) {
            _client       = nullptr;
            _staging_used = 0;
        }
    }

    // -------------------------------------------------------------------------
    // Ring queue helpers
    // -------------------------------------------------------------------------

    template<typename Packet, std::size_t QueueDepth>
    void esp_async_wifi_channel<Packet, QueueDepth>::push_packet() noexcept {
        _cs.enter();
        const bool full = queue_full();
        if (not full) {
            std::memcpy(&_slots[_head], _staging, sizeof(Packet));
            _head = (_head + 1) % QueueDepth;
        }
        _cs.exit();
    }

    template<typename Packet, std::size_t QueueDepth>
    bool esp_async_wifi_channel<Packet, QueueDepth>::queue_empty() const noexcept {
        return _head == _tail;
    }

    template<typename Packet, std::size_t QueueDepth>
    bool esp_async_wifi_channel<Packet, QueueDepth>::queue_full() const noexcept {
        return (_head + 1) % QueueDepth == _tail;
    }

} // namespace ecomm::channels

#endif // ECOMM_NO_ESP_ASYNC_WIFI_SUPPORT
#endif // ECOMM_CHANNELS_ESP_ASYNC_WIFI_CHANNEL_TPP_
