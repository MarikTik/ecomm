// SPDX-License-Identifier: MIT
/**
* @file test_router.cpp
*
* @brief Unit tests for ecomm::router.
*
* @ingroup ecomm_tests
*
* Uses `flexible_mock<tag>` (templated `do_send`/`do_try_receive`/`do_receive_raw`,
* no class-level packet type -- mirrors `arduino_serial_channel<>`) and
* `fixed_mock<Packet, tag>` (the shape `reliable_channel` wraps), the same
* pattern as `test_hub.cpp`.
*
* `router` is a stateful object now: it is constructed from its
* `on_channel(...)` groups (it owns a per-channel reassembly buffer that must
* persist across polls) and polled with the nullary `try_receive_any()`.
*
* Coverage:
*   single channel, single on_channel group
*     - Returns false when nothing is available.
*     - Dispatches a small (or large) packet to its handler.
*     - Handler declaration order does not affect which candidate frames first
*       (the ordering-hazard regression test -- see router.hpp's "largest-first"
*       note).
*     - A complete small packet is dispatched, not misframed, when a larger
*       candidate is also declared.
*     - Drains correctly in a `while` loop, both for one packet type and for
*       mixed sizes polled every iteration.
*
*   partial arrival / reassembly (the 2026-07-21 fix)
*     - A partially-arrived larger packet is NOT stolen by a smaller candidate's
*       byte-count probe: nothing is dispatched or destroyed until the whole
*       packet has arrived, then it frames correctly (this is the headline bug
*       the reassembly rewrite fixes).
*     - Bytes delivered one at a time still reassemble into the right packet.
*     - Three small packets whose bytes total a larger candidate's size drain as
*       three small packets -- prefix framing resolves what a byte-count probe
*       could not (this is what USED to be the "ambiguous backlog" limitation).
*     - After a run of unframable garbage bytes, the router resyncs and still
*       delivers a following valid packet.
*
*   multiple channels, multiple on_channel groups
*     - Each channel is framed only for its own group's packet types.
*     - First channel in group-declaration order wins when several have data.
*     - A channel not named in a router is never polled by it -- omission from
*       the router's groups is the only "disable" mechanism (router holds no
*       per-channel enable/disable flags, unlike `hub::set_role`).
*     - Works across channel shapes: a streaming channel and a reliable_channel
*       (message-atomic) can be named in the same router.
*
*   stress / long-term stability (fixed-seed, deterministic)
*     - Tens of thousands of packets of one type, drained each iteration and as
*       one large backlog: every one delivered once, in order.
*     - Mixed sizes on one channel with random backlog depths, and mixed sizes
*       arriving in random byte-sized fragments with random draining: exact
*       per-type order and counts hold across many buffer fill/drain cycles.
*     - Six channels (two multi-size) fed interleaved at volume stay
*       independently consistent.
*     - The real send() path round-trips at volume (seal -> wire -> reassemble).
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-07-19
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-07-19 Initial creation, split out of test_hub.cpp alongside `ecomm::hub`
*      being split into `hub` (explicit-`Packet` fan-out/poll) and
*      `ecomm::dispatcher` (heterogeneous, handler-driven dispatch). Converted
*      every test from the old `hub.try_receive_any(handlers...)` /
*      `on_channel<Channel>(handlers...)` (channel resolved from the hub's own
*      stored tuple) to `router{}.try_receive_any(on_channel(channel, handlers...))`
*      (channel passed directly, dispatcher holds nothing). Retired
*      `skips_inactive_receivers`/`respects_inactive_receivers` (dispatcher has
*      no active/inactive state) in favour of
*      `a_channel_not_named_in_the_call_is_never_polled`, which captures the same
*      guarantee the new way: omission from the call *is* the disable mechanism.
* - 2026-07-20 Moved from tests/hub/test_dispatcher.cpp to tests/fabric/test_router.cpp
*      alongside `ecomm::dispatcher` being renamed to `ecomm::router`; updated the
*      include path, `TEST` suite names (`dispatcher_single`/`dispatcher_multi_channel`
*      -> `router_single`/`router_multi_channel`), and every `dispatcher{}` call site
*      to `router{}`.
* - 2026-07-21 Rewritten for the stateful, reassembling `router`: every test now
*      constructs `router r{on_channel(...)}` and calls `r.try_receive_any()`
*      (was the stateless temporary `router{}.try_receive_any(on_channel(...))`),
*      and `flexible_mock` gained `do_receive_raw` + a partial-byte `inject_bytes`.
*      Added the partial-arrival / reassembly suite (the headline regression) and
*      turned the old `backlog_of_small_packets_is_ambiguous_with_a_larger_candidate`
*      -- which pinned the pre-fix destructive behaviour -- into
*      `backlog_of_small_packets_drains_as_small_packets`, which pins the fix.
*      Added the `router_stress` suite (fixed-seed, high-volume, mixed-size,
*      fragmented, multi-channel, and real-send-path round-trip) to exercise
*      long-term stability across many buffer fill/drain cycles.
*/

