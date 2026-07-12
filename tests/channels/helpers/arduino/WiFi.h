// SPDX-License-Identifier: MIT
/**
* @file WiFi.h
*
* @brief Test-only mock for the Arduino ESP32/ESP8266 WiFi library.
*
* Replaces the real <WiFi.h> / <ESP8266WiFi.h> when compiling channel tests
* on a host machine. CMake injects this directory via target_include_directories
* so the include resolves here transparently.
*
* Design note — shared backend:
*   The real WiFiClient is a handle (thin wrapper over a socket fd). Copies of
*   it refer to the same underlying connection. The mock mirrors this by having
*   all copies of a WiFiClient share a single Backend via std::shared_ptr. This
*   means bytes written by the channel's internal _client copy are visible
*   through the original WiFiClient held by the test, matching real behaviour.
*
*   WiFiServer::available() returns a WiFiClient sharing the server's backend,
*   so any write performed by the channel is directly inspectable from the test.
*
* @note Test code only. std::shared_ptr/std::deque/std::vector are used
*       deliberately — they must never appear in library code paths.
*/
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// WiFiClient mock
// ---------------------------------------------------------------------------

struct WiFiClientBackend {
    std::deque<uint8_t>  rx;
    std::vector<uint8_t> tx;
    bool connected = true;
};

class WiFiClient {
public:
    // Default-constructed client is "disconnected" (no shared backend).
    WiFiClient() = default;

    // Construct a connected client sharing the given backend.
    explicit WiFiClient(std::shared_ptr<WiFiClientBackend> backend)
        : _backend{std::move(backend)} {}

    /// Push raw bytes into the RX queue as if they arrived from the wire.
    void inject(const void* data, std::size_t len) {
        const auto* p = static_cast<const uint8_t*>(data);
        _backend->rx.insert(_backend->rx.end(), p, p + len);
    }

    /// Bytes written by the channel — inspect from tests after send().
    const std::vector<uint8_t>& tx() const { return _backend->tx; }

    // bool-conversion: true when connected and backend is present.
    explicit operator bool() const {
        return _backend && _backend->connected;
    }

    // --- Arduino WiFiClient API used by arduino_wifi_channel ---

    int available() const {
        if (!_backend) return 0;
        return static_cast<int>(_backend->rx.size());
    }

    std::size_t read(uint8_t* buf, std::size_t len) {
        if (!_backend) return 0;
        const std::size_t n = std::min(len, _backend->rx.size());
        for (std::size_t i = 0; i < n; ++i) {
            buf[i] = _backend->rx.front();
            _backend->rx.pop_front();
        }
        return n;
    }

    std::size_t write(const uint8_t* buf, std::size_t len) {
        if (!_backend) return 0;
        _backend->tx.insert(_backend->tx.end(), buf, buf + len);
        return len;
    }

private:
    std::shared_ptr<WiFiClientBackend> _backend;
};

// ---------------------------------------------------------------------------
// WiFiServer mock
// ---------------------------------------------------------------------------

class WiFiServer {
public:
    WiFiServer()
        : _backend{std::make_shared<WiFiClientBackend>()}
        , client{_backend}
    {}

    /// Control whether available() hands out a client or a disconnected stub.
    void set_client_available(bool v) { _backend->connected = v; }

    // --- Arduino WiFiServer API used by arduino_wifi_channel ---

    /// Returns a WiFiClient sharing the server's backend (mirrors real handle semantics).
    WiFiClient available() {
        if (_backend->connected) return WiFiClient{_backend};
        return WiFiClient{};   // disconnected stub
    }

    // _backend is declared first so it is initialised before client.
    // C++ initialises members in declaration order regardless of the
    // initialiser-list order, so this placement is load-bearing.
    std::shared_ptr<WiFiClientBackend> _backend;

    /// The canonical client handle — use this in tests to inject RX or inspect TX.
    WiFiClient client;
};
