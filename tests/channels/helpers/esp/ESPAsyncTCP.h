// SPDX-License-Identifier: MIT
/**
* @file ESPAsyncTCP.h
*
* @brief Test-only mock for the ESPAsyncTCP library (ESP8266) and AsyncTCP (ESP32).
*
* Replaces the real <ESPAsyncTCP.h> / <AsyncTCP.h> when compiling channel tests
* on a host machine. CMake injects this directory via target_include_directories
* so both includes resolve here transparently — the channel source is unchanged.
*
* Design:
*   AsyncClient stores the function pointers and arg pointers registered via
*   onData / onDisconnect so that tests can fire them manually, simulating what
*   the TCP task would do. write() captures bytes into a tx vector.
*
*   AsyncServer stores the onClient callback so the test can simulate a new
*   connection by calling simulate_connect(client).
*
*   noInterrupts() / interrupts() are no-ops — the critical_section on the
*   ESP8266 path compiles to nothing on the host, which is correct for a
*   single-threaded unit test.
*
* @note Test code only. std::vector / std::function are used deliberately;
*       they must never appear in library source paths.
*/
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Arduino-compat stubs needed by the critical_section in the channel
// ---------------------------------------------------------------------------

// On the ESP8266 path the critical_section calls noInterrupts()/interrupts().
// On the host these are no-ops.
inline void noInterrupts() {}
inline void interrupts()   {}

// ---------------------------------------------------------------------------
// AsyncClient mock
// ---------------------------------------------------------------------------

class AsyncClient {
public:
    // Bytes written by do_send — inspect in tests.
    std::vector<uint8_t> tx;

    // --- Callback registration (called by the channel in on_client) ---------

    using data_cb_t       = void(*)(void*, AsyncClient*, void*, std::size_t);
    using disconnect_cb_t = void(*)(void*, AsyncClient*);

    void onData(data_cb_t cb, void* arg) noexcept {
        _data_cb  = cb;
        _data_arg = arg;
    }

    void onDisconnect(disconnect_cb_t cb, void* arg) noexcept {
        _disconnect_cb  = cb;
        _disconnect_arg = arg;
    }

    // --- API called by do_send ----------------------------------------------

    std::size_t write(const char* data, std::size_t len) {
        const auto* p = reinterpret_cast<const uint8_t*>(data);
        tx.insert(tx.end(), p, p + len);
        return len;
    }

    // close() called when a second client tries to connect.
    void close(bool /*abort*/ = false) noexcept { _closed = true; }

    bool was_closed() const noexcept { return _closed; }

    // --- Test helpers — simulate what the TCP task would do -----------------

    /// Deliver raw bytes to the channel's onData callback.
    void simulate_data(const void* data, std::size_t len) {
        if (_data_cb) _data_cb(_data_arg, this, const_cast<void*>(data), len);
    }

    /// Fire the disconnect callback.
    void simulate_disconnect() {
        if (_disconnect_cb) _disconnect_cb(_disconnect_arg, this);
    }

private:
    data_cb_t       _data_cb       = nullptr;
    void*           _data_arg      = nullptr;
    disconnect_cb_t _disconnect_cb = nullptr;
    void*           _disconnect_arg= nullptr;
    bool            _closed        = false;
};

// ---------------------------------------------------------------------------
// AsyncServer mock
// ---------------------------------------------------------------------------

class AsyncServer {
public:
    using client_cb_t = void(*)(void*, AsyncClient*);

    void onClient(client_cb_t cb, void* arg) noexcept {
        _client_cb  = cb;
        _client_arg = arg;
    }

    /// Simulate an incoming connection — fires the channel's on_client handler.
    void simulate_connect(AsyncClient* client) {
        if (_client_cb) _client_cb(_client_arg, client);
    }

private:
    client_cb_t _client_cb  = nullptr;
    void*       _client_arg = nullptr;
};
