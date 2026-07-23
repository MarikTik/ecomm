// SPDX-License-Identifier: MIT
/**
* @file test_hub.cpp
*
* @brief Unit tests for ecomm::hub.
*
* @ingroup ecomm_tests
*
* Uses two mock transport shapes, mirroring the two shapes `hub` actually
* recognizes (see hub.hpp's capability-based routing note):
*   - `flexible_mock<tag>`: templated `do_send`/`do_try_receive`, no
*     class-level packet type -- mirrors `arduino_serial_channel<>`. Accepts
*     any packet type per call.
*   - `fixed_mock<Packet, tag>`: ordinary, non-template `do_send`/
*     `do_try_receive`, fixed to one `Packet` -- mirrors the Impl a
*     `reliable_channel` wraps.
*
* For heterogeneous, handler-driven dispatch (`try_receive_any`/`on_channel`),
* see `test_router.cpp` -- a different type (`ecomm::router`), a different
* responsibility, its own test file.
*
* Coverage:
*   construction
*     - Class template argument deduction (CTAD) works: `hub h{ch0, ch1};`
*       deduces Channels... directly from the constructor arguments.
*     - All channels start at channels::role::transceiver (active for both
*       sending and receiving).
*
*   set_role<T>(channels::role)
*     - role::receiver excludes exactly that channel as a sender;
*       role::transceiver re-includes it.
*     - role::sender excludes exactly that channel as a receiver;
*       role::transceiver re-includes it.
*     - role::none excludes a channel from both send() and try_receive() at once.
*
*   send -- homogeneous (all channels flexible, same packet type)
*     - Fans out to every active sender; each engaged result is send_result::ok.
*     - Results are positional: slot i corresponds to the i-th matching
*       channel in Channels..., regardless of which channels are currently active.
*
*   send -- capability-based fan-out
*     - A flexible channel matches every packet type it's asked about, not
*       just "the one it was first used with" -- sending two distinct packet
*       types through the same hub reaches the same flexible channels both times.
*     - A hub mixing a flexible channel and a reliable_channel (fixed to one
*       packet type): sending the reliable_channel's own packet type reaches
*       both (ok from the flexible one, ok/timeout from the reliable one);
*       sending a packet type the reliable_channel doesn't support reaches
*       only the flexible channel -- proving routing is per-Packet capability,
*       not per-channel identity.
*
*   try_receive<Packet>
*     - Returns nullopt when no active matching receiver has data.
*     - Returns the packet from whichever matching channel has one.
*     - When multiple matching channels have data simultaneously, the first
*       one in Channels... declaration order wins (documented, not
*       starvation-fixed).
*
*   compile-time validation (not runtime-tested, same convention as
*   test_reliable_channel.cpp's static_assert coverage note)
*     - Duplicate channel types and a type recognizable as neither channel
*       shape fail to compile hub<...>'s own static_asserts. Verified by
*       inspection / the hub.hpp header's own assertions, not by a runtime
*       test here.
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
* - 2026-07-16 Rewritten again for capability-based routing: mock_impl<Packet,
*      Tag> split into flexible_mock<tag> (templated do_send/do_try_receive,
*      no class-level packet type) and fixed_mock<Packet, tag> (the shape
*      reliable_channel wraps), matching the two shapes hub.hpp now actually
*      recognizes. Added coverage for a flexible channel matching multiple
*      packet types and for capability-based routing across a mixed
*      flexible/reliable_channel hub. try_receive_any() -> try_receive_any<
*      Packets...>(), since a flexible channel has no enumerable packet-type
*      set of its own for hub to fall back on.
* - 2026-07-17 Updated for set_role<T>(hub_role) replacing use_sender/
*      use_receiver/remove_sender/remove_receiver: existing tests renamed and
*      switched to set_role calls; added a dedicated hub_set_role suite
*      covering hub_role::none (excluding a channel from both send and
*      receive at once) and restoring hub_role::transceiver afterward.
* - 2026-07-19 `try_receive_any`/`on_channel` coverage moved to the new
*      test_dispatcher.cpp, alongside `ecomm::hub` being split into `hub`
*      (this file) and `ecomm::dispatcher` (a different responsibility --
*      heterogeneous dispatch). `hub_role` -> `channels::role`
*      (`channels/role.hpp`); updated throughout.
* - 2026-07-20 Moved from tests/hub/ to tests/fabric/ alongside `ecomm::dispatcher`
*      being renamed to `ecomm::router` (test_dispatcher.cpp -> test_router.cpp);
*      only the include path in this file changed.
*/

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

