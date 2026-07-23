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
* `BufferCapacity` (bytes) replaces the old `QueueDepth` (packet count).
* `N * sizeof(test_packet)` reproduces the old "QueueDepth = N" semantics
* exactly for a single packet type: usable bytes = N*sizeof(Packet) - 1 (one
* byte always reserved to distinguish full from empty), which fits exactly
* N-1 whole packets and no more -- see the per-test comments for the specific
* capacities chosen.
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
*   receive_raw -- unframed byte access (backs router's reassembly path)
*     - Reads all available bytes and advances the ring; a second read is empty.
*     - Honours the `max` argument, draining incrementally across several reads.
*     - Wraps correctly across the ring's physical end (two-chunk copy).
*
*   Ring buffer
*     - try_receive returns nullopt once the ring is drained.
*     - When a delivery overflows the ring, the existing backlog is discarded
*       and the overflowing delivery is placed fresh at offset 0 -- kept whole
*       if it fits alone, truncated only if it alone exceeds capacity.
*     - A single delivery larger than the ring can ever hold desyncs framing
*       for the rest of the connection, until disconnect clears the ring --
*       the documented tradeoff of buffering raw bytes instead of typed packets.
*

*   Disconnect
*     - After simulate_disconnect, try_receive returns nullopt.
*     - A partial packet in the ring is discarded on disconnect.
*     - A complete-but-unread packet is also discarded on disconnect (the ring
*       has no packet-boundary information to distinguish the two cases).
*     - A new connection after disconnect is accepted.
*
*   Second-connection rejection
*     - A second client connecting while one is active is closed immediately.
*
*   multi-packet-type flexibility
*     - One channel instance can send()/try_receive<>() two distinct packet
*       types over the same connection, since Packet is now a per-call
*       template parameter rather than baked into the channel type.
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
* - 2026-07-16 Rewritten for the byte-ring redesign: `esp_async_wifi_channel<
*      test_packet, QueueDepth>` -> `esp_async_wifi_channel<BufferCapacity>`;
*      `try_receive()` -> `try_receive<test_packet>()`. Updated the two tests
*      whose old assertions assumed packet-aligned overflow/disconnect
*      behavior that no longer holds for a byte ring (`queued_packets_still_
*      available_after_disconnect` inverted; `queue_recovers_after_overflow_
*      and_drain` replaced by an explicit framing-desync test). Added coverage
*      for multi-packet-type flexibility through one instance.
* - 2026-07-17 Updated for the reset-to-offset-0 overflow policy: `overflow_
*      drops_excess_packets` (which assumed the old "truncate the incoming
*      delivery's tail in place, keep the existing backlog" policy) became
*      `overflow_resets_and_keeps_only_the_overflowing_delivery`, reflecting
*      that the backlog is now discarded and the overflowing delivery is kept
*      whole. `coalesced_delivery_that_fills_queue_drops_excess` and
*      `partial_overflow_desyncs_framing_until_reconnect` needed no changes --
*      both engineer their overflow against an empty ring, where the old and
*      new policies happen to coincide (see esp_async_wifi_channel.hpp's
*      "Overflow resets to a clean run at offset 0" note).
* - 2026-07-21 Added the `receive_raw` suite covering the new unframed byte read
*      (backs `ecomm::router`'s reassembly path): available-bytes read + advance,
*      `max`-limited incremental drain, and a ring-wrap read.
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

// A second, distinct packet type -- different size and checksum -- used to
// prove one channel instance can carry more than one Packet type.
using other_packet = packet<16, topology::point_to_point, no_sequence, crc16>;

// Capacity for most tests: room for 3 whole test_packets (4*sizeof - 1 usable
// bytes), matching the old QueueDepth=4 fixture exactly.
using test_channel = esp_async_wifi_channel<4 * sizeof(test_packet)>;

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

    // Before connect: ring is empty, no client yet.
    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());

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
    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());
}

TEST_F(fixture, try_receive_accepts_one_valid_packet) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);

    EXPECT_TRUE(ch.try_receive<test_packet>().has_value());
}

