// SPDX-License-Identifier: MIT
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
*     - send() returns send_result::ok.
*
*   try_receive
*     - Returns nullopt when no bytes are available.
*     - Returns nullopt when fewer than sizeof(Packet) bytes are available.
*     - Returns an engaged optional when a complete sealed packet is injected.
*     - Returns nullopt when a complete but corrupt packet is injected.
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
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-26 Initial creation.
* - 2026-05-27 Updated for send_result return and std::optional try_receive.
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
    static_cast<void>(ch.send(pkt));

    EXPECT_EQ(serial.tx.size(), sizeof(test_packet));
}

TEST(arduino_serial_channel_send, written_bytes_deserialize_to_valid_packet) {
    HardwareSerial serial;
    test_channel ch{serial};

    test_packet pkt{header_type::data, header_options::none};
    static_cast<void>(ch.send(pkt));

    test_packet wire{};
    std::memcpy(&wire, serial.tx.data(), sizeof(test_packet));
    EXPECT_TRUE(validator<test_packet>{}.is_valid(wire));
}

TEST(arduino_serial_channel_send, sending_twice_appends_two_packets) {
    HardwareSerial serial;
    test_channel ch{serial};

    test_packet pkt{header_type::data, header_options::none};
    static_cast<void>(ch.send(pkt));
    static_cast<void>(ch.send(pkt));

    EXPECT_EQ(serial.tx.size(), 2 * sizeof(test_packet));
}

TEST(arduino_serial_channel_send, returns_ok) {
    HardwareSerial serial;
    test_channel ch{serial};

    test_packet pkt{header_type::data, header_options::none};
    EXPECT_EQ(ch.send(pkt), send_result::ok);
}

// ---------------------------------------------------------------------------
// try_receive
// ---------------------------------------------------------------------------

TEST(arduino_serial_channel_try_receive, returns_nullopt_when_empty) {
    HardwareSerial serial;
    test_channel ch{serial};

    EXPECT_FALSE(ch.try_receive().has_value());
}

TEST(arduino_serial_channel_try_receive, returns_nullopt_when_partial_packet) {
    HardwareSerial serial;
    test_channel ch{serial};

    const test_packet pkt = make_sealed();
    serial.inject(&pkt, sizeof(test_packet) - 1);

    EXPECT_FALSE(ch.try_receive().has_value());
}

TEST(arduino_serial_channel_try_receive, returns_engaged_optional_for_valid_packet) {
    HardwareSerial serial;
    test_channel ch{serial};

    const test_packet pkt = make_sealed();
    serial.inject(&pkt, sizeof(test_packet));

    EXPECT_TRUE(ch.try_receive().has_value());
}

TEST(arduino_serial_channel_try_receive, received_packet_matches_sent) {
    HardwareSerial serial;
    test_channel ch{serial};

    const test_packet pkt = make_sealed();
    serial.inject(&pkt, sizeof(test_packet));

    const auto result = ch.try_receive();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt, sizeof(test_packet)), 0);
}

TEST(arduino_serial_channel_try_receive, returns_nullopt_for_corrupt_packet) {
    HardwareSerial serial;
    test_channel ch{serial};

    test_packet pkt = make_sealed();
    pkt.payload[0] ^= std::byte{0xFF};
    serial.inject(&pkt, sizeof(test_packet));

    EXPECT_FALSE(ch.try_receive().has_value());
}

// ---------------------------------------------------------------------------
// round-trip
// ---------------------------------------------------------------------------

TEST(arduino_serial_channel_round_trip, send_then_receive_preserves_packet) {
    HardwareSerial tx_serial;
    HardwareSerial rx_serial;
    test_channel   tx_ch{tx_serial};
    test_channel   rx_ch{rx_serial};

    test_packet original{header_type::data, header_options::none};
    for (std::size_t i = 0; i < test_packet::payload_size; ++i)
        original.payload[i] = static_cast<std::byte>(i);

    static_cast<void>(tx_ch.send(original));
    rx_serial.inject(tx_serial.tx.data(), tx_serial.tx.size());

    const auto result = rx_ch.try_receive();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(std::memcmp(result->payload, original.payload,
                          test_packet::payload_size), 0);
    EXPECT_EQ(result->header.type(), header_type::data);
}
