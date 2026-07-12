// SPDX-License-Identifier: MIT
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
*     - send() returns send_result::ok.
*
*   try_receive -- single complete delivery
*     - Returns nullopt when queue is empty (no data delivered).
*     - Returns an engaged optional after one complete packet is delivered.
*     - Received packet matches the injected bytes.
*     - Returns nullopt for a corrupt packet (invalid FCS after delivery).
*
*   try_receive -- fragmented delivery (TCP reassembly)
*     - A packet split across two simulate_data calls is reassembled correctly.
*     - A packet split into sizeof(Packet) individual byte deliveries is
*       reassembled correctly.
*
*   try_receive -- multi-packet delivery (coalesced TCP segment)
*     - Two packets delivered in a single simulate_data call are enqueued and
*       dequeued independently.
*
*   Ring queue
*     - try_receive returns nullopt once the queue is drained.
*     - When the queue is full, excess packets are silently dropped.
*
*   Disconnect
*     - After simulate_disconnect, try_receive returns nullopt.
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
#include <vector>

// ESPAsyncTCP.h resolves to the mock via CMake include_directories injection.
// ESP8266 is defined so the channel compiles the noInterrupts/interrupts guard.
#include <ecomm/channels/esp_async_wifi_channel.hpp>
#include <ecomm/protocol/packet.hpp>
#include <ecomm/protocol/checksum.hpp>
#include <ecomm/protocol/validator.hpp>

using namespace ecomm::protocol;
using namespace ecomm::channels;

using test_packet  = packet<32, topology::point_to_point, no_sequence, crc32>;

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
        // Simulate a TCP handshake completing -- fires the channel's on_client.
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
    EXPECT_FALSE(ch.try_receive().has_value());

    // Connect -- if no callback was registered this is a no-op and the next
    // send still hits the "no client" guard.
    server.simulate_connect(&client);

    test_packet pkt = make_sealed();
    static_cast<void>(ch.send(pkt));
    EXPECT_EQ(client.tx.size(), sizeof(test_packet));
}

// ---------------------------------------------------------------------------
// send
// ---------------------------------------------------------------------------

TEST(esp_async_wifi_channel_send, no_bytes_written_without_client) {
    AsyncServer  server;
    test_channel ch{server};   // no connect

    test_packet pkt = make_sealed();
    static_cast<void>(ch.send(pkt));
    // No client registered -- nothing to assert on tx; just verify no crash.
}

TEST_F(fixture, send_writes_exactly_packet_size_bytes) {
    test_packet pkt{header_type::data, header_options::none};
    static_cast<void>(ch.send(pkt));
    EXPECT_EQ(client.tx.size(), sizeof(test_packet));
}

TEST_F(fixture, send_written_bytes_have_valid_fcs) {
    test_packet pkt{header_type::data, header_options::none};
    static_cast<void>(ch.send(pkt));

    test_packet wire{};
    std::memcpy(&wire, client.tx.data(), sizeof(test_packet));
    EXPECT_TRUE(validator<test_packet>{}.is_valid(wire));
}

TEST_F(fixture, send_twice_appends_two_packets) {
    test_packet pkt{header_type::data, header_options::none};
    static_cast<void>(ch.send(pkt));
    static_cast<void>(ch.send(pkt));
    EXPECT_EQ(client.tx.size(), 2 * sizeof(test_packet));
}

TEST_F(fixture, send_returns_ok) {
    test_packet pkt{header_type::data, header_options::none};
    EXPECT_EQ(ch.send(pkt), send_result::ok);
}

// ---------------------------------------------------------------------------
// try_receive -- single complete delivery
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_returns_nullopt_when_empty) {
    EXPECT_FALSE(ch.try_receive().has_value());
}

TEST_F(fixture, try_receive_accepts_one_valid_packet) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);

    EXPECT_TRUE(ch.try_receive().has_value());
}

