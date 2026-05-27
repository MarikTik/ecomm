// SPDX-License-Identifier: BSL-1.1
/**
* @file test_esp_async_wifi_channel.cpp
*
* @brief Unit tests for ecomm::channels::esp_async_wifi_channel.
*
* @ingroup ecomm_tests
*
* Tests use a mock ESPAsyncTCP header (injected via CMake include path) so the
* real esp_async_wifi_channel.hpp/.tpp is compiled and exercised on the host
* without any ESP SDK present. No library source file is modified.
*
* The mock AsyncServer / AsyncClient store the function pointers registered by
* the channel and expose simulate_connect / simulate_data / simulate_disconnect
* so tests can drive the TCP callback path directly, exactly as the TCP task
* would on the real hardware.
*
* Coverage:
*   Construction
*     - Channel registers an onClient callback with the server on construction.
*
*   send
*     - No bytes written when no client is connected.
*     - Exactly sizeof(Packet) bytes written to the client after connection.
*     - Written bytes deserialize to a packet with a valid FCS.
*     - Sending twice appends two packets to the TX buffer.
*
*   try_receive — single complete delivery
*     - Returns false when queue is empty (no data delivered).
*     - Returns true and populates out after one complete packet is delivered.
*     - Received packet matches the injected bytes.
*     - Returns false for a corrupt packet (invalid FCS after delivery).
*
*   try_receive — fragmented delivery (TCP reassembly)
*     - A packet split across two simulate_data calls is reassembled correctly.
*     - A packet split into sizeof(Packet) individual byte deliveries is
*       reassembled correctly.
*
*   try_receive — multi-packet delivery (coalesced TCP segment)
*     - Two packets delivered in a single simulate_data call are enqueued and
*       dequeued independently.
*
*   Ring queue
*     - try_receive returns false once the queue is drained.
*     - When the queue is full, excess packets are silently dropped.
*
*   Disconnect
*     - After simulate_disconnect, try_receive returns false.
*     - A partial packet in staging is discarded on disconnect.
*     - A new connection after disconnect is accepted.
*
*   Second-connection rejection
*     - A second client connecting while one is active is closed immediately.
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
#include <vector>

// ESPAsyncTCP.h resolves to the mock via CMake include_directories injection.
// ESP8266 is defined so the channel compiles the noInterrupts/interrupts guard.
#include <ecomm/channels/esp_async_wifi_channel.hpp>
#include <ecomm/protocol/packet.hpp>
#include <ecomm/protocol/checksum.hpp>
#include <ecomm/protocol/validator.hpp>

using namespace ecomm::protocol;
using namespace ecomm::channels;

using test_packet  = packet<32, topology::point_to_point, crc32>;

// Queue depth of 4 for most tests; a dedicated fixture uses depth 3 for
// overflow testing (easier to fill than 4).
using test_channel = esp_async_wifi_channel<test_packet, 4>;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static test_packet make_sealed(header_type type = header_type::data,
                               header_options opts = header_options::none)
{
    test_packet p{type, opts};
    for (std::size_t i = 0; i < test_packet::payload_size; ++i)
        p.payload[i] = static_cast<std::byte>(0xA5 ^ i);
    validator<test_packet>{}.seal(p);
    return p;
}

// Deliver a packet's raw bytes to a client in one call.
static void inject(AsyncClient& client, const test_packet& pkt)
{
    client.simulate_data(&pkt, sizeof(test_packet));
}

// ---------------------------------------------------------------------------
// Fixture: server + one connected client, ready for data
// ---------------------------------------------------------------------------

struct fixture : ::testing::Test {
    AsyncServer  server;
    AsyncClient  client;
    test_channel ch{server};   // registers onClient with server on construction

    void SetUp() override {
        // Simulate a TCP handshake completing — fires the channel's on_client.
        server.simulate_connect(&client);
    }
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(esp_async_wifi_channel_construction, registers_on_client_callback) {
    // If onClient was not registered the server's callback pointer is null
    // and simulate_connect would silently do nothing. Verify that after
    // construction the server fires the channel's handler (which sets _client).
    AsyncServer  server;
    AsyncClient  client;
    test_channel ch{server};

    // Before connect: queue is empty, no client yet.
    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));

    // Connect — if no callback was registered this is a no-op and the next
    // send still hits the "no client" guard.
    server.simulate_connect(&client);

    test_packet pkt = make_sealed();
    ch.send(pkt);
    EXPECT_EQ(client.tx.size(), sizeof(test_packet));
}

// ---------------------------------------------------------------------------
// send
// ---------------------------------------------------------------------------

TEST(esp_async_wifi_channel_send, no_bytes_written_without_client) {
    AsyncServer  server;
    test_channel ch{server};   // no connect

    test_packet pkt = make_sealed();
    ch.send(pkt);
    // No client registered — nothing to assert on tx; just verify no crash.
    // (AsyncClient::tx is not accessible without a connected client handle.)
}

TEST_F(fixture, send_writes_exactly_packet_size_bytes) {
    test_packet pkt{header_type::data, header_options::none};
    ch.send(pkt);
    EXPECT_EQ(client.tx.size(), sizeof(test_packet));
}

TEST_F(fixture, send_written_bytes_have_valid_fcs) {
    test_packet pkt{header_type::data, header_options::none};
    ch.send(pkt);

    test_packet wire{};
    std::memcpy(&wire, client.tx.data(), sizeof(test_packet));
    EXPECT_TRUE(validator<test_packet>{}.is_valid(wire));
}

TEST_F(fixture, send_twice_appends_two_packets) {
    test_packet pkt{header_type::data, header_options::none};
    ch.send(pkt);
    ch.send(pkt);
    EXPECT_EQ(client.tx.size(), 2 * sizeof(test_packet));
}

// ---------------------------------------------------------------------------
// try_receive — single complete delivery
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_returns_false_when_empty) {
    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

TEST_F(fixture, try_receive_accepts_one_valid_packet) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);

    test_packet out{};
    EXPECT_TRUE(ch.try_receive(out));
}

TEST_F(fixture, try_receive_received_packet_matches_injected) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);

    test_packet out{};
    static_cast<void>(ch.try_receive(out));
    EXPECT_EQ(std::memcmp(&out, &pkt, sizeof(test_packet)), 0);
}

TEST_F(fixture, try_receive_rejects_corrupt_packet) {
    test_packet pkt = make_sealed();
    pkt.payload[0] ^= std::byte{0xFF};   // corrupt after sealing
    inject(client, pkt);

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

// ---------------------------------------------------------------------------
// try_receive — fragmented TCP delivery
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_reassembles_two_fragment_delivery) {
    const test_packet pkt = make_sealed();
    const std::size_t half = sizeof(test_packet) / 2;

    // Deliver first half, then second half — simulates TCP fragmentation.
    client.simulate_data(&pkt, half);
    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));   // incomplete: not yet queued

    client.simulate_data(
        reinterpret_cast<const std::byte*>(&pkt) + half,
        sizeof(test_packet) - half
    );
    EXPECT_TRUE(ch.try_receive(out));
    EXPECT_EQ(std::memcmp(&out, &pkt, sizeof(test_packet)), 0);
}

TEST_F(fixture, try_receive_reassembles_byte_by_byte_delivery) {
    const test_packet pkt = make_sealed();
    const auto* raw = reinterpret_cast<const std::byte*>(&pkt);

    for (std::size_t i = 0; i < sizeof(test_packet); ++i)
        client.simulate_data(raw + i, 1);

    test_packet out{};
    EXPECT_TRUE(ch.try_receive(out));
    EXPECT_EQ(std::memcmp(&out, &pkt, sizeof(test_packet)), 0);
}

// ---------------------------------------------------------------------------
// try_receive — coalesced TCP segment (multiple packets in one delivery)
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_handles_two_packets_in_one_delivery) {
    const test_packet pkt_a = make_sealed(header_type::data);
    const test_packet pkt_b = make_sealed(header_type::control);

    // Build a buffer containing both packets back-to-back.
    std::vector<std::byte> buf(2 * sizeof(test_packet));
    std::memcpy(buf.data(),                  &pkt_a, sizeof(test_packet));
    std::memcpy(buf.data() + sizeof(test_packet), &pkt_b, sizeof(test_packet));

    client.simulate_data(buf.data(), buf.size());

    test_packet out_a{}, out_b{};
    EXPECT_TRUE(ch.try_receive(out_a));
    EXPECT_TRUE(ch.try_receive(out_b));
    EXPECT_EQ(std::memcmp(&out_a, &pkt_a, sizeof(test_packet)), 0);
    EXPECT_EQ(std::memcmp(&out_b, &pkt_b, sizeof(test_packet)), 0);
}

// ---------------------------------------------------------------------------
// Ring queue
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_returns_false_after_queue_drained) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);

    test_packet out{};
    EXPECT_TRUE(ch.try_receive(out));
    EXPECT_FALSE(ch.try_receive(out));   // queue empty now
}

TEST(esp_async_wifi_channel_queue, overflow_drops_excess_packets) {
    // Use QueueDepth=3: capacity is 2 usable slots (one always kept empty).
    // Inject 3 packets — the third must be silently dropped.
    using small_channel = esp_async_wifi_channel<test_packet, 3>;

    AsyncServer  server;
    AsyncClient  client;
    small_channel ch{server};
    server.simulate_connect(&client);

    const test_packet pkt_1 = make_sealed(header_type::data);
    const test_packet pkt_2 = make_sealed(header_type::control);
    const test_packet pkt_3 = make_sealed(header_type::data);

    inject(client, pkt_1);
    inject(client, pkt_2);
    inject(client, pkt_3);   // must be dropped — queue is full

    test_packet out{};
    EXPECT_TRUE(ch.try_receive(out));
    EXPECT_EQ(std::memcmp(&out, &pkt_1, sizeof(test_packet)), 0);

    EXPECT_TRUE(ch.try_receive(out));
    EXPECT_EQ(std::memcmp(&out, &pkt_2, sizeof(test_packet)), 0);

    // pkt_3 was dropped.
    EXPECT_FALSE(ch.try_receive(out));
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_returns_false_after_disconnect) {
    client.simulate_disconnect();

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}

TEST_F(fixture, disconnect_discards_partial_staging_packet) {
    const test_packet pkt = make_sealed();

    // Deliver only half the packet, then disconnect before it completes.
    client.simulate_data(&pkt, sizeof(test_packet) / 2);
    client.simulate_disconnect();

    // Reconnect with a fresh client and deliver a complete packet.
    AsyncClient client2;
    server.simulate_connect(&client2);

    const test_packet pkt2 = make_sealed(header_type::control);
    inject(client2, pkt2);

    test_packet out{};
    ASSERT_TRUE(ch.try_receive(out));
    // Must be pkt2, not a hybrid of the partial pkt and pkt2.
    EXPECT_EQ(std::memcmp(&out, &pkt2, sizeof(test_packet)), 0);
}

TEST_F(fixture, new_connection_accepted_after_disconnect) {
    client.simulate_disconnect();

    AsyncClient client2;
    server.simulate_connect(&client2);

    const test_packet pkt = make_sealed();
    inject(client2, pkt);

    test_packet out{};
    EXPECT_TRUE(ch.try_receive(out));
}

// ---------------------------------------------------------------------------
// Second-connection rejection
// ---------------------------------------------------------------------------

TEST_F(fixture, second_client_is_closed_immediately) {
    AsyncClient client2;
    server.simulate_connect(&client2);   // first client already active

    EXPECT_TRUE(client2.was_closed());
}

TEST_F(fixture, second_client_data_does_not_reach_queue) {
    AsyncClient client2;
    server.simulate_connect(&client2);

    // Even if the closed client somehow delivers data, it should not
    // reach the channel's queue (the data callback is never registered).
    const test_packet pkt = make_sealed();
    client2.simulate_data(&pkt, sizeof(test_packet));

    test_packet out{};
    EXPECT_FALSE(ch.try_receive(out));
}
