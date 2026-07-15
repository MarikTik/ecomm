// SPDX-License-Identifier: MIT
/**
* @file test_hub.cpp
*
* @brief Unit tests for ecomm::hub.
*
* @ingroup ecomm_tests
*
* Uses the same software-only `mock_impl<Packet>` transport pattern as
* test_reliable_channel.cpp (independent rx/tx byte queues, no hardware
* headers needed) so hub can be exercised against several distinct channel
* instances -- and, in the mixed-transport tests, against a real
* `reliable_channel` wrapping one -- without any mocked-out ecomm code.
*
* Coverage:
*   construction
*     - Class template argument deduction (CTAD) works: `hub h{ch0, ch1};`
*       deduces Channels... directly from the constructor arguments.
*     - All channels start enabled for both sending and receiving.
*
*   send
*     - Fans out to every active sender whose packet_t matches the argument's
*       type; each engaged result is send_result::ok for a plain channel.
*     - Packet is deduced from send's argument -- no explicit template
*       argument needed.
*     - In a heterogeneous hub, send<Packet>() only reaches channels whose
*       packet_t matches; a channel operating on a different packet type has
*       no result slot at all.
*     - remove_sender<T>() excludes exactly that channel from subsequent sends
*       (its result slot becomes disengaged); use_sender<T>() re-includes it.
*     - Results are positional: slot i corresponds to the i-th matching
*       channel in Channels..., regardless of which channels are currently active.
*     - A mixed hub (plain channel + reliable_channel) reports ok for the
*       plain channel and timeout for the reliable channel when nothing acks
*       it -- proving hub's validation is duck-typed, not inheritance-based
*       (reliable_channel does not derive from channel<>).
*
*   try_receive<Packet>
*     - Packet must be named explicitly (nothing to deduce a return type from).
*     - Returns nullopt when no active matching receiver has data.
*     - Returns the packet from whichever matching channel has one.
*     - When multiple matching channels have data simultaneously, the first
*       one in Channels... declaration order wins (documented, not
*       starvation-fixed).
*     - remove_receiver<T>() excludes exactly that channel; use_receiver<T>()
*       re-includes it.
*     - In a heterogeneous hub, only channels whose packet_t matches Packet
*       are polled.
*
*   try_receive_any
*     - Returns nullopt when nothing is available anywhere.
*     - Polls every active receiver regardless of packet type and returns the
*       first hit as any_packet_t, consumable via std::visit + ecomm::overload.
*
*   compile-time validation (not runtime-tested, same convention as
*   test_reliable_channel.cpp's static_assert coverage note)
*     - Duplicate channel types and a type missing send/try_receive fail to
*       compile hub<...>'s own static_asserts. Verified by inspection / the
*       hub.hpp header's own assertions, not by a runtime test here.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-07-14
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-07-14 Initial creation, alongside the hub rewrite.
* - 2026-07-15 Rewritten for the heterogeneous hub<Channels...>: send<Packet> is
*      deduced, try_receive<Packet> requires an explicit template argument, and
*      packet-type filtering plus try_receive_any() are now exercised with a
*      genuinely heterogeneous hub (two distinct packet types).
*/

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>
#include <variant>

#include <ecomm/hub/hub.hpp>
#include <ecomm/channels/channel.hpp>
#include <ecomm/channels/reliable_channel.hpp>
#include <ecomm/protocol/packet.hpp>
#include <ecomm/protocol/checksum.hpp>
#include <ecomm/protocol/validator.hpp>

using namespace ecomm;
using namespace ecomm::protocol;
using namespace ecomm::channels;

// ---------------------------------------------------------------------------
// Packet types
// ---------------------------------------------------------------------------

using p2p_packet = packet<16, topology::point_to_point, no_sequence, crc16>;
using seq_packet = packet<16, topology::point_to_point, sequenced, crc16>;
// A second, distinct packet type -- larger payload, different checksum --
// used to prove hub can combine channels operating on different packet types.
using big_packet = packet<48, topology::point_to_point, no_sequence, crc32>;

// ---------------------------------------------------------------------------
// mock_impl: software transport, identical pattern to test_reliable_channel.cpp
// ---------------------------------------------------------------------------

template<typename Packet, int Tag = 0>
struct mock_impl : channel<mock_impl<Packet, Tag>, Packet> {
    std::deque<std::byte> rx;
    std::vector<std::byte> tx;