TEST_F(fixture, try_receive_received_packet_matches_injected) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);

    const auto result = ch.try_receive();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt, sizeof(test_packet)), 0);
}

TEST_F(fixture, try_receive_rejects_corrupt_packet) {
    test_packet pkt = make_sealed();
    pkt.payload[0] ^= std::byte{0xFF};   // corrupt after sealing
    inject(client, pkt);

    EXPECT_FALSE(ch.try_receive().has_value());
}

// ---------------------------------------------------------------------------
// try_receive -- fragmented TCP delivery
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_reassembles_two_fragment_delivery) {
    const test_packet pkt = make_sealed();
    const std::size_t half = sizeof(test_packet) / 2;

    // Deliver first half, then second half -- simulates TCP fragmentation.
    client.simulate_data(&pkt, half);
    EXPECT_FALSE(ch.try_receive().has_value());   // incomplete: not yet queued

    client.simulate_data(
        reinterpret_cast<const std::byte*>(&pkt) + half,
        sizeof(test_packet) - half
    );

    const auto result = ch.try_receive();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt, sizeof(test_packet)), 0);
}

TEST_F(fixture, try_receive_reassembles_byte_by_byte_delivery) {
    const test_packet pkt = make_sealed();
    const auto* raw = reinterpret_cast<const std::byte*>(&pkt);

    for (std::size_t i = 0; i < sizeof(test_packet); ++i)
        client.simulate_data(raw + i, 1);

    const auto result = ch.try_receive();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt, sizeof(test_packet)), 0);
}

// ---------------------------------------------------------------------------
// try_receive -- coalesced TCP segment (multiple packets in one delivery)
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_handles_two_packets_in_one_delivery) {
    const test_packet pkt_a = make_sealed(header_type::data);
    const test_packet pkt_b = make_sealed(header_type::control);

    // Build a buffer containing both packets back-to-back.
    std::vector<std::byte> buf(2 * sizeof(test_packet));
    std::memcpy(buf.data(),                      &pkt_a, sizeof(test_packet));
    std::memcpy(buf.data() + sizeof(test_packet), &pkt_b, sizeof(test_packet));

    client.simulate_data(buf.data(), buf.size());

    const auto result_a = ch.try_receive();
    const auto result_b = ch.try_receive();

    ASSERT_TRUE(result_a.has_value());
    ASSERT_TRUE(result_b.has_value());
    EXPECT_EQ(std::memcmp(&result_a.value(), &pkt_a, sizeof(test_packet)), 0);
    EXPECT_EQ(std::memcmp(&result_b.value(), &pkt_b, sizeof(test_packet)), 0);
}

// ---------------------------------------------------------------------------
// Ring queue
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_returns_nullopt_after_queue_drained) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);

    EXPECT_TRUE(ch.try_receive().has_value());
    EXPECT_FALSE(ch.try_receive().has_value());   // queue empty now
}

TEST(esp_async_wifi_channel_queue, overflow_drops_excess_packets) {
    // Use QueueDepth=3: capacity is 2 usable slots (one always kept empty).
    // Inject 3 packets -- the third must be silently dropped.
    using small_channel = esp_async_wifi_channel<test_packet, 3>;

    AsyncServer   server;
    AsyncClient   client;
    small_channel ch{server};
    server.simulate_connect(&client);

    const test_packet pkt_1 = make_sealed(header_type::data);
    const test_packet pkt_2 = make_sealed(header_type::control);
    const test_packet pkt_3 = make_sealed(header_type::data);

    inject(client, pkt_1);
    inject(client, pkt_2);
    inject(client, pkt_3);   // must be dropped -- queue is full

    const auto result_1 = ch.try_receive();
    ASSERT_TRUE(result_1.has_value());
    EXPECT_EQ(std::memcmp(&result_1.value(), &pkt_1, sizeof(test_packet)), 0);

    const auto result_2 = ch.try_receive();
    ASSERT_TRUE(result_2.has_value());
    EXPECT_EQ(std::memcmp(&result_2.value(), &pkt_2, sizeof(test_packet)), 0);

    // pkt_3 was dropped.
    EXPECT_FALSE(ch.try_receive().has_value());
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_returns_nullopt_after_disconnect) {
    client.simulate_disconnect();
    EXPECT_FALSE(ch.try_receive().has_value());
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

    const auto result = ch.try_receive();
    ASSERT_TRUE(result.has_value());
    // Must be pkt2, not a hybrid of the partial pkt and pkt2.
    EXPECT_EQ(std::memcmp(&result.value(), &pkt2, sizeof(test_packet)), 0);
}