#include <ecomm/fabric/hub.hpp>
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
// A second, distinct packet type -- larger payload, different checksum --
// used to prove a flexible channel matches more than just p2p_packet.
using big_packet = packet<48, topology::point_to_point, no_sequence, crc32>;
// Sequenced -- required by reliable_channel.
using seq_packet = packet<16, topology::point_to_point, sequenced, crc16>;

// ---------------------------------------------------------------------------
// flexible_mock: templated do_send/do_try_receive, no class-level packet
// type -- mirrors arduino_serial_channel<>. Accepts any Packet per call.
// ---------------------------------------------------------------------------

template<int Tag = 0>
struct flexible_mock : channel<flexible_mock<Tag>> {
    std::deque<std::byte> rx;
    std::vector<std::byte> tx;

    template<typename Packet>
    void do_send(const Packet& packet) noexcept {
        const auto* raw = reinterpret_cast<const std::byte*>(&packet);
        for (std::size_t i = 0; i < sizeof(Packet); ++i)
            tx.push_back(raw[i]);
    }

    template<typename Packet>
    bool do_try_receive(Packet& out) noexcept {
        if (rx.size() < sizeof(Packet)) return false;
        auto* raw = reinterpret_cast<std::byte*>(&out);
        for (std::size_t i = 0; i < sizeof(Packet); ++i) {
            raw[i] = rx.front();
            rx.pop_front();
        }
        return true;
    }

    template<typename Packet>
    void inject(const Packet& pkt) {
        const auto* raw = reinterpret_cast<const std::byte*>(&pkt);
        for (std::size_t i = 0; i < sizeof(Packet); ++i)
            rx.push_back(raw[i]);
    }

    template<typename Packet>
    std::size_t tx_packet_count() const { return tx.size() / sizeof(Packet); }
};

// ---------------------------------------------------------------------------
// fixed_mock: ordinary, non-template do_send/do_try_receive, fixed to one
// Packet -- the shape reliable_channel wraps.
// ---------------------------------------------------------------------------

template<typename Packet, int Tag = 0>
struct fixed_mock : channel<fixed_mock<Packet, Tag>> {
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
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;

    hub h{ch0, ch1};
    static_assert(std::is_same_v<decltype(h), hub<flexible_mock<0>, flexible_mock<1>>>);
    SUCCEED();
}

TEST(hub_construction, all_channels_enabled_by_default) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
    hub h{ch0, ch1};

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);
    EXPECT_TRUE(results[0].has_value());
    EXPECT_TRUE(results[1].has_value());
}

// ---------------------------------------------------------------------------
// set_role<T>(channels::role)
// ---------------------------------------------------------------------------

TEST(hub_set_role, none_excludes_a_channel_from_both_send_and_receive) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
    hub h{ch0, ch1};

    h.set_role<flexible_mock<0>>(role::none);

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);
    EXPECT_FALSE(results[0].has_value());
    EXPECT_TRUE(results[1].has_value());

    const p2p_packet injected = make_sealed_p2p();
    ch0.inject(injected);  // only the fully-disabled channel has data
    EXPECT_FALSE(h.try_receive<p2p_packet>().has_value());
}