    void do_send(const Packet& packet) noexcept {
        const auto* raw = reinterpret_cast<const std::byte*>(&packet);
        for (std::size_t i = 0; i < sizeof(Packet); ++i)
            tx.push_back(raw[i]);
    }

    bool do_try_receive(Packet& out) noexcept {
        if (rx.size() < sizeof(Packet)) return false;
        auto* raw = reinterpret_cast<std::byte*>(&out);
        for (std::size_t i = 0; i < sizeof(Packet); ++i) {
            raw[i] = rx.front();
            rx.pop_front();
        }
        return true;
    }

    void inject(const Packet& pkt) {
        const auto* raw = reinterpret_cast<const std::byte*>(&pkt);
        for (std::size_t i = 0; i < sizeof(Packet); ++i)
            rx.push_back(raw[i]);
    }

    std::size_t tx_packet_count() const { return tx.size() / sizeof(Packet); }
};

// mock_clock: identical pattern to test_reliable_channel.cpp. Never produces
// an ack on its own -- used only to prove a reliable_channel inside a hub
// correctly reports send_result::timeout when nothing acks it.
struct mock_clock {
    using tick_type = std::uint32_t;
    static tick_type _now;
    static tick_type now() noexcept { return _now++; }
    static tick_type timeout_ticks() noexcept { return 5u; }
};
mock_clock::tick_type mock_clock::_now = 0;

static p2p_packet make_sealed_p2p() {
    p2p_packet p{header_type::data, header_options::none};
    for (std::size_t i = 0; i < p2p_packet::payload_size; ++i)
        p.payload[i] = static_cast<std::byte>(0xA5 ^ i);
    validator<p2p_packet>{}.seal(p);
    return p;
}

static big_packet make_sealed_big() {
    big_packet p{header_type::data, header_options::none};
    for (std::size_t i = 0; i < big_packet::payload_size; ++i)
        p.payload[i] = static_cast<std::byte>(0x5A ^ i);
    validator<big_packet>{}.seal(p);
    return p;
}

// ---------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------

TEST(hub_construction, ctad_deduces_channels_from_constructor_args) {
    mock_impl<p2p_packet, 0> ch0;
    mock_impl<p2p_packet, 1> ch1;

    hub h{ch0, ch1};
    static_assert(std::is_same_v<decltype(h), hub<mock_impl<p2p_packet, 0>, mock_impl<p2p_packet, 1>>>);
    SUCCEED();
}

TEST(hub_construction, all_channels_enabled_by_default) {
    mock_impl<p2p_packet, 0> ch0;
    mock_impl<p2p_packet, 1> ch1;
    hub h{ch0, ch1};

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);
    EXPECT_TRUE(results[0].has_value());
    EXPECT_TRUE(results[1].has_value());
}

// ---------------------------------------------------------------------------
// send
// ---------------------------------------------------------------------------

TEST(hub_send, fans_out_to_all_active_senders_with_ok) {
    mock_impl<p2p_packet, 0> ch0;
    mock_impl<p2p_packet, 1> ch1;
    hub h{ch0, ch1};

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);  // Packet deduced from pkt

    ASSERT_TRUE(results[0].has_value());
    ASSERT_TRUE(results[1].has_value());
    EXPECT_EQ(*results[0], send_result::ok);
    EXPECT_EQ(*results[1], send_result::ok);
    EXPECT_EQ(ch0.tx_packet_count(), 1u);
    EXPECT_EQ(ch1.tx_packet_count(), 1u);
}

TEST(hub_send, remove_sender_excludes_exactly_that_channel) {
    mock_impl<p2p_packet, 0> ch0;
    mock_impl<p2p_packet, 1> ch1;
    hub h{ch0, ch1};

    h.remove_sender<mock_impl<p2p_packet, 0>>();

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);

    EXPECT_FALSE(results[0].has_value());  // slot 0: skipped, no result
    ASSERT_TRUE(results[1].has_value());
    EXPECT_EQ(*results[1], send_result::ok);
    EXPECT_EQ(ch0.tx_packet_count(), 0u);
    EXPECT_EQ(ch1.tx_packet_count(), 1u);
}

TEST(hub_send, use_sender_reenables_a_removed_channel) {
    mock_impl<p2p_packet, 0> ch0;
    mock_impl<p2p_packet, 1> ch1;
    hub h{ch0, ch1};

    h.remove_sender<mock_impl<p2p_packet, 0>>();
    h.use_sender<mock_impl<p2p_packet, 0>>();

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);

    EXPECT_TRUE(results[0].has_value());
    EXPECT_EQ(ch0.tx_packet_count(), 1u);
}

