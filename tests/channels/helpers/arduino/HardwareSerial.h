// SPDX-License-Identifier: BSL-1.1
/**
* @file HardwareSerial.h
*
* @brief Test-only mock for Arduino's HardwareSerial class.
*
* Replaces the real Arduino header when compiling channel tests on a host
* machine. CMake injects this directory ahead of any system include paths via
* target_include_directories, so #include <HardwareSerial.h> inside
* arduino_serial_channel.hpp resolves here transparently — no changes to
* library source are required.
*
* The mock models a bidirectional byte pipe:
*   - inject()   — push bytes into the RX queue as if they arrived from wire.
*   - available() / readBytes() — consumed by do_try_receive.
*   - write()    — captured in `tx` for test assertions.
*
* @note Test code only. std::deque/std::vector are used deliberately — they
*       must never appear in library code paths (ecomm/channels/, protocol/).
*/
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

class HardwareSerial {
public:
    /// Bytes captured by write() — inspect in tests to verify what was sent.
    std::vector<uint8_t> tx;

    /// Push raw bytes into the RX queue as if they arrived from the wire.
    void inject(const void* data, std::size_t len) {
        const auto* p = static_cast<const uint8_t*>(data);
        _rx.insert(_rx.end(), p, p + len);
    }

    // --- Arduino HardwareSerial API used by arduino_serial_channel ---

    int available() const {
        return static_cast<int>(_rx.size());
    }

    std::size_t readBytes(uint8_t* buf, std::size_t len) {
        const std::size_t n = std::min(len, _rx.size());
        for (std::size_t i = 0; i < n; ++i) {
            buf[i] = _rx.front();
            _rx.pop_front();
        }
        return n;
    }

    std::size_t write(const uint8_t* buf, std::size_t len) {
        tx.insert(tx.end(), buf, buf + len);
        return len;
    }

private:
    std::deque<uint8_t> _rx;
};
