// SPDX-License-Identifier: MIT
/**
* @file esp_async_wifi_channel.tpp
*
* @brief Implementation of esp_async_wifi_channel<BufferCapacity>.
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
* - 2026-07-16 Rewritten around a byte ring instead of a Packet-typed slot
*      ring; do_send/do_try_receive became member templates over Packet. See
*      esp_async_wifi_channel.hpp's file-level documentation for the design.
* - 2026-07-17 on_data's overflow handling changed from "truncate the tail of
*      the incoming delivery in place, keep the existing backlog" to "reset to
*      offset 0 and place this delivery there, discarding the existing
*      backlog" -- avoids ever writing a truncated chunk straddling the
*      ring's physical wrap boundary, and in the common case (an existing
*      backlog plus this delivery don't fit together, but the delivery alone
*      does) keeps the new delivery whole and untorn instead of splitting it.
* - 2026-07-21 Added do_receive_raw (backs channel::receive_raw): pops
*      min(max, bytes_available()) bytes from the ring, wrapping as needed,
*      under the same locking discipline as do_try_receive.
*/
#ifndef ECOMM_CHANNELS_ESP_ASYNC_WIFI_CHANNEL_TPP_
#define ECOMM_CHANNELS_ESP_ASYNC_WIFI_CHANNEL_TPP_
#ifndef ECOMM_NO_ESP_ASYNC_WIFI_SUPPORT

#include "esp_async_wifi_channel.hpp"

namespace ecomm::channels {

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------