TEST(hub_send, results_are_positional_regardless_of_active_set) {
    mock_impl<p2p_packet, 0> ch0;
    mock_impl<p2p_packet, 1> ch1;
    mock_impl<p2p_packet, 2> ch2;
    hub h{ch0, ch1, ch2};

    h.remove_sender<mock_impl<p2p_packet, 1>>();  // disable the middle one

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);

    EXPECT_TRUE(results[0].has_value());
    EXPECT_FALSE(results[1].has_value());
    EXPECT_TRUE(results[2].has_value());
}

TEST(hub_send, mixed_plain_and_reliable_channel_reports_distinct_results) {
    mock_impl<seq_packet, 0> plain_impl;
    mock_impl<seq_packet, 1> reliable_impl;
    reliable_channel<mock_impl<seq_packet, 1>, seq_packet, mock_clock, 2, 1> reliable{reliable_impl};

    mock_clock::_now = 0;
    hub h{plain_impl, reliable};

    seq_packet pkt{header_type::data, header_options::none};
    auto results = h.send(pkt);

    ASSERT_TRUE(results[0].has_value());
    ASSERT_TRUE(results[1].has_value());
    EXPECT_EQ(*results[0], send_result::ok);       // plain channel: always ok
    EXPECT_EQ(*results[1], send_result::timeout);  // reliable channel: never acked
}

// ---------------------------------------------------------------------------
// send -- heterogeneous packet types
// ---------------------------------------------------------------------------

TEST(hub_send_heterogeneous, only_reaches_channels_with_matching_packet_t) {
    mock_impl<p2p_packet, 0> small_ch;
    mock_impl<big_packet, 0> big_ch;
    hub h{small_ch, big_ch};

    p2p_packet small_pkt = make_sealed_p2p();
    auto small_results = h.send(small_pkt);  // Packet deduced as p2p_packet
    static_assert(std::tuple_size_v<decltype(small_results)> == 1);
    ASSERT_TRUE(small_results[0].has_value());
    EXPECT_EQ(*small_results[0], send_result::ok);
    EXPECT_EQ(small_ch.tx_packet_count(), 1u);
    EXPECT_EQ(big_ch.tx_packet_count(), 0u);  // untouched: different packet_t

    big_packet big_pkt = make_sealed_big();
    auto big_results = h.send(big_pkt);  // Packet deduced as big_packet
    static_assert(std::tuple_size_v<decltype(big_results)> == 1);
    ASSERT_TRUE(big_results[0].has_value());
    EXPECT_EQ(*big_results[0], send_result::ok);
    EXPECT_EQ(big_ch.tx_packet_count(), 1u);
    EXPECT_EQ(small_ch.tx_packet_count(), 1u);  // unchanged by the second send
}

// ---------------------------------------------------------------------------
// try_receive<Packet>
// ---------------------------------------------------------------------------

TEST(hub_try_receive, returns_nullopt_when_nothing_available) {
    mock_impl<p2p_packet, 0> ch0;
    mock_impl<p2p_packet, 1> ch1;
    hub h{ch0, ch1};

    EXPECT_FALSE(h.try_receive<p2p_packet>().has_value());
}

TEST(hub_try_receive, returns_packet_from_whichever_channel_has_one) {
    mock_impl<p2p_packet, 0> ch0;
    mock_impl<p2p_packet, 1> ch1;
    hub h{ch0, ch1};

    const p2p_packet pkt = make_sealed_p2p();
    ch1.inject(pkt);

    const auto result = h.try_receive<p2p_packet>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt, sizeof(p2p_packet)), 0);
}

TEST(hub_try_receive, first_channel_in_declaration_order_wins_when_both_have_data) {
    mock_impl<p2p_packet, 0> ch0;
    mock_impl<p2p_packet, 1> ch1;
    hub h{ch0, ch1};

    p2p_packet pkt0 = make_sealed_p2p();
    pkt0.payload[0] = std::byte{0x11};
    validator<p2p_packet>{}.seal(pkt0);

    p2p_packet pkt1 = make_sealed_p2p();
    pkt1.payload[0] = std::byte{0x22};
    validator<p2p_packet>{}.seal(pkt1);

    ch0.inject(pkt0);
    ch1.inject(pkt1);

    const auto result = h.try_receive<p2p_packet>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->payload[0], std::byte{0x11});  // ch0 polled first

    // ch1's packet is still queued for a later call.
    const auto second = h.try_receive<p2p_packet>();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->payload[0], std::byte{0x22});
}