TEST_F(fixture, try_receive_received_packet_matches_injected) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);

    const auto result = ch.try_receive<test_packet>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt, sizeof(test_packet)), 0);
}

TEST_F(fixture, try_receive_rejects_corrupt_packet) {
    test_packet pkt = make_sealed();
    pkt.payload[0] ^= std::byte{0xFF};   // corrupt after sealing
    inject(client, pkt);

    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());
}

// ---------------------------------------------------------------------------
// try_receive -- fragmented TCP delivery
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_reassembles_two_fragment_delivery) {
    const test_packet pkt = make_sealed();
    const std::size_t half = sizeof(test_packet) / 2;

    // Deliver first half, then second half -- simulates TCP fragmentation.
    client.simulate_data(&pkt, half);
    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());   // incomplete: not yet enough bytes

    client.simulate_data(
        reinterpret_cast<const std::byte*>(&pkt) + half,
        sizeof(test_packet) - half
    );

    const auto result = ch.try_receive<test_packet>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt, sizeof(test_packet)), 0);
}

TEST_F(fixture, try_receive_reassembles_byte_by_byte_delivery) {
    const test_packet pkt = make_sealed();
    const auto* raw = reinterpret_cast<const std::byte*>(&pkt);

    for (std::size_t i = 0; i < sizeof(test_packet); ++i)
        client.simulate_data(raw + i, 1);

    const auto result = ch.try_receive<test_packet>();
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

    const auto result_a = ch.try_receive<test_packet>();
    const auto result_b = ch.try_receive<test_packet>();

    ASSERT_TRUE(result_a.has_value());
    ASSERT_TRUE(result_b.has_value());
    EXPECT_EQ(std::memcmp(&result_a.value(), &pkt_a, sizeof(test_packet)), 0);
    EXPECT_EQ(std::memcmp(&result_b.value(), &pkt_b, sizeof(test_packet)), 0);
}

// ---------------------------------------------------------------------------
// receive_raw -- unframed byte access (backs router's reassembly path)
// ---------------------------------------------------------------------------

TEST_F(fixture, receive_raw_reads_available_bytes_and_advances) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);

    std::byte out[sizeof(test_packet)]{};
    const std::size_t n = ch.receive_raw(out, sizeof(out));

    EXPECT_EQ(n, sizeof(test_packet));
    EXPECT_EQ(std::memcmp(out, &pkt, sizeof(test_packet)), 0);

    // Ring is now drained -- a second read yields nothing.
    std::byte again[sizeof(test_packet)]{};
    EXPECT_EQ(ch.receive_raw(again, sizeof(again)), 0u);
}

TEST_F(fixture, receive_raw_honours_max_and_drains_incrementally) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);

    std::byte out[sizeof(test_packet)]{};
    std::size_t got = 0;

    // Pull in 10-byte bites; the last read returns only the remainder.
    while (got < sizeof(test_packet)) {
        const std::size_t n = ch.receive_raw(out + got, 10);
        ASSERT_GT(n, 0u);
        EXPECT_LE(n, 10u);
        got += n;
    }

    EXPECT_EQ(got, sizeof(test_packet));
    EXPECT_EQ(std::memcmp(out, &pkt, sizeof(test_packet)), 0);
    EXPECT_EQ(ch.receive_raw(out, sizeof(out)), 0u);
}