TEST(hub_set_role, transceiver_restores_both_after_none) {
    flexible_mock<0> ch0;
    hub h{ch0};

    h.set_role<flexible_mock<0>>(role::none);
    h.set_role<flexible_mock<0>>(role::transceiver);

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);
    ASSERT_TRUE(results[0].has_value());
    EXPECT_EQ(*results[0], send_result::ok);

    ch0.inject(pkt);
    EXPECT_TRUE(h.try_receive<p2p_packet>().has_value());
}

// ---------------------------------------------------------------------------
// send -- homogeneous (all channels flexible, same packet type)
// ---------------------------------------------------------------------------

TEST(hub_send, fans_out_to_all_active_senders_with_ok) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
    hub h{ch0, ch1};

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);  // Packet deduced from pkt

    ASSERT_TRUE(results[0].has_value());
    ASSERT_TRUE(results[1].has_value());
    EXPECT_EQ(*results[0], send_result::ok);
    EXPECT_EQ(*results[1], send_result::ok);
    EXPECT_EQ(ch0.template tx_packet_count<p2p_packet>(), 1u);
    EXPECT_EQ(ch1.template tx_packet_count<p2p_packet>(), 1u);
}

TEST(hub_send, set_role_receiver_excludes_exactly_that_channel_as_sender) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
    hub h{ch0, ch1};

    h.set_role<flexible_mock<0>>(role::receiver);  // no longer a sender

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);

    EXPECT_FALSE(results[0].has_value());  // slot 0: skipped, no result
    ASSERT_TRUE(results[1].has_value());
    EXPECT_EQ(*results[1], send_result::ok);
    EXPECT_EQ(ch0.template tx_packet_count<p2p_packet>(), 0u);
    EXPECT_EQ(ch1.template tx_packet_count<p2p_packet>(), 1u);
}

TEST(hub_send, set_role_transceiver_reenables_a_channel_as_sender) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
    hub h{ch0, ch1};

    h.set_role<flexible_mock<0>>(role::receiver);   // no longer a sender
    h.set_role<flexible_mock<0>>(role::transceiver); // sender again

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);

    EXPECT_TRUE(results[0].has_value());
    EXPECT_EQ(ch0.template tx_packet_count<p2p_packet>(), 1u);
}

TEST(hub_send, results_are_positional_regardless_of_active_set) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
    flexible_mock<2> ch2;
    hub h{ch0, ch1, ch2};

    h.set_role<flexible_mock<1>>(role::receiver);  // disable the middle one as a sender

    p2p_packet pkt = make_sealed_p2p();
    auto results = h.send(pkt);

    EXPECT_TRUE(results[0].has_value());
    EXPECT_FALSE(results[1].has_value());
    EXPECT_TRUE(results[2].has_value());
}

// ---------------------------------------------------------------------------
// send -- capability-based fan-out
// ---------------------------------------------------------------------------

TEST(hub_send_capability, flexible_channel_matches_every_packet_type) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
    hub h{ch0, ch1};

    p2p_packet small_pkt = make_sealed_p2p();
    auto small_results = h.send(small_pkt);
    EXPECT_EQ(small_results.size(), 2u);

    big_packet big_pkt = make_sealed_big();
    auto big_results = h.send(big_pkt);   // same two channels, a different Packet
    EXPECT_EQ(big_results.size(), 2u);

    // tx now holds both a p2p_packet's and a big_packet's bytes back to back --
    // check the raw byte count directly rather than tx_packet_count<Packet>(),
    // which assumes a single packet type and would misreport once tx mixes types.
    EXPECT_EQ(ch0.tx.size(), sizeof(p2p_packet) + sizeof(big_packet));
    EXPECT_EQ(ch1.tx.size(), sizeof(p2p_packet) + sizeof(big_packet));
}