TEST_F(fixture, new_connection_accepted_after_disconnect) {
    client.simulate_disconnect();

    AsyncClient client2;
    server.simulate_connect(&client2);

    const test_packet pkt = make_sealed();
    inject(client2, pkt);

    EXPECT_TRUE(ch.try_receive().has_value());
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

    const test_packet pkt = make_sealed();
    client2.simulate_data(&pkt, sizeof(test_packet));

    EXPECT_FALSE(ch.try_receive().has_value());
}

// ---------------------------------------------------------------------------
// Disconnect -- queued packets still available
// ---------------------------------------------------------------------------

TEST_F(fixture, queued_packets_still_available_after_disconnect) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);
    client.simulate_disconnect();

    const auto result = ch.try_receive();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt, sizeof(test_packet)), 0);
}

// ---------------------------------------------------------------------------
// Disconnect -- staging reset: last byte of previous connection must not
// bleed into a packet from the new connection
// ---------------------------------------------------------------------------

TEST_F(fixture, staging_reset_prevents_bleed_across_connections) {
    const test_packet pkt_a = make_sealed(header_type::data);
    const test_packet pkt_b = make_sealed(header_type::control);

    // Deliver sizeof(Packet)-1 bytes of pkt_a: one byte short of a complete packet.
    client.simulate_data(&pkt_a, sizeof(test_packet) - 1);
    client.simulate_disconnect();

    // Reconnect and deliver a full fresh packet.
    AsyncClient client2;
    server.simulate_connect(&client2);
    inject(client2, pkt_b);

    const auto result = ch.try_receive();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt_b, sizeof(test_packet)), 0);

    EXPECT_FALSE(ch.try_receive().has_value());
}

// ---------------------------------------------------------------------------
// send after disconnect -- no crash, no bytes written
// ---------------------------------------------------------------------------

TEST_F(fixture, send_after_disconnect_does_not_crash) {
    client.simulate_disconnect();

    test_packet pkt{header_type::data, header_options::none};
    EXPECT_EQ(ch.send(pkt), send_result::ok);
    EXPECT_EQ(client.tx.size(), 0u);
}

// ---------------------------------------------------------------------------
// Fragmentation edge cases
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_reassembles_last_byte_separate) {
    const test_packet pkt = make_sealed();
    const auto* raw = reinterpret_cast<const std::byte*>(&pkt);

    client.simulate_data(raw, sizeof(test_packet) - 1);
    EXPECT_FALSE(ch.try_receive().has_value());

    client.simulate_data(raw + sizeof(test_packet) - 1, 1);
    const auto result = ch.try_receive();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt, sizeof(test_packet)), 0);
}

TEST_F(fixture, try_receive_handles_three_packets_in_one_delivery) {
    const test_packet pkt_a = make_sealed(header_type::data);
    const test_packet pkt_b = make_sealed(header_type::control);
    const test_packet pkt_c = make_sealed(header_type::data);

    std::vector<std::byte> buf(3 * sizeof(test_packet));
    std::memcpy(buf.data(),                          &pkt_a, sizeof(test_packet));
    std::memcpy(buf.data() +     sizeof(test_packet), &pkt_b, sizeof(test_packet));
    std::memcpy(buf.data() + 2 * sizeof(test_packet), &pkt_c, sizeof(test_packet));

    client.simulate_data(buf.data(), buf.size());

    const auto ra = ch.try_receive();
    const auto rb = ch.try_receive();
    const auto rc = ch.try_receive();

    ASSERT_TRUE(ra.has_value());
    ASSERT_TRUE(rb.has_value());
    ASSERT_TRUE(rc.has_value());
    EXPECT_EQ(std::memcmp(&ra.value(), &pkt_a, sizeof(test_packet)), 0);
    EXPECT_EQ(std::memcmp(&rb.value(), &pkt_b, sizeof(test_packet)), 0);
    EXPECT_EQ(std::memcmp(&rc.value(), &pkt_c, sizeof(test_packet)), 0);
}