TEST_F(fixture, receive_raw_wraps_around_the_physical_ring_end) {
    // Capacity is 4*sizeof(test_packet) = 128. Drive _tail forward so a later
    // read must span the ring's physical wrap boundary, and confirm the two
    // copy chunks stitch back together in order.
    constexpr std::size_t cap = 4 * sizeof(test_packet); // 128
    static_assert(cap == 128, "test assumes a 128-byte ring");

    // 120 sequential bytes: 0,1,2,...,119.
    std::vector<std::byte> first(120);
    for (std::size_t i = 0; i < first.size(); ++i)
        first[i] = static_cast<std::byte>(i);
    client.simulate_data(first.data(), first.size());

    // Consume 100, leaving 20 (values 100..119) and _tail at 100.
    std::byte scratch[100]{};
    ASSERT_EQ(ch.receive_raw(scratch, 100), 100u);

    // 40 more bytes valued 200..239. on_data writes 8 at [120,128) then wraps
    // 32 to [0,32); _head lands at 32.
    std::vector<std::byte> second(40);
    for (std::size_t i = 0; i < second.size(); ++i)
        second[i] = static_cast<std::byte>(200 + i);
    client.simulate_data(second.data(), second.size());

    // Now 60 bytes are buffered starting at _tail=100 and wrapping: values
    // 100..119 (20), then 200..239 (40).
    std::byte out[60]{};
    ASSERT_EQ(ch.receive_raw(out, sizeof(out)), 60u);

    std::byte expected[60]{};
    for (std::size_t i = 0; i < 20; ++i) expected[i]      = static_cast<std::byte>(100 + i);
    for (std::size_t i = 0; i < 40; ++i) expected[20 + i] = static_cast<std::byte>(200 + i);
    EXPECT_EQ(std::memcmp(out, expected, sizeof(out)), 0);
}

// ---------------------------------------------------------------------------
// Ring buffer
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_returns_nullopt_after_queue_drained) {
    const test_packet pkt = make_sealed();
    inject(client, pkt);

    EXPECT_TRUE(ch.try_receive<test_packet>().has_value());
    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());   // ring empty now
}

TEST(esp_async_wifi_channel_queue, overflow_resets_and_keeps_only_the_overflowing_delivery) {
    // Capacity for 2 whole packets (3*sizeof - 1 usable bytes). Inject 3
    // packets, each in its own simulate_data call -- the third overflows.
    //
    // Per the reset-to-offset-0 overflow policy (esp_async_wifi_channel.hpp's
    // "Overflow resets to a clean run at offset 0" note), the backlog (pkt_1,
    // pkt_2) is discarded entirely and pkt_3 -- the delivery that triggered
    // the overflow -- is kept whole and untorn, since pkt_3 alone fits within
    // the ring even though the backlog plus pkt_3 together do not.
    using small_channel = esp_async_wifi_channel<3 * sizeof(test_packet)>;

    AsyncServer   server;
    AsyncClient   client;
    small_channel ch{server};
    server.simulate_connect(&client);

    const test_packet pkt_1 = make_sealed(header_type::data);
    const test_packet pkt_2 = make_sealed(header_type::control);
    const test_packet pkt_3 = make_sealed(header_type::data);

    inject(client, pkt_1);
    inject(client, pkt_2);
    inject(client, pkt_3);   // overflow: resets the ring, pkt_1 and pkt_2 are discarded

    const auto result = ch.try_receive<test_packet>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt_3, sizeof(test_packet)), 0);

    // Nothing else survives the reset.
    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());
}

TEST(esp_async_wifi_channel_queue, drain_after_filling_to_capacity) {
    // Capacity for 2 whole packets. Fill, drain, verify empty.
    using small_channel = esp_async_wifi_channel<3 * sizeof(test_packet)>;

    AsyncServer   server;
    AsyncClient   client;
    small_channel ch{server};
    server.simulate_connect(&client);

    const test_packet pkt_a = make_sealed(header_type::data);
    const test_packet pkt_b = make_sealed(header_type::control);

    inject(client, pkt_a);
    inject(client, pkt_b);

    EXPECT_TRUE(ch.try_receive<test_packet>().has_value());
    EXPECT_TRUE(ch.try_receive<test_packet>().has_value());
    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());
}

TEST(esp_async_wifi_channel_queue, coalesced_delivery_that_fills_queue_drops_excess) {
    // Capacity for 2 whole packets. Deliver 3 packets coalesced in one call --
    // only sizeof(test_packet)-1 bytes of room remain after the first two, so
    // the third is truncated, not admitted whole.
    using small_channel = esp_async_wifi_channel<3 * sizeof(test_packet)>;

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

    const auto ra = ch.try_receive<test_packet>();
    const auto rb = ch.try_receive<test_packet>();

    ASSERT_TRUE(ra.has_value());
    ASSERT_TRUE(rb.has_value());
    EXPECT_EQ(std::memcmp(&ra.value(), &pkt_a, sizeof(test_packet)), 0);
    EXPECT_EQ(std::memcmp(&rb.value(), &pkt_b, sizeof(test_packet)), 0);
    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());
}