#include <gtest/gtest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <random>
#include <vector>

#include <ecomm/fabric/router.hpp>
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
// A second, distinct packet type -- larger payload, different checksum.
using big_packet = packet<48, topology::point_to_point, no_sequence, crc32>;
// Sequenced -- required by reliable_channel.
using seq_packet = packet<16, topology::point_to_point, sequenced, crc16>;

// ---------------------------------------------------------------------------
// flexible_mock: templated do_send/do_try_receive/do_receive_raw, no
// class-level packet type -- mirrors arduino_serial_channel<>. Accepts any
// Packet per call, and hands raw bytes to router's reassembly path.
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

    std::size_t do_receive_raw(std::byte* dst, std::size_t max) noexcept {
        const std::size_t n = rx.size() < max ? rx.size() : max;
        for (std::size_t i = 0; i < n; ++i) {
            dst[i] = rx.front();
            rx.pop_front();
        }
        return n;
    }

    // Inject a whole packet's worth of bytes.
    template<typename Packet>
    void inject(const Packet& pkt) {
        inject_bytes(pkt, 0, sizeof(Packet));
    }

    // Inject `count` bytes starting at byte offset `off` of `pkt` -- used to
    // simulate a packet arriving in fragments over a stream.
    template<typename Packet>
    void inject_bytes(const Packet& pkt, std::size_t off, std::size_t count) {
        const auto* raw = reinterpret_cast<const std::byte*>(&pkt);
        for (std::size_t i = 0; i < count; ++i)
            rx.push_back(raw[off + i]);
    }

    // Inject arbitrary raw (unframed, non-packet) bytes.
    void inject_raw(std::size_t count, std::byte value) {
        for (std::size_t i = 0; i < count; ++i)
            rx.push_back(value);
    }
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
// an ack on its own.
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
// Stress helpers
//
// Four distinct sizes, all crc32. Distinct sizes satisfy router's guardrail;
// crc32 (a 4-byte FCS) keeps the residual "a smaller candidate's prefix
// happens to validate against the leading bytes of a larger, still-arriving
// packet" false-positive rate at ~2^-32 -- negligible even over the tens of
// thousands of framing attempts these tests perform, so the fixed-seed runs
// below are deterministic.
// ---------------------------------------------------------------------------

using m16 = packet<16, topology::point_to_point, no_sequence, crc32>;
using m24 = packet<24, topology::point_to_point, no_sequence, crc32>;
using m32 = packet<32, topology::point_to_point, no_sequence, crc32>;
using m48 = packet<48, topology::point_to_point, no_sequence, crc32>;

// Write a 32-bit sequence number into the first four payload bytes and a
// seq-derived pattern into the rest, so every packet is unique and its
// ordering is verifiable on receipt. `fill` leaves it unsealed (for the send
// path, which seals); `stamp` seals it (for direct byte injection).
template<typename P>
static P fill(std::uint32_t seq) {
    P p{header_type::data, header_options::none};
    p.payload[0] = static_cast<std::byte>(seq & 0xFFu);
    p.payload[1] = static_cast<std::byte>((seq >> 8) & 0xFFu);
    p.payload[2] = static_cast<std::byte>((seq >> 16) & 0xFFu);
    p.payload[3] = static_cast<std::byte>((seq >> 24) & 0xFFu);
    for (std::size_t i = 4; i < P::payload_size; ++i)
        p.payload[i] = static_cast<std::byte>(static_cast<std::uint8_t>(seq * 2654435761u + i));
    return p;
}