// ---------------------------------------------------------------------------
// Ring queue -- boundary conditions
// ---------------------------------------------------------------------------

TEST(esp_async_wifi_channel_queue, drain_after_filling_to_capacity) {
    // QueueDepth=3 => 2 usable slots. Fill, drain, verify empty.
    using small_channel = esp_async_wifi_channel<test_packet, 3>;

    AsyncServer   server;
    AsyncClient   client;
    small_channel ch{server};
    server.simulate_connect(&client);

    const test_packet pkt_a = make_sealed(header_type::data);
    const test_packet pkt_b = make_sealed(header_type::control);

    inject(client, pkt_a);
    inject(client, pkt_b);

    EXPECT_TRUE(ch.try_receive().has_value());
    EXPECT_TRUE(ch.try_receive().has_value());
    EXPECT_FALSE(ch.try_receive().has_value());
}

TEST(esp_async_wifi_channel_queue, coalesced_delivery_that_fills_queue_drops_excess) {
    // QueueDepth=3 => 2 usable slots. Deliver 3 packets coalesced -- third dropped.
    using small_channel = esp_async_wifi_channel<test_packet, 3>;

    AsyncServer   server;
    AsyncClient   client;
    small_channel ch{server};
    server.simulate_connect(&client);

    const test_packet pkt_a = make_sealed(header_type::data);
    const test_packet pkt_b = make_sealed(header_type::control);
    const test_packet pkt_c = make_sealed(header_type::data);

    std::vector<std::byte> buf(3 * sizeof(test_packet));
    std::memcpy(buf.data(),                          &pkt_a, sizeof(test_packet));
    std::memcpy(buf.data() +     sizeof(test_packet), &pkt_b, sizeof(test_packet));
    std::memcpy(buf.data() + 2 * sizeof(test_packet), &pkt_c, sizeof(test_packet));

    client.simulate_data(buf.data(), buf.size());

    const auto ra = ch.try_receive();
    const auto rb = ch.try_receive();

    ASSERT_TRUE(ra.has_value());
    ASSERT_TRUE(rb.has_value());
    EXPECT_EQ(std::memcmp(&ra.value(), &pkt_a, sizeof(test_packet)), 0);
    EXPECT_EQ(std::memcmp(&rb.value(), &pkt_b, sizeof(test_packet)), 0);
    EXPECT_FALSE(ch.try_receive().has_value());
}

TEST(esp_async_wifi_channel_queue, queue_recovers_after_overflow_and_drain) {
    // Fill queue, drain, then verify new packets are accepted normally.
    using small_channel = esp_async_wifi_channel<test_packet, 3>;

    AsyncServer   server;
    AsyncClient   client;
    small_channel ch{server};
    server.simulate_connect(&client);

    const test_packet filler = make_sealed(header_type::data);
    inject(client, filler);
    inject(client, filler);
    inject(client, filler);   // dropped -- queue full

    // Drain.
    EXPECT_TRUE(ch.try_receive().has_value());
    EXPECT_TRUE(ch.try_receive().has_value());
    EXPECT_FALSE(ch.try_receive().has_value());

    // Now inject again -- should be accepted.
    const test_packet fresh = make_sealed(header_type::control);
    inject(client, fresh);
    const auto result = ch.try_receive();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &fresh, sizeof(test_packet)), 0);
}