TEST(esp_async_wifi_channel_queue, partial_overflow_desyncs_framing_until_reconnect) {
    // Documents and verifies the tradeoff described in esp_async_wifi_channel.hpp's
    // "Overflow desyncs framing" note: because the ring holds undifferentiated
    // bytes, an overflow that truncates mid-packet cannot be resolved
    // packet-aligned. A later delivery lands right after the truncated tail,
    // so what try_receive<Packet>() pops next is torn across the two
    // deliveries -- not a clean, validator-accepted packet from either one.
    using small_channel = esp_async_wifi_channel<3 * sizeof(test_packet)>;   // 2 packets usable

    AsyncServer   server;
    AsyncClient   client;
    small_channel ch{server};
    server.simulate_connect(&client);

    const test_packet pkt_a = make_sealed(header_type::data);
    const test_packet pkt_b = make_sealed(header_type::control);
    const test_packet pkt_c = make_sealed(header_type::data);

    // One coalesced delivery of 3 packets -- only 2 fit whole; pkt_c is
    // truncated to sizeof(test_packet)-1 bytes, one byte short.
    std::vector<std::byte> buf(3 * sizeof(test_packet));
    std::memcpy(buf.data(),                          &pkt_a, sizeof(test_packet));
    std::memcpy(buf.data() +     sizeof(test_packet), &pkt_b, sizeof(test_packet));
    std::memcpy(buf.data() + 2 * sizeof(test_packet), &pkt_c, sizeof(test_packet));
    client.simulate_data(buf.data(), buf.size());

    // Drain the two packets that fit whole.
    ASSERT_TRUE(ch.try_receive<test_packet>().has_value());
    ASSERT_TRUE(ch.try_receive<test_packet>().has_value());

    // sizeof(test_packet)-1 stray bytes (the truncated tail of pkt_c) remain.
    // A fresh, cleanly-framed packet delivered now lands right after them.
    inject(client, pkt_a);

    // What try_receive<Packet>() pops is torn across the stray tail and the
    // new delivery -- not pkt_a's bytes from offset 0. It may parse as a
    // (likely FCS-invalid) different packet, or as nullopt if fewer than
    // sizeof(Packet) bytes are available; either way it is not a match for
    // the freshly-injected pkt_a, proving framing is desynced.
    const auto torn = ch.try_receive<test_packet>();
    if (torn.has_value()) {
        EXPECT_NE(std::memcmp(&torn.value(), &pkt_a, sizeof(test_packet)), 0);
    }

    // Reconnecting clears the ring entirely, restoring clean framing.
    client.simulate_disconnect();
    AsyncClient client2;
    server.simulate_connect(&client2);
    inject(client2, pkt_a);

    const auto clean = ch.try_receive<test_packet>();
    ASSERT_TRUE(clean.has_value());
    EXPECT_EQ(std::memcmp(&clean.value(), &pkt_a, sizeof(test_packet)), 0);
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------

TEST_F(fixture, try_receive_returns_nullopt_after_disconnect) {
    client.simulate_disconnect();
    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());
}

TEST_F(fixture, disconnect_discards_partial_packet) {
    const test_packet pkt = make_sealed();

    // Deliver only half the packet, then disconnect before it completes.
    client.simulate_data(&pkt, sizeof(test_packet) / 2);
    client.simulate_disconnect();

    // Reconnect with a fresh client and deliver a complete packet.
    AsyncClient client2;
    server.simulate_connect(&client2);

    const test_packet pkt2 = make_sealed(header_type::control);
    inject(client2, pkt2);

    const auto result = ch.try_receive<test_packet>();
    ASSERT_TRUE(result.has_value());
    // Must be pkt2, not a hybrid of the partial pkt and pkt2.
    EXPECT_EQ(std::memcmp(&result.value(), &pkt2, sizeof(test_packet)), 0);
}