template<typename P>
static P stamp(std::uint32_t seq) {
    P p = fill<P>(seq);
    validator<P>{}.seal(p);
    return p;
}

template<typename P>
static std::uint32_t seq_of(const P& p) {
    return  static_cast<std::uint32_t>(std::to_integer<std::uint32_t>(p.payload[0]))
         | (static_cast<std::uint32_t>(std::to_integer<std::uint32_t>(p.payload[1])) << 8)
         | (static_cast<std::uint32_t>(std::to_integer<std::uint32_t>(p.payload[2])) << 16)
         | (static_cast<std::uint32_t>(std::to_integer<std::uint32_t>(p.payload[3])) << 24);
}

// ---------------------------------------------------------------------------
// single channel, single on_channel group
// ---------------------------------------------------------------------------

TEST(router_single, returns_false_when_nothing_available) {
    flexible_mock<0> ch0;

    router r{
        on_channel(ch0,
            [](p2p_packet&) { FAIL() << "no packet was queued"; },
            [](big_packet&) { FAIL() << "no packet was queued"; })};

    EXPECT_FALSE(r.try_receive_any());
}

TEST(router_single, dispatches_a_small_packet_to_its_handler) {
    flexible_mock<0> ch0;
    const p2p_packet pkt = make_sealed_p2p();
    ch0.inject(pkt);

    bool visited_small = false;
    router r{
        on_channel(ch0,
            [&](p2p_packet& p) {
                visited_small = true;
                EXPECT_EQ(std::memcmp(&p, &pkt, sizeof(p2p_packet)), 0);
            },
            [&](big_packet&) { FAIL() << "expected the p2p_packet handler"; })};

    EXPECT_TRUE(r.try_receive_any());
    EXPECT_TRUE(visited_small);
}

TEST(router_single, dispatches_a_big_packet_to_its_handler) {
    flexible_mock<0> ch0;
    const big_packet pkt = make_sealed_big();
    ch0.inject(pkt);

    bool visited_big = false;
    router r{
        on_channel(ch0,
            [&](p2p_packet&) { FAIL() << "expected the big_packet handler"; },
            [&](big_packet& p) {
                visited_big = true;
                EXPECT_EQ(std::memcmp(&p, &pkt, sizeof(big_packet)), 0);
            })};

    EXPECT_TRUE(r.try_receive_any());
    EXPECT_TRUE(visited_big);
}

TEST(router_single, handler_declaration_order_does_not_affect_dispatch) {
    // The ordering-hazard regression test. A p2p_packet (16 bytes) is queued
    // while big_packet (48 bytes) is also a candidate. Handlers are declared
    // SMALLEST-FIRST -- the destructive order if it were honoured literally.
    // router frames candidate prefixes largest-first, so big_packet is tested
    // first (16 buffered < 48 -> not yet), then p2p_packet frames cleanly.
    flexible_mock<0> ch0;
    const p2p_packet pkt = make_sealed_p2p();
    ch0.inject(pkt);

    bool visited_small = false;
    router r{
        on_channel(ch0,
            [&](p2p_packet& p) {            // declared first, framed second
                visited_small = true;
                EXPECT_EQ(std::memcmp(&p, &pkt, sizeof(p2p_packet)), 0);
            },
            [&](big_packet&) {              // declared second, framed first
                FAIL() << "big_packet must not match a queued p2p_packet";
            })};

    EXPECT_TRUE(r.try_receive_any());
    EXPECT_TRUE(visited_small);
}