TEST(hub_send_capability, mixed_flexible_and_reliable_channel_routes_per_packet_type) {
    flexible_mock<0> flexible_ch;
    fixed_mock<seq_packet, 0> reliable_impl;
    reliable_channel<fixed_mock<seq_packet, 0>, seq_packet, mock_clock, 2, 1> reliable{reliable_impl};

    mock_clock::_now = 0;
    hub h{flexible_ch, reliable};

    // seq_packet: both channels support it (flexible always does; reliable_channel is fixed to it).
    seq_packet seq_pkt{header_type::data, header_options::none};
    auto seq_results = h.send(seq_pkt);
    ASSERT_EQ(seq_results.size(), 2u);
    EXPECT_EQ(*seq_results[0], send_result::ok);       // flexible channel: always ok
    EXPECT_EQ(*seq_results[1], send_result::timeout);  // reliable channel: never acked

    // p2p_packet: only the flexible channel supports it -- reliable_channel is
    // fixed to seq_packet and has no slot at all for a different Packet.
    p2p_packet p2p_pkt = make_sealed_p2p();
    auto p2p_results = h.send(p2p_pkt);
    ASSERT_EQ(p2p_results.size(), 1u);
    EXPECT_EQ(*p2p_results[0], send_result::ok);
}

// ---------------------------------------------------------------------------
// try_receive<Packet>
// ---------------------------------------------------------------------------

TEST(hub_try_receive, returns_nullopt_when_nothing_available) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
    hub h{ch0, ch1};

    EXPECT_FALSE(h.try_receive<p2p_packet>().has_value());
}

TEST(hub_try_receive, returns_packet_from_whichever_channel_has_one) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
    hub h{ch0, ch1};

    const p2p_packet pkt = make_sealed_p2p();
    ch1.inject(pkt);

    const auto result = h.try_receive<p2p_packet>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::memcmp(&result.value(), &pkt, sizeof(p2p_packet)), 0);
}

TEST(hub_try_receive, first_channel_in_declaration_order_wins_when_both_have_data) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
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

TEST(hub_try_receive, set_role_sender_excludes_exactly_that_channel_as_receiver) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
    hub h{ch0, ch1};

    h.set_role<flexible_mock<0>>(role::sender);  // no longer a receiver

    const p2p_packet pkt = make_sealed_p2p();
    ch0.inject(pkt);  // only the disabled receiver has data

    EXPECT_FALSE(h.try_receive<p2p_packet>().has_value());
}

TEST(hub_try_receive, set_role_transceiver_reenables_a_channel_as_receiver) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;
    hub h{ch0, ch1};

    h.set_role<flexible_mock<0>>(role::sender);       // no longer a receiver
    h.set_role<flexible_mock<0>>(role::transceiver);  // receiver again

    const p2p_packet pkt = make_sealed_p2p();
    ch0.inject(pkt);

    EXPECT_TRUE(h.try_receive<p2p_packet>().has_value());
}

TEST(hub_try_receive_capability, flexible_channel_receives_either_packet_type) {
    // Inject both packets back to back and receive each with its own,
    // correct type in order -- proving one flexible channel instance
    // handles two distinct packet types across sequential calls. (Trying a
    // *wrong* type against a queued packet is deliberately not exercised
    // here: try_receive<Packet>() always consumes sizeof(Packet) bytes once
    // that many are available, valid or not, so a smaller wrong-type guess
    // would consume and discard real, valid data -- see router.hpp's
    // warning for the general version of this.)
    flexible_mock<0> ch0;
    hub h{ch0};

    const p2p_packet small_pkt = make_sealed_p2p();
    const big_packet big_pkt = make_sealed_big();
    ch0.inject(small_pkt);
    ch0.inject(big_pkt);

    const auto result_small = h.try_receive<p2p_packet>();
    ASSERT_TRUE(result_small.has_value());
    EXPECT_EQ(std::memcmp(&result_small.value(), &small_pkt, sizeof(p2p_packet)), 0);

    const auto result_big = h.try_receive<big_packet>();
    ASSERT_TRUE(result_big.has_value());
    EXPECT_EQ(std::memcmp(&result_big.value(), &big_pkt, sizeof(big_packet)), 0);
}
