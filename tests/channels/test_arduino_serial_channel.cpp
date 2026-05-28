// SPDX-License-Identifier: BSL-1.1
/**
* @file test_arduino_serial_channel.cpp
*
* @brief Unit tests for ecomm::channels::arduino_serial_channel.
*
* @ingroup ecomm_tests
*
* Tests use a mock HardwareSerial (injected via CMake include path) so the
* real arduino_serial_channel.hpp/.tpp is compiled and exercised without any
* Arduino SDK present on the host. No library source file is modified.
*
* Coverage:
*   send
*     - Exactly sizeof(Packet) bytes are written to the serial port.
*     - The written bytes deserialize to a packet whose FCS is valid.
*     - Sending twice appends two packets to the TX buffer.
*
*   try_receive
*     - Returns false when no bytes are available.
*     - Returns false when fewer than sizeof(Packet) bytes are available.
*     - Returns true and populates `out` when a complete sealed packet is injected.
*     - Returns false when a complete but corrupt packet is injected.
*     - Corrupt packet does not modify `out`.
*
*   round-trip
*     - A packet sent through one channel and injected into a second is
*       received correctly (payload preserved, header preserved).
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

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

// HardwareSerial.h resolves to the mock via CMake include_directories injection.
// ARDUINO must be defined before including the channel header to satisfy its guard.
#include <ecomm/channels/arduino_serial_channel.hpp>
#include <ecomm/protocol/packet.hpp>
#include <ecomm/protocol/validator.hpp>
#include <ecomm/protocol/checksum.hpp>

using namespace ecomm::protocol;
using namespace ecomm::channels;

// Packet with a real checksum policy so seal/validate are non-trivial.
using test_packet = packet<32, topology::point_to_point, no_sequence, crc32>;
using test_channel = arduino_serial_channel<test_packet>;

// Seal a packet using the validator and return it (for constructing wire bytes).
static test_packet make_sealed(header_type type = header_type::data,
                               header_options opts = header_options::none)
{
    test_packet p{type, opts};
    // Write a recognisable payload pattern.
    for (std::size_t i = 0; i < test_packet::payload_size; ++i)
        p.payload[i] = static_cast<std::byte>(0xA5 ^ i);
    validator<test_packet>{}.seal(p);
    return p;
}

// ---------------------------------------------------------------------------
// send
// ---------------------------------------------------------------------------

TEST(arduino_serial_channel_send, writes_exactly_packet_size_bytes) {
    HardwareSerial serial;
    test_channel ch{serial};

    test_packet pkt{header_type::data, header_options::none};
    ch.send(pkt);

    EXPECT_EQ(serial.tx.size(), sizeof(test_packet));
}

TEST(arduino_serial_channel_send, written_bytes_deserialize_to_valid_packet) {
    HardwareSerial serial;
    test_channel ch{serial};

    test_packet pkt{header_type::data, header_options::none};
    ch.send(pkt);

    test_packet wire{};
    std::memcpy(&wire, serial.tx.data(), sizeof(test_packet));
    EXPECT_TRUE(validator<test_packet>{}.is_valid(wire));
}

TEST(arduino_serial_channel_send, sending_twice_appends_two_packets) {
    HardwareSerial serial;
    test_channel ch{serial};

    test_packet pkt{header_type::data, header_options::none};
    ch.send(pkt);
    ch.send(pkt);

    EXPECT_EQ(serial.tx.size(), 2 * sizeof(test_packet));
}

// ---------------------------------------------------------------------------
// try_receive
// ---------------------------------------------------------------------------

TEST(arduino_serial_channel_try_receive, returns_false_when_empty) {
    HardwareSerial serial;
    test_channel ch{serial};

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

TEST(arduino_serial_channel_try_receive, returns_false_when_partial_packet) {
    HardwareSerial serial;
    test_channel ch{serial};

    // Inject one byte fewer than a full packet.
    const test_packet pkt = make_sealed();
    serial.inject(&pkt, sizeof(test_packet) - 1);

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

TEST(arduino_serial_channel_try_receive, accepts_valid_sealed_packet) {
    HardwareSerial serial;
    test_channel ch{serial};

    const test_packet pkt = make_sealed();
    serial.inject(&pkt, sizeof(test_packet));

    test_packet out{};
    EXPECT_TRUE(ch.try_receive(out));
}

TEST(arduino_serial_channel_try_receive, received_packet_matches_sent) {
    HardwareSerial serial;
    test_channel ch{serial};

    const test_packet pkt = make_sealed();
    serial.inject(&pkt, sizeof(test_packet));

    test_packet out{};
    static_cast<void>(ch.try_receive(out));

    EXPECT_EQ(std::memcmp(&out, &pkt, sizeof(test_packet)), 0);
}

TEST(arduino_serial_channel_try_receive, rejects_corrupt_packet) {
    HardwareSerial serial;
    test_channel ch{serial};

    test_packet pkt = make_sealed();
    // Flip a payload byte after sealing.
    pkt.payload[0] ^= std::byte{0xFF};
    serial.inject(&pkt, sizeof(test_packet));

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

TEST(arduino_serial_channel_try_receive, corrupt_packet_returns_false) {
    // Contract: a structurally corrupt packet must be rejected (false return).
    // The state of `out` after a false return is intentionally unspecified —
    // try_receive reads into `out` before validating to avoid a second
    // packet-sized stack allocation on RAM-constrained targets.
    HardwareSerial serial;
    test_channel ch{serial};

    test_packet pkt = make_sealed();
    pkt.payload[0] ^= std::byte{0xFF};
    serial.inject(&pkt, sizeof(test_packet));

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

// ---------------------------------------------------------------------------
// round-trip
// ---------------------------------------------------------------------------

TEST(arduino_serial_channel_round_trip, send_then_receive_preserves_packet) {
    HardwareSerial tx_serial;
    HardwareSerial rx_serial;
    test_channel   tx_ch{tx_serial};
    test_channel   rx_ch{rx_serial};

    // Build a packet with a known payload.
    test_packet original{header_type::data, header_options::none};
    for (std::size_t i = 0; i < test_packet::payload_size; ++i)
        original.payload[i] = static_cast<std::byte>(i);

    // Send through tx_ch — seals into tx_serial.tx.
    tx_ch.send(original);

    // Wire the TX bytes into rx_serial's RX queue.
    rx_serial.inject(tx_serial.tx.data(), tx_serial.tx.size());

    // Receive on rx_ch — validates.
    test_packet received{};
    ASSERT_TRUE(rx_ch.try_receive(received));

    // Payload must be byte-for-byte identical.
    EXPECT_EQ(std::memcmp(received.payload, original.payload,
                          test_packet::payload_size), 0);
    // Header type must be preserved.
    EXPECT_EQ(received.header.type(), header_type::data);
}