TEST(router_single, complete_small_packet_is_not_misframed_as_a_larger_candidate) {
    // A complete p2p_packet with a big_packet also declared: the big prefix
    // test can't succeed (16 < 48), so the p2p is framed and dispatched
    // correctly and byte-for-byte intact.
    flexible_mock<0> ch0;
    const p2p_packet pkt = make_sealed_p2p();
    ch0.inject(pkt);

    bool visited = false;
    router r{
        on_channel(ch0,
            [&](p2p_packet& p) {
                visited = true;
                EXPECT_EQ(std::memcmp(&p, &pkt, sizeof(p2p_packet)), 0);
            },
            [](big_packet&) { FAIL() << "big_packet must not match"; })};

    EXPECT_TRUE(r.try_receive_any());
    EXPECT_TRUE(visited);
    EXPECT_FALSE(r.try_receive_any());   // nothing left
}

TEST(router_single, drains_in_a_while_loop) {
    flexible_mock<0> ch0;
    ch0.inject(make_sealed_p2p());
    ch0.inject(make_sealed_p2p());
    ch0.inject(make_sealed_p2p());

    int small_count = 0;
    router r{on_channel(ch0, [&](p2p_packet&) { ++small_count; })};
    while (r.try_receive_any()) {}

    EXPECT_EQ(small_count, 3);
}

TEST(router_single, drains_mixed_sizes_when_polled_each_iteration) {
    flexible_mock<0> ch0;

    int small_count = 0;
    int big_count   = 0;
    router r{
        on_channel(ch0,
            [&](p2p_packet&) { ++small_count; },
            [&](big_packet&) { ++big_count; })};

    auto drain = [&] { while (r.try_receive_any()) {} };

    ch0.inject(make_sealed_p2p()); drain();
    ch0.inject(make_sealed_big());  drain();
    ch0.inject(make_sealed_p2p()); drain();

    EXPECT_EQ(small_count, 2);
    EXPECT_EQ(big_count, 1);
}

// ---------------------------------------------------------------------------
// partial arrival / reassembly (the 2026-07-21 fix)
// ---------------------------------------------------------------------------

TEST(router_partial, partially_arrived_large_packet_is_not_stolen_by_a_smaller_candidate) {
    // THE headline regression. A big_packet (48B) arrives 26 bytes first. With
    // a p2p_packet (16B) also a candidate, the old typed-probe code would read
    // 16 of those 26 bytes as a bogus p2p_packet, consume them, fail FCS, and
    // destroy the big_packet. router now buffers the 26 bytes and dispatches
    // nothing until the whole big_packet has arrived.
    flexible_mock<0> ch0;
    const big_packet big = make_sealed_big();

    int small = 0;
    int got_big = 0;
    router r{
        on_channel(ch0,
            [&](p2p_packet&) { ++small; },
            [&](big_packet& p) {
                ++got_big;
                EXPECT_EQ(std::memcmp(&p, &big, sizeof(big_packet)), 0);
            })};

    ch0.inject_bytes(big, 0, 26);            // only 26 of 48 bytes so far
    EXPECT_FALSE(r.try_receive_any());       // nothing complete; nothing stolen
    EXPECT_EQ(small, 0);
    EXPECT_EQ(got_big, 0);

    ch0.inject_bytes(big, 26, 22);           // the remaining 22 bytes arrive
    EXPECT_TRUE(r.try_receive_any());        // now the big_packet frames whole
    EXPECT_EQ(small, 0);
    EXPECT_EQ(got_big, 1);
}

TEST(router_partial, byte_at_a_time_arrival_still_reassembles) {
    flexible_mock<0> ch0;
    const big_packet big = make_sealed_big();

    int got_big = 0;
    router r{
        on_channel(ch0,
            [&](p2p_packet&) { FAIL() << "no p2p_packet was ever sent"; },
            [&](big_packet& p) {
                ++got_big;
                EXPECT_EQ(std::memcmp(&p, &big, sizeof(big_packet)), 0);
            })};

    for (std::size_t i = 0; i < sizeof(big_packet); ++i) {
        ch0.inject_bytes(big, i, 1);
        if (i + 1 < sizeof(big_packet)) {
            EXPECT_FALSE(r.try_receive_any()); // incomplete on every prefix
        }
    }
    EXPECT_TRUE(r.try_receive_any());          // completes on the last byte
    EXPECT_EQ(got_big, 1);
}

