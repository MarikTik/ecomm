// SPDX-License-Identifier: BSL-1.1
/**
* @file test_arduino_wifi_channel.cpp
*
* @brief Unit tests for ecomm::channels::arduino_wifi_channel.
*
* @ingroup ecomm_tests
*
* Tests use mock WiFiServer/WiFiClient types (injected via CMake include path)
* so the real arduino_wifi_channel.hpp/.tpp is compiled and exercised on the
* host without any Arduino/ESP SDK present. No library source file is modified.
*
* The mock WiFiClient uses a shared backend (std::shared_ptr) so that the
* channel's internal copy and the test's handle see the same RX/TX buffers —
* mirroring the real WiFiClient handle semantics.
*
* Coverage:
*   send
*     - Exactly sizeof(Packet) bytes are written through the client.
*     - The written bytes deserialize to a packet whose FCS is valid.
*     - Sending twice appends two packets to the TX buffer.
*     - No bytes written when no client is available.
*
*   try_receive
*     - Returns false when no client is available.
*     - Returns false when client is available but RX is empty.
*     - Returns false when fewer than sizeof(Packet) bytes are available.
*     - Returns true and populates `out` for a complete sealed packet.
*     - Returns false for a complete but corrupt packet (state of `out` unspecified).
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

// WiFi.h resolves to the mock via CMake include_directories injection.
// ESP32 must be defined before including the channel header to satisfy its
// platform guard (arduino_wifi_channel.hpp checks defined(ESP32) first).
#include <ecomm/channels/arduino_wifi_channel.hpp>
#include <ecomm/protocol/packet.hpp>
#include <ecomm/protocol/validator.hpp>
#include <ecomm/protocol/checksum.hpp>

using namespace ecomm::protocol;
using namespace ecomm::channels;

using test_packet  = packet<32, topology::point_to_point, no_sequence, crc32>;
using test_channel = arduino_wifi_channel<test_packet>;

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

TEST(arduino_wifi_channel_send, writes_exactly_packet_size_bytes) {
    WiFiServer server;
    test_channel ch{server};

    test_packet pkt{header_type::data, header_options::none};
    ch.send(pkt);

    EXPECT_EQ(server.client.tx().size(), sizeof(test_packet));
}

TEST(arduino_wifi_channel_send, written_bytes_deserialize_to_valid_packet) {
    WiFiServer server;
    test_channel ch{server};

    test_packet pkt{header_type::data, header_options::none};
    ch.send(pkt);

    test_packet wire{};
    std::memcpy(&wire, server.client.tx().data(), sizeof(test_packet));
    EXPECT_TRUE(validator<test_packet>{}.is_valid(wire));
}

TEST(arduino_wifi_channel_send, sending_twice_appends_two_packets) {
    WiFiServer server;
    test_channel ch{server};

    test_packet pkt{header_type::data, header_options::none};
    ch.send(pkt);
    ch.send(pkt);

    EXPECT_EQ(server.client.tx().size(), 2 * sizeof(test_packet));
}

TEST(arduino_wifi_channel_send, no_bytes_written_when_no_client) {
    WiFiServer server;
    server.set_client_available(false);
    test_channel ch{server};

    test_packet pkt{header_type::data, header_options::none};
    ch.send(pkt);

    EXPECT_TRUE(server.client.tx().empty());
}

// ---------------------------------------------------------------------------
// try_receive
// ---------------------------------------------------------------------------

TEST(arduino_wifi_channel_try_receive, returns_false_when_no_client) {
    WiFiServer server;
    server.set_client_available(false);
    test_channel ch{server};

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

TEST(arduino_wifi_channel_try_receive, returns_false_when_rx_empty) {
    WiFiServer server;
    test_channel ch{server};

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

TEST(arduino_wifi_channel_try_receive, returns_false_when_partial_packet) {
    WiFiServer server;
    test_channel ch{server};

    const test_packet pkt = make_sealed();
    server.client.inject(&pkt, sizeof(test_packet) - 1);

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

TEST(arduino_wifi_channel_try_receive, accepts_valid_sealed_packet) {
    WiFiServer server;
    test_channel ch{server};

    const test_packet pkt = make_sealed();
    server.client.inject(&pkt, sizeof(test_packet));

    test_packet out{};
    EXPECT_TRUE(ch.try_receive(out));
}

TEST(arduino_wifi_channel_try_receive, received_packet_matches_sent) {
    WiFiServer server;
    test_channel ch{server};

    const test_packet pkt = make_sealed();
    server.client.inject(&pkt, sizeof(test_packet));

    test_packet out{};
    static_cast<void>(ch.try_receive(out));

    EXPECT_EQ(std::memcmp(&out, &pkt, sizeof(test_packet)), 0);
}

TEST(arduino_wifi_channel_try_receive, rejects_corrupt_packet) {
    WiFiServer server;
    test_channel ch{server};

    test_packet pkt = make_sealed();
    pkt.payload[0] ^= std::byte{0xFF};
    server.client.inject(&pkt, sizeof(test_packet));

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

TEST(arduino_wifi_channel_try_receive, corrupt_packet_returns_false) {
    // Contract: a structurally corrupt packet must be rejected (false return).
    // The state of `out` after a false return is intentionally unspecified —
    // try_receive reads into `out` before validating to avoid a second
    // packet-sized stack allocation on RAM-constrained targets.
    WiFiServer server;
    test_channel ch{server};

    test_packet pkt = make_sealed();
    pkt.payload[0] ^= std::byte{0xFF};
    server.client.inject(&pkt, sizeof(test_packet));

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

// ---------------------------------------------------------------------------
// round-trip
// ---------------------------------------------------------------------------

TEST(arduino_wifi_channel_round_trip, send_then_receive_preserves_packet) {
    WiFiServer tx_server;
    WiFiServer rx_server;
    test_channel tx_ch{tx_server};
    test_channel rx_ch{rx_server};

    test_packet original{header_type::data, header_options::none};
    for (std::size_t i = 0; i < test_packet::payload_size; ++i)
        original.payload[i] = static_cast<std::byte>(i);

    tx_ch.send(original);

    // Wire the TX bytes from the sender's client into the receiver's client.
    rx_server.client.inject(tx_server.client.tx().data(),
                            tx_server.client.tx().size());

    test_packet received{};
    ASSERT_TRUE(rx_ch.try_receive(received));

    EXPECT_EQ(std::memcmp(received.payload, original.payload,
                          test_packet::payload_size), 0);
    EXPECT_EQ(received.header.type(), header_type::data);
}