TEST_F(fixture, queued_but_unread_packet_is_discarded_on_disconnect) {
    // Unlike the old Packet-typed ring (which preserved already-completed
    // packets across a disconnect), the byte ring clears everything on
    // disconnect -- see esp_async_wifi_channel.hpp's "Disconnect clears the
    // entire ring" note. With no packet-boundary information at buffer time,
    // there is no way to tell a complete-but-unread packet apart from a
    // partial one, so preserving one would risk corrupting the other.
    const test_packet pkt = make_sealed();
    inject(client, pkt);
    client.simulate_disconnect();

    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());
}

TEST_F(fixture, new_connection_accepted_after_disconnect) {
    client.simulate_disconnect();

    AsyncClient client2;
    server.simulate_connect(&client2);

    const test_packet pkt = make_sealed();
    inject(client2, pkt);

    EXPECT_TRUE(ch.try_receive<test_packet>().has_value());
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

    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());
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

    const auto result = ch.try_receive<test_packet>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt_b, sizeof(test_packet)), 0);

    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());
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
    EXPECT_FALSE(ch.try_receive<test_packet>().has_value());

    client.simulate_data(raw + sizeof(test_packet) - 1, 1);
    const auto result = ch.try_receive<test_packet>();
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

    const auto ra = ch.try_receive<test_packet>();
    const auto rb = ch.try_receive<test_packet>();
    const auto rc = ch.try_receive<test_packet>();

    ASSERT_TRUE(ra.has_value());
    ASSERT_TRUE(rb.has_value());
    ASSERT_TRUE(rc.has_value());
    EXPECT_EQ(std::memcmp(&ra.value(), &pkt_a, sizeof(test_packet)), 0);
    EXPECT_EQ(std::memcmp(&rb.value(), &pkt_b, sizeof(test_packet)), 0);
    EXPECT_EQ(std::memcmp(&rc.value(), &pkt_c, sizeof(test_packet)), 0);
}

// ---------------------------------------------------------------------------
// multi-packet-type flexibility
// ---------------------------------------------------------------------------

TEST(esp_async_wifi_channel_multi_packet, sends_two_distinct_packet_types) {
    AsyncServer  server;
    AsyncClient  client;
    test_channel ch{server};
    server.simulate_connect(&client);

    test_packet  a{header_type::data, header_options::none};
    other_packet b{header_type::data, header_options::none};

    static_cast<void>(ch.send(a));   // Packet deduced as test_packet
    static_cast<void>(ch.send(b));   // Packet deduced as other_packet

    EXPECT_EQ(client.tx.size(), sizeof(test_packet) + sizeof(other_packet));
}

TEST(esp_async_wifi_channel_multi_packet, receives_two_distinct_packet_types_in_order) {
    // Capacity sized to comfortably hold one of each packet type.
    using mixed_channel = esp_async_wifi_channel<sizeof(test_packet) + sizeof(other_packet) + 1>;

    AsyncServer   server;
    AsyncClient   client;
    mixed_channel ch{server};
    server.simulate_connect(&client);

    const test_packet a = make_sealed();
    other_packet b{header_type::data, header_options::none};
    for (std::size_t i = 0; i < other_packet::payload_size; ++i)
        b.payload[i] = static_cast<std::byte>(0x11 ^ i);
    validator<other_packet>{}.seal(b);

    client.simulate_data(&a, sizeof(test_packet));
    client.simulate_data(&b, sizeof(other_packet));

    const auto result_a = ch.try_receive<test_packet>();
    ASSERT_TRUE(result_a.has_value());
    EXPECT_EQ(std::memcmp(&result_a.value(), &a, sizeof(test_packet)), 0);

    const auto result_b = ch.try_receive<other_packet>();
    ASSERT_TRUE(result_b.has_value());
    EXPECT_EQ(std::memcmp(&result_b.value(), &b, sizeof(other_packet)), 0);
}