TEST(router_partial, backlog_of_small_packets_drains_as_small_packets) {
    // Three whole p2p_packets total exactly one big_packet's size. A pure
    // byte-count probe cannot tell them apart from one big_packet, but prefix
    // framing can: the big_packet interpretation fails its crc32 while each
    // 16-byte p2p_packet prefix passes its crc16. So the backlog drains as
    // three p2p_packets rather than being destroyed (the pre-fix behaviour).
    static_assert(3 * sizeof(p2p_packet) == sizeof(big_packet),
        "this test's premise requires three p2p_packets to exactly fill a big_packet");

    flexible_mock<0> ch0;
    ch0.inject(make_sealed_p2p());
    ch0.inject(make_sealed_p2p());
    ch0.inject(make_sealed_p2p());

    int small_count = 0;
    int big_count   = 0;
    router r{
        on_channel(ch0,
            [&](p2p_packet&) { ++small_count; },
            [&](big_packet&) { ++big_count; })};

    while (r.try_receive_any()) {}

    EXPECT_EQ(small_count, 3);
    EXPECT_EQ(big_count, 0);
}

TEST(router_partial, resyncs_past_garbage_and_delivers_the_next_valid_packet) {
    // A run of unframable bytes precedes a valid big_packet. Once the buffer
    // fills with bytes that frame as nothing, router drops leading bytes one at
    // a time until the real packet aligns and validates.
    flexible_mock<0> ch0;
    const big_packet big = make_sealed_big();

    // Enough leading garbage to fill the 48-byte reassembly buffer, then a
    // whole valid big_packet.
    ch0.inject_raw(20, std::byte{0xEE});
    ch0.inject(big);

    int got_big = 0;
    router r{
        on_channel(ch0,
            [&](p2p_packet&) { ++got_big; FAIL() << "garbage must not frame as p2p"; },
            [&](big_packet& p) {
                ++got_big;
                EXPECT_EQ(std::memcmp(&p, &big, sizeof(big_packet)), 0);
            })};

    // A single poll drains the buffer, resyncing internally as needed.
    bool delivered = false;
    for (int i = 0; i < 100 && not delivered; ++i)
        delivered = r.try_receive_any();

    EXPECT_TRUE(delivered);
    EXPECT_EQ(got_big, 1);
}

// ---------------------------------------------------------------------------
// multiple channels, multiple on_channel groups
// ---------------------------------------------------------------------------

TEST(router_multi_channel, each_channel_is_framed_only_for_its_own_types) {
    flexible_mock<0> wifi;
    flexible_mock<1> serial;

    // Only the serial link has data, and only its group names p2p_packet.
    const p2p_packet pkt = make_sealed_p2p();
    serial.inject(pkt);

    bool from_serial = false;
    router r{
        on_channel(wifi,
            [&](big_packet&) { FAIL() << "wifi had no data"; }),
        on_channel(serial,
            [&](p2p_packet& p) {
                from_serial = true;
                EXPECT_EQ(std::memcmp(&p, &pkt, sizeof(p2p_packet)), 0);
            })};

    EXPECT_TRUE(r.try_receive_any());
    EXPECT_TRUE(from_serial);
}

TEST(router_multi_channel, first_channel_in_declaration_order_wins) {
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;

    p2p_packet pkt0 = make_sealed_p2p();
    pkt0.payload[0] = std::byte{0x11};
    validator<p2p_packet>{}.seal(pkt0);

    p2p_packet pkt1 = make_sealed_p2p();
    pkt1.payload[0] = std::byte{0x22};
    validator<p2p_packet>{}.seal(pkt1);

    ch0.inject(pkt0);
    ch1.inject(pkt1);

    std::byte seen{0x00};
    router r{
        on_channel(ch0, [&](p2p_packet& p) { seen = p.payload[0]; }),
        on_channel(ch1, [&](p2p_packet& p) { seen = p.payload[0]; })};

    ASSERT_TRUE(r.try_receive_any());
    EXPECT_EQ(seen, std::byte{0x11});   // ch0's group framed first

    ASSERT_TRUE(r.try_receive_any());
    EXPECT_EQ(seen, std::byte{0x22});   // ch1 on the next call
}