    template<std::size_t BufferCapacity>
    esp_async_wifi_channel<BufferCapacity>::esp_async_wifi_channel(
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

    template<std::size_t BufferCapacity>
    template<typename Packet>
    void esp_async_wifi_channel<BufferCapacity>::do_send(
        const Packet& packet
    ) noexcept {
        static_assert(std::is_trivially_copyable_v<Packet>,
            "esp_async_wifi_channel: Packet must be trivially copyable; "
            "raw bytes are written directly to the client.");

        if (not _client) return;
        _client->write(
            reinterpret_cast<const char*>(&packet),
            sizeof(Packet)
        );
    }

    template<std::size_t BufferCapacity>
    template<typename Packet>
    bool esp_async_wifi_channel<BufferCapacity>::do_try_receive(
        Packet& out
    ) noexcept {
        static_assert(std::is_trivially_copyable_v<Packet>,
            "esp_async_wifi_channel: Packet must be trivially copyable; "
            "memcpy is used to move packets in and out of the ring buffer.");
        static_assert(sizeof(Packet) < BufferCapacity,
            "esp_async_wifi_channel: BufferCapacity must be large enough to hold "
            "at least one Packet (one byte is always reserved to distinguish "
            "full from empty).");

        _cs.enter();
        const std::size_t avail = bytes_available();
        _cs.exit();

        if (avail < sizeof(Packet)) return false;

        // Safe without holding the lock: _tail is this method's exclusive
        // domain, and on_data only ever writes into [_head, _tail) (mod
        // BufferCapacity) -- the not-yet-exposed free region -- never into
        // bytes already committed to [_tail, _head). Verifying `avail` under
        // lock above guarantees the region read here was fully committed by
        // on_data before this call started.
        auto* dest = reinterpret_cast<std::byte*>(&out);
        const std::size_t first_chunk = std::min(sizeof(Packet), BufferCapacity - _tail);
        std::memcpy(dest, _ring.data() + _tail, first_chunk);
        if (sizeof(Packet) > first_chunk) {
            std::memcpy(dest + first_chunk, _ring.data(), sizeof(Packet) - first_chunk);
        }

        _cs.enter();
        _tail = (_tail + sizeof(Packet)) % BufferCapacity;
        _cs.exit();

        return true;
    }

    template<std::size_t BufferCapacity>
    std::size_t esp_async_wifi_channel<BufferCapacity>::do_receive_raw(
        std::byte* dst, std::size_t max
    ) noexcept {
        _cs.enter();
        const std::size_t avail = bytes_available();
        _cs.exit();

        const std::size_t n = std::min(max, avail);
        if (n == 0) return 0;

        // Safe outside the lock for the same reason as do_try_receive: _tail is
        // the consumer's exclusive domain and the [_tail, _tail+n) region was
        // fully committed by on_data before `avail` was observed above.
        const std::size_t first_chunk = std::min(n, BufferCapacity - _tail);
        std::memcpy(dst, _ring.data() + _tail, first_chunk);
        if (n > first_chunk) {
            std::memcpy(dst + first_chunk, _ring.data(), n - first_chunk);
        }

        _cs.enter();
        _tail = (_tail + n) % BufferCapacity;
        _cs.exit();

        return n;
    }

    // -------------------------------------------------------------------------
    // AsyncTCP callbacks
    // -------------------------------------------------------------------------

    template<std::size_t BufferCapacity>
    void esp_async_wifi_channel<BufferCapacity>::on_client(
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

    template<std::size_t BufferCapacity>
    void esp_async_wifi_channel<BufferCapacity>::on_data(
        const void* data, std::size_t len
    ) noexcept {
        const auto* bytes = static_cast<const std::byte*>(data);

        _cs.enter();
        const std::size_t room = bytes_free();
        _cs.exit();

        if (len <= room) {
            // Fits: append normally, wrapping through the ring as needed.
            // Safe without holding the lock: _head is this callback's
            // exclusive domain (AsyncTCP never re-enters on_data concurrently
            // with itself for one connection), and [_head, _tail) (mod
            // BufferCapacity) is the free region no consumer read ever
            // touches until _head is committed below.
            const std::size_t first_chunk = std::min(len, BufferCapacity - _head);
            std::memcpy(_ring.data() + _head, bytes, first_chunk);
            if (len > first_chunk) {
                std::memcpy(_ring.data(), bytes + first_chunk, len - first_chunk);
            }

            _cs.enter();
            _head = (_head + len) % BufferCapacity;
            _cs.exit();
            return;
        }

        // Overflow: rather than writing a truncated chunk wherever _head
        // currently sits -- which could itself straddle the ring's physical
        // wrap boundary mid-write -- reset to a clean run starting at offset
        // 0 and place this delivery there instead. Whatever was buffered and
        // not yet read is discarded. If the delivery itself is larger than
        // the ring can ever hold, only its first BufferCapacity-1 bytes are
        // kept; otherwise (the overwhelmingly common case: an existing
        // backlog plus this delivery together don't fit, but the delivery
        // alone does) the entire delivery is kept intact, untorn. See the
        // file-level "Overflow" note.
        const std::size_t to_copy = std::min(len, BufferCapacity - 1);
        std::memcpy(_ring.data(), bytes, to_copy);

        _cs.enter();
        _head = to_copy;
        _tail = 0;
        _cs.exit();
    }

    template<std::size_t BufferCapacity>
    void esp_async_wifi_channel<BufferCapacity>::on_disconnect(
        AsyncClient* client
    ) noexcept {
        if (client not_eq _client) return;

        _client = nullptr;

        // Clear the entire ring, not just a trailing partial packet -- see
        // the file-level "Disconnect clears the entire ring" note for why a
        // byte ring cannot tell a complete-but-unread packet apart from a
        // partial one at this point.
        _cs.enter();
        _head = 0;
        _tail = 0;
        _cs.exit();
    }

    // -------------------------------------------------------------------------
    // Ring buffer helpers
    // -------------------------------------------------------------------------

    template<std::size_t BufferCapacity>
    std::size_t esp_async_wifi_channel<BufferCapacity>::bytes_available() const noexcept {
        return (_head + BufferCapacity - _tail) % BufferCapacity;
    }

    template<std::size_t BufferCapacity>
    std::size_t esp_async_wifi_channel<BufferCapacity>::bytes_free() const noexcept {
        return BufferCapacity - 1 - bytes_available();
    }

} // namespace ecomm::channels

#endif // ECOMM_NO_ESP_ASYNC_WIFI_SUPPORT
#endif // ECOMM_CHANNELS_ESP_ASYNC_WIFI_CHANNEL_TPP_