TEST(hub_try_receive, remove_receiver_excludes_exactly_that_channel) {
    mock_impl<p2p_packet, 0> ch0;
    mock_impl<p2p_packet, 1> ch1;
    hub h{ch0, ch1};

    h.remove_receiver<mock_impl<p2p_packet, 0>>();

    const p2p_packet pkt = make_sealed_p2p();
    ch0.inject(pkt);  // only the disabled receiver has data

    EXPECT_FALSE(h.try_receive<p2p_packet>().has_value());
}

TEST(hub_try_receive, use_receiver_reenables_a_removed_channel) {
    mock_impl<p2p_packet, 0> ch0;
    mock_impl<p2p_packet, 1> ch1;
    hub h{ch0, ch1};

    h.remove_receiver<mock_impl<p2p_packet, 0>>();
    h.use_receiver<mock_impl<p2p_packet, 0>>();

    const p2p_packet pkt = make_sealed_p2p();
    ch0.inject(pkt);

    EXPECT_TRUE(h.try_receive<p2p_packet>().has_value());
}

TEST(hub_try_receive_heterogeneous, only_polls_channels_with_matching_packet_t) {
    mock_impl<p2p_packet, 0> small_ch;
    mock_impl<big_packet, 0> big_ch;
    hub h{small_ch, big_ch};

    const big_packet pkt = make_sealed_big();
    big_ch.inject(pkt);

    // A big_packet is queued, but asking for p2p_packet must not see it.
    EXPECT_FALSE(h.try_receive<p2p_packet>().has_value());

    const auto result = h.try_receive<big_packet>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt, sizeof(big_packet)), 0);
}

// ---------------------------------------------------------------------------
// try_receive_any
// ---------------------------------------------------------------------------

TEST(hub_try_receive_any, returns_nullopt_when_nothing_available) {
    mock_impl<p2p_packet, 0> small_ch;
    mock_impl<big_packet, 0> big_ch;
    hub h{small_ch, big_ch};

    EXPECT_FALSE(h.try_receive_any().has_value());
}

TEST(hub_try_receive_any, finds_packet_from_a_small_channel) {
    mock_impl<p2p_packet, 0> small_ch;
    mock_impl<big_packet, 0> big_ch;
    hub h{small_ch, big_ch};

    const p2p_packet pkt = make_sealed_p2p();
    small_ch.inject(pkt);

    auto any = h.try_receive_any();
    ASSERT_TRUE(any.has_value());

    bool visited_small = false;
    std::visit(overload{
        [&](p2p_packet& p) {
            visited_small = true;
            EXPECT_EQ(std::memcmp(&p, &pkt, sizeof(p2p_packet)), 0);
        },
        [&](big_packet&) {
            FAIL() << "expected p2p_packet alternative";
        },
    }, *any);
    EXPECT_TRUE(visited_small);
}

TEST(hub_try_receive_any, finds_packet_from_a_big_channel) {
    mock_impl<p2p_packet, 0> small_ch;
    mock_impl<big_packet, 0> big_ch;
    hub h{small_ch, big_ch};

    const big_packet pkt = make_sealed_big();
    big_ch.inject(pkt);

    auto any = h.try_receive_any();
    ASSERT_TRUE(any.has_value());

    bool visited_big = false;
    std::visit(overload{
        [&](p2p_packet&) {
            FAIL() << "expected big_packet alternative";
        },
        [&](big_packet& p) {
            visited_big = true;
            EXPECT_EQ(std::memcmp(&p, &pkt, sizeof(big_packet)), 0);
        },
    }, *any);
    EXPECT_TRUE(visited_big);
}

TEST(hub_try_receive_any, skips_inactive_receivers) {
    mock_impl<p2p_packet, 0> small_ch;
    mock_impl<big_packet, 0> big_ch;
    hub h{small_ch, big_ch};

    h.remove_receiver<mock_impl<p2p_packet, 0>>();

    const p2p_packet pkt = make_sealed_p2p();
    small_ch.inject(pkt);  // only the disabled receiver has data

    EXPECT_FALSE(h.try_receive_any().has_value());
}