TEST(router_multi_channel, a_channel_not_named_in_a_router_is_never_polled) {
    // router has no persistent enable/disable state (unlike hub::set_role):
    // whether a channel participates is fixed by the groups the router was
    // built from. A channel absent from those groups is never touched, so its
    // bytes remain in the channel for a different router that does name it.
    flexible_mock<0> ch0;
    flexible_mock<1> ch1;

    const p2p_packet pkt = make_sealed_p2p();
    ch0.inject(pkt);   // ch0 has data, but this router only names ch1

    router r1{on_channel(ch1, [](p2p_packet&) { FAIL() << "ch1 has no data"; })};
    EXPECT_FALSE(r1.try_receive_any());

    // ch0's packet is untouched -- a router that does name ch0 finds it.
    bool found = false;
    router r2{
        on_channel(ch0, [&](p2p_packet& p) {
            found = true;
            EXPECT_EQ(std::memcmp(&p, &pkt, sizeof(p2p_packet)), 0);
        })};
    ASSERT_TRUE(r2.try_receive_any());
    EXPECT_TRUE(found);
}

TEST(router_multi_channel, works_across_flexible_and_reliable_channel) {
    flexible_mock<0> flexible_ch;
    fixed_mock<seq_packet, 0> reliable_impl;
    reliable_channel<fixed_mock<seq_packet, 0>, seq_packet, mock_clock, 2, 1> reliable{reliable_impl};

    mock_clock::_now = 0;
    const p2p_packet pkt = make_sealed_p2p();
    flexible_ch.inject(pkt);

    bool from_flexible = false;
    router r{
        on_channel(flexible_ch,
            [&](p2p_packet& p) {
                from_flexible = true;
                EXPECT_EQ(std::memcmp(&p, &pkt, sizeof(p2p_packet)), 0);
            }),
        on_channel(reliable,
            [&](seq_packet&) { FAIL() << "reliable channel had no data"; })};

    EXPECT_TRUE(r.try_receive_any());
    EXPECT_TRUE(from_flexible);
}

// ---------------------------------------------------------------------------
// stress / long-term stability
//
// These push large volumes through the fabric so the reassembly buffers cycle
// through fill/drain many thousands of times, and verify -- by an embedded
// per-type sequence number -- that every packet is delivered exactly once, in
// order, to the right handler, with nothing lost, duplicated, reordered, or
// misframed.
// ---------------------------------------------------------------------------

TEST(router_stress, single_type_high_volume_drained_each_iteration) {
    // 50k packets, one type, drained after every inject -- 50k independent
    // fill-to-empty buffer cycles.
    flexible_mock<0> ch;
    constexpr int N = 50000;

    std::uint32_t expected = 0;
    int received = 0;
    router r{
        on_channel(ch, [&](m16& p) {
            EXPECT_EQ(seq_of(p), expected);
            ++expected;
            ++received;
        })};

    for (int i = 0; i < N; ++i) {
        ch.inject(stamp<m16>(static_cast<std::uint32_t>(i)));
        ASSERT_TRUE(r.try_receive_any());
    }
    EXPECT_FALSE(r.try_receive_any());
    EXPECT_EQ(received, N);
    EXPECT_EQ(expected, static_cast<std::uint32_t>(N));
}

TEST(router_stress, single_type_large_backlog_then_bulk_drain) {
    // Queue a big backlog first, then drain it all -- the buffer tops up from
    // the channel repeatedly (cap = one packet) across the whole run.
    flexible_mock<0> ch;
    constexpr int N = 30000;

    for (int i = 0; i < N; ++i)
        ch.inject(stamp<m16>(static_cast<std::uint32_t>(i)));

    std::uint32_t expected = 0;
    int received = 0;
    router r{
        on_channel(ch, [&](m16& p) {
            EXPECT_EQ(seq_of(p), expected);
            ++expected;
            ++received;
        })};

    while (r.try_receive_any()) {}
    EXPECT_EQ(received, N);
}

