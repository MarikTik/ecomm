// SPDX-License-Identifier: BSL-1.1
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
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2026 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
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
        // Register the new-connection callback. AsyncTCP calls this on the TCP
        // task whenever a client completes the handshake. The lambda captures
        // `this` by pointer  --  safe because the channel outlives the server in
        // any correct usage (see @pre in the header).
        _server.onClient(
            [](void* arg, AsyncClient* client) {
                static_cast<esp_async_wifi_channel*>(arg)->on_client(client);
            },
            this
        );
    }

    // -------------------------------------------------------------------------
    // channel<> CRTP contract  --  called by the base send / try_receive
    // -------------------------------------------------------------------------

    template<typename Packet, std::size_t QueueDepth>
    void esp_async_wifi_channel<Packet, QueueDepth>::do_send(
        const Packet& packet
    ) noexcept {
        if (!_client) return;
        // write() accepts a const char* and a size. The cast is safe: Packet is
        // trivially copyable and we are treating it as a raw byte sequence.
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

        // Copy the packet out of the ring slot before advancing the tail, so
        // the callback can never overwrite a slot we are mid-read.
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
            // Already have an active connection  --  reject the newcomer.
            client->close(true);
            return;
        }

        _client = client;

        // Per-client data callback.
        client->onData(
            [](void* arg, AsyncClient*, void* data, std::size_t len) {
                static_cast<esp_async_wifi_channel*>(arg)->on_data(data, len);
            },
            this
        );

        // Per-client disconnect callback.
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
            // How many bytes does the current staging slot still need?
            const std::size_t needed   = sizeof(Packet) - _staging_used;
            const std::size_t available = len - consumed;
            const std::size_t to_copy  = available < needed ? available : needed;

            std::memcpy(_staging + _staging_used, bytes + consumed, to_copy);
            _staging_used += to_copy;
            consumed      += to_copy;

            if (_staging_used == sizeof(Packet)) {
                // A complete packet has been assembled  --  push it into the queue.
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
            _staging_used = 0;   // discard any partial packet in flight
        }
    }

    // -------------------------------------------------------------------------
    // Ring queue helpers
    // -------------------------------------------------------------------------

    template<typename Packet, std::size_t QueueDepth>
    void esp_async_wifi_channel<Packet, QueueDepth>::push_packet() noexcept {
        _cs.enter();
        const bool full = queue_full();
        if (!full) {
            std::memcpy(&_slots[_head], _staging, sizeof(Packet));
            _head = (_head + 1) % QueueDepth;
        }
        // If full: silently drop. Stale packets in a real-time workload are
        // worthless; the caller's try_receive rate is too low for the packet rate.
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