TEST(router_stress, mixed_sizes_one_channel_random_backlog_depths) {
    // Four sizes on one channel, injected in a deterministic pseudo-random
    // order with the drain happening only sometimes -- so backlogs of mixed
    // sizes build up and must be framed apart correctly, over and over.
    flexible_mock<0> ch;
    std::mt19937 rng(0xC0FFEEu);
    constexpr int N = 40000;

    std::uint32_t e16 = 0, e24 = 0, e32 = 0, e48 = 0;
    int seen = 0;
    router r{
        on_channel(ch,
            [&](m16& p) { EXPECT_EQ(seq_of(p), e16); ++e16; ++seen; },
            [&](m24& p) { EXPECT_EQ(seq_of(p), e24); ++e24; ++seen; },
            [&](m32& p) { EXPECT_EQ(seq_of(p), e32); ++e32; ++seen; },
            [&](m48& p) { EXPECT_EQ(seq_of(p), e48); ++e48; ++seen; })};

    std::uint32_t s16 = 0, s24 = 0, s32 = 0, s48 = 0;
    for (int i = 0; i < N; ++i) {
        switch (rng() % 4) {
            case 0: ch.inject(stamp<m16>(s16++)); break;
            case 1: ch.inject(stamp<m24>(s24++)); break;
            case 2: ch.inject(stamp<m32>(s32++)); break;
            case 3: ch.inject(stamp<m48>(s48++)); break;
        }
        if (rng() % 4 == 0) while (r.try_receive_any()) {}   // drain ~1/4 of the time
    }
    while (r.try_receive_any()) {}

    EXPECT_EQ(seen, N);
    EXPECT_EQ(e16, s16);
    EXPECT_EQ(e24, s24);
    EXPECT_EQ(e32, s32);
    EXPECT_EQ(e48, s48);
}

TEST(router_stress, mixed_sizes_fragmented_arrival_long_run) {
    // The strongest test: a long wire of mixed-size packets fed to the channel
    // in random byte-sized chunks (simulating arbitrary TCP/serial
    // segmentation), with draining at random points. Packet boundaries fall
    // anywhere relative to chunk boundaries, so the reassembly buffer is
    // constantly holding partial packets across polls.
    flexible_mock<0> ch;
    std::mt19937 rng(0xBEEF1234u);
    constexpr int M = 12000;

    std::uint32_t e16 = 0, e24 = 0, e32 = 0, e48 = 0;
    int seen = 0;
    router r{
        on_channel(ch,
            [&](m16& p) { EXPECT_EQ(seq_of(p), e16); ++e16; ++seen; },
            [&](m24& p) { EXPECT_EQ(seq_of(p), e24); ++e24; ++seen; },
            [&](m32& p) { EXPECT_EQ(seq_of(p), e32); ++e32; ++seen; },
            [&](m48& p) { EXPECT_EQ(seq_of(p), e48); ++e48; ++seen; })};

    // Build the wire: M packets of random type, each with a per-type sequence.
    std::vector<std::byte> wire;
    std::uint32_t s16 = 0, s24 = 0, s32 = 0, s48 = 0;
    auto append = [&](const auto& pkt) {
        const auto* b = reinterpret_cast<const std::byte*>(&pkt);
        wire.insert(wire.end(), b, b + sizeof(pkt));
    };
    for (int i = 0; i < M; ++i) {
        switch (rng() % 4) {
            case 0: append(stamp<m16>(s16++)); break;
            case 1: append(stamp<m24>(s24++)); break;
            case 2: append(stamp<m32>(s32++)); break;
            case 3: append(stamp<m48>(s48++)); break;
        }
    }

    // Feed it in random 1..40 byte chunks, draining a random amount between.
    std::uniform_int_distribution<std::size_t> chunk(1, 40);
    std::size_t pos = 0;
    while (pos < wire.size()) {
        const std::size_t n = std::min(chunk(rng), wire.size() - pos);
        for (std::size_t k = 0; k < n; ++k)
            ch.rx.push_back(wire[pos + k]);
        pos += n;

        const int drains = static_cast<int>(rng() % 3);   // 0..2 drains
        for (int d = 0; d < drains; ++d)
            if (not r.try_receive_any()) break;
    }
    while (r.try_receive_any()) {}

    EXPECT_EQ(seen, M);
    EXPECT_EQ(e16, s16);
    EXPECT_EQ(e24, s24);
    EXPECT_EQ(e32, s32);
    EXPECT_EQ(e48, s48);
}

TEST(router_stress, many_channels_distinct_packets_stay_consistent) {
    // Six channels carrying different packet types (two of them multi-size),
    // fed interleaved at high volume with intermittent draining. Each channel's
    // stream must stay independently consistent.
    flexible_mock<0> c0;   // m16
    flexible_mock<1> c1;   // m24
    flexible_mock<2> c2;   // m32
    flexible_mock<3> c3;   // m48
    flexible_mock<4> c4;   // {m16, m48}
    flexible_mock<5> c5;   // {m24, m32}

    std::uint32_t e0 = 0, e1 = 0, e2 = 0, e3 = 0;
    std::uint32_t e4_16 = 0, e4_48 = 0, e5_24 = 0, e5_32 = 0;

    router r{
        on_channel(c0, [&](m16& p) { EXPECT_EQ(seq_of(p), e0); ++e0; }),
        on_channel(c1, [&](m24& p) { EXPECT_EQ(seq_of(p), e1); ++e1; }),
        on_channel(c2, [&](m32& p) { EXPECT_EQ(seq_of(p), e2); ++e2; }),
        on_channel(c3, [&](m48& p) { EXPECT_EQ(seq_of(p), e3); ++e3; }),
        on_channel(c4,
            [&](m16& p) { EXPECT_EQ(seq_of(p), e4_16); ++e4_16; },
            [&](m48& p) { EXPECT_EQ(seq_of(p), e4_48); ++e4_48; }),
        on_channel(c5,
            [&](m24& p) { EXPECT_EQ(seq_of(p), e5_24); ++e5_24; },
            [&](m32& p) { EXPECT_EQ(seq_of(p), e5_32); ++e5_32; })};

    std::mt19937 rng(0x5151A5A5u);
    constexpr int ROUNDS = 8000;
    std::uint32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    std::uint32_t s4_16 = 0, s4_48 = 0, s5_24 = 0, s5_32 = 0;

    for (int i = 0; i < ROUNDS; ++i) {
        c0.inject(stamp<m16>(s0++));
        c1.inject(stamp<m24>(s1++));
        c2.inject(stamp<m32>(s2++));
        c3.inject(stamp<m48>(s3++));
        if (rng() & 1) c4.inject(stamp<m16>(s4_16++));
        else           c4.inject(stamp<m48>(s4_48++));
        if (rng() & 1) c5.inject(stamp<m24>(s5_24++));
        else           c5.inject(stamp<m32>(s5_32++));

        if (rng() % 5 == 0) while (r.try_receive_any()) {}   // drain ~1/5 of rounds
    }
    while (r.try_receive_any()) {}

    EXPECT_EQ(e0, s0);
    EXPECT_EQ(e1, s1);
    EXPECT_EQ(e2, s2);
    EXPECT_EQ(e3, s3);
    EXPECT_EQ(e4_16, s4_16);
    EXPECT_EQ(e4_48, s4_48);
    EXPECT_EQ(e5_24, s5_24);
    EXPECT_EQ(e5_32, s5_32);
}

TEST(router_stress, real_send_path_round_trips_at_volume) {
    // End-to-end through the real send() path: seal+serialize many packets with
    // channel::send, loop the produced bytes back as received bytes, and route
    // them. Exercises seal -> wire -> receive_raw -> reassembly -> validate ->
    // dispatch as one pipeline.
    flexible_mock<0> link;
    constexpr int N = 15000;

    for (int i = 0; i < N; ++i) {
        m32 p = fill<m32>(static_cast<std::uint32_t>(i));
        ASSERT_EQ(link.send(p), send_result::ok);
    }
    // Loop the transmitted bytes back onto the receive side.
    for (const std::byte b : link.tx) link.rx.push_back(b);
    link.tx.clear();

    std::uint32_t expected = 0;
    int received = 0;
    router r{
        on_channel(link, [&](m32& p) {
            EXPECT_EQ(seq_of(p), expected);
            ++expected;
            ++received;
        })};

    while (r.try_receive_any()) {}
    EXPECT_EQ(received, N);
}
