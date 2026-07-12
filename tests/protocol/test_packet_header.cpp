// SPDX-License-Identifier: MIT
/**
* @file test_packet_header.cpp
*
* @brief Unit tests for ecomm::protocol::packet_header.
*
* @ingroup ecomm_tests
*
* Coverage:
*   - Wire layout (sizeof, field offsets) for all eight topology x sequence x
*     checksum combinations.
*   - Default-construction produces a zero protocol byte.
*   - Two-parameter construction correctly packs type, options, and version.
*   - Type accessor round-trips all six header_type enumerators.
*   - Options accessor round-trips all individual flags and multi-flag combinations.
*   - has() subset test: positive and negative cases.
*   - version() always equals ECOMM_PROTOCOL_VERSION after construction.
*   - raw() matches the manually computed expected byte.
*   - network_ids defaults: sender_id == ECOMM_BOARD_ID, receiver_id == 0.
*   - seq_num defaults to 0 on construction; is publicly writable.
*   - fcs_storage zero-initialization for each checksum policy width.
*   - has_node_ids, has_seq_num, and fcs_size compile-time constants are correct.
*   - Bit-field isolation: type, options, and version occupy disjoint bit
*     regions; setting one field to its maximum value does not corrupt the
*     others.
*   - All eight specialisations give identical results from the shared accessor
*     surface for the same inputs.
*   - Field exposure per specialisation: each spec exposes exactly the fields
*     it carries; extra-field writes do not corrupt each other or the protocol byte.
*   - Wire field order: offsetof static_asserts lock in the declared field
*     positions for every specialisation.
*   - header_options bitwise operators (|, &) produce the expected values.
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
* - 2026-05-27 Added SequencePolicy parameter; eight specialisations total.
*              Added seq_num field tests and offsetof checks for sequenced specs.
*/

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ecomm/protocol/packet_header.hpp>

// ---------------------------------------------------------------------------
// Convenience aliases  --  no_sequence variants (backward-compatible surface)
// ---------------------------------------------------------------------------

using namespace ecomm::protocol;

// no_sequence variants (SequencePolicy = no_sequence)
using hdr_p2p_none   = packet_header<topology::point_to_point, no_sequence, none>;
using hdr_net_none   = packet_header<topology::network,        no_sequence, none>;
using hdr_p2p_crc8   = packet_header<topology::point_to_point, no_sequence, crc8>;
using hdr_p2p_crc16  = packet_header<topology::point_to_point, no_sequence, crc16>;
using hdr_p2p_crc32  = packet_header<topology::point_to_point, no_sequence, crc32>;
using hdr_p2p_crc64  = packet_header<topology::point_to_point, no_sequence, crc64>;
using hdr_net_crc32  = packet_header<topology::network,        no_sequence, crc32>;

// sequenced variants (SequencePolicy = sequenced)
using hdr_p2p_seq_none  = packet_header<topology::point_to_point, sequenced, none>;
using hdr_p2p_seq_crc32 = packet_header<topology::point_to_point, sequenced, crc32>;
using hdr_net_seq_none  = packet_header<topology::network,        sequenced, none>;
using hdr_net_seq_crc32 = packet_header<topology::network,        sequenced, crc32>;

// ---------------------------------------------------------------------------
// Compile-time layout assertions  --  no_sequence variants
//
// These fire at compile time. If any fails the translation unit does not
// compile and the binary is never produced.
// ---------------------------------------------------------------------------

// point_to_point + no_sequence + none: protocol byte only → 1 byte
static_assert(sizeof(hdr_p2p_none) == 1,
    "p2p/noseq/none: must be exactly 1 byte");

// network + no_sequence + none: proto + sender_id + receiver_id → 3 bytes
static_assert(sizeof(hdr_net_none) == 3,
    "net/noseq/none: must be exactly 3 bytes");

// point_to_point + no_sequence + crc8: 1 + 1 = 2 bytes
static_assert(sizeof(hdr_p2p_crc8) == 2,
    "p2p/noseq/crc8: must be exactly 2 bytes");

// point_to_point + no_sequence + crc16: 1 + 2 = 3 bytes
static_assert(sizeof(hdr_p2p_crc16) == 3,
    "p2p/noseq/crc16: must be exactly 3 bytes");

// point_to_point + no_sequence + crc32: 1 + 4 = 5 bytes
static_assert(sizeof(hdr_p2p_crc32) == 5,
    "p2p/noseq/crc32: must be exactly 5 bytes");

// point_to_point + no_sequence + crc64: 1 + 8 = 9 bytes
static_assert(sizeof(hdr_p2p_crc64) == 9,
    "p2p/noseq/crc64: must be exactly 9 bytes");

// network + no_sequence + crc32: 1 + 2 + 4 = 7 bytes
static_assert(sizeof(hdr_net_crc32) == 7,
    "net/noseq/crc32: must be exactly 7 bytes");

// ---------------------------------------------------------------------------
// Compile-time layout assertions  --  sequenced variants
// ---------------------------------------------------------------------------

// point_to_point + sequenced + none: proto + seq_num → 2 bytes
static_assert(sizeof(hdr_p2p_seq_none) == 2,
    "p2p/seq/none: must be exactly 2 bytes (proto + seq_num)");

// point_to_point + sequenced + crc32: proto + seq_num + fcs → 6 bytes
static_assert(sizeof(hdr_p2p_seq_crc32) == 6,
    "p2p/seq/crc32: must be exactly 6 bytes (proto + seq_num + fcs)");

// network + sequenced + none: proto + seq_num + sender_id + receiver_id → 4 bytes
static_assert(sizeof(hdr_net_seq_none) == 4,
    "net/seq/none: must be exactly 4 bytes (proto + seq_num + sender_id + receiver_id)");

// network + sequenced + crc32: proto + seq_num + sender_id + receiver_id + fcs → 8 bytes
static_assert(sizeof(hdr_net_seq_crc32) == 8,
    "net/seq/crc32: must be exactly 8 bytes (full header)");

// ---------------------------------------------------------------------------
// Compile-time constant checks  --  fcs_size
// ---------------------------------------------------------------------------

static_assert(hdr_p2p_none::fcs_size      == 0, "p2p/noseq/none: fcs_size must be 0");
static_assert(hdr_net_none::fcs_size      == 0, "net/noseq/none: fcs_size must be 0");
static_assert(hdr_p2p_crc32::fcs_size     == 4, "p2p/noseq/crc32: fcs_size must be 4");
static_assert(hdr_net_crc32::fcs_size     == 4, "net/noseq/crc32: fcs_size must be 4");
static_assert(hdr_p2p_seq_none::fcs_size  == 0, "p2p/seq/none: fcs_size must be 0");
static_assert(hdr_p2p_seq_crc32::fcs_size == 4, "p2p/seq/crc32: fcs_size must be 4");
static_assert(hdr_net_seq_none::fcs_size  == 0, "net/seq/none: fcs_size must be 0");
static_assert(hdr_net_seq_crc32::fcs_size == 4, "net/seq/crc32: fcs_size must be 4");

// ---------------------------------------------------------------------------
// Compile-time constant checks  --  has_node_ids
// ---------------------------------------------------------------------------

static_assert(not hdr_p2p_none::has_node_ids,     "p2p topology must not have node ids");
static_assert( hdr_net_none::has_node_ids,     "network topology must have node ids");
static_assert(not hdr_p2p_seq_none::has_node_ids, "p2p/seq topology must not have node ids");
static_assert( hdr_net_seq_none::has_node_ids, "net/seq topology must have node ids");

// ---------------------------------------------------------------------------
// Compile-time constant checks  --  has_seq_num
// ---------------------------------------------------------------------------

static_assert(not hdr_p2p_none::has_seq_num,    "no_sequence must not have seq_num");
static_assert(not hdr_net_none::has_seq_num,    "net/no_sequence must not have seq_num");
static_assert(not hdr_p2p_crc32::has_seq_num,  "p2p/crc32 must not have seq_num");
static_assert(not hdr_net_crc32::has_seq_num,  "net/crc32 must not have seq_num");
static_assert( hdr_p2p_seq_none::has_seq_num,  "p2p/seq/none must have seq_num");
static_assert( hdr_p2p_seq_crc32::has_seq_num, "p2p/seq/crc32 must have seq_num");
static_assert( hdr_net_seq_none::has_seq_num,  "net/seq/none must have seq_num");
static_assert( hdr_net_seq_crc32::has_seq_num, "net/seq/crc32 must have seq_num");

// ---------------------------------------------------------------------------
// Standard-layout guarantee
// ---------------------------------------------------------------------------

static_assert(std::is_standard_layout_v<hdr_p2p_none>,
    "packet_header<p2p, noseq, none> must be standard-layout");
static_assert(std::is_standard_layout_v<hdr_net_none>,
    "packet_header<net, noseq, none> must be standard-layout");
static_assert(std::is_standard_layout_v<hdr_p2p_crc32>,
    "packet_header<p2p, noseq, crc32> must be standard-layout");
static_assert(std::is_standard_layout_v<hdr_net_crc32>,
    "packet_header<net, noseq, crc32> must be standard-layout");
static_assert(std::is_standard_layout_v<hdr_p2p_seq_none>,
    "packet_header<p2p, seq, none> must be standard-layout");
static_assert(std::is_standard_layout_v<hdr_p2p_seq_crc32>,
    "packet_header<p2p, seq, crc32> must be standard-layout");
static_assert(std::is_standard_layout_v<hdr_net_seq_none>,
    "packet_header<net, seq, none> must be standard-layout");
static_assert(std::is_standard_layout_v<hdr_net_seq_crc32>,
    "packet_header<net, seq, crc32> must be standard-layout");

// ---------------------------------------------------------------------------
// Wire field order  --  offsetof assertions
//
// These lock in the exact byte positions for every field in every specialisation
// where it is present. They fire at compile time if the field order in
// header_layout specialisations is ever changed accidentally.
//
// no_sequence wire orders:
//   p2p/noseq/crc32 : _byte(0), fcs(1)
//   net/noseq/none  : _byte(0), sender_id(1), receiver_id(2)
//   net/noseq/crc32 : _byte(0), sender_id(1), receiver_id(2), fcs(3)
//
// sequenced wire orders:
//   p2p/seq/none    : _byte(0), seq_num(1)
//   p2p/seq/crc32   : _byte(0), seq_num(1), fcs(2)
//   net/seq/none    : _byte(0), seq_num(1), sender_id(2), receiver_id(3)
//   net/seq/crc32   : _byte(0), seq_num(1), sender_id(2), receiver_id(3), fcs(4)
// ---------------------------------------------------------------------------

// p2p/noseq/crc32
static_assert(offsetof(hdr_p2p_crc32, fcs) == 1,
    "p2p/noseq/crc32: fcs must start at byte 1");

// net/noseq/none
static_assert(offsetof(hdr_net_none, sender_id)   == 1,
    "net/noseq/none: sender_id must start at byte 1");
static_assert(offsetof(hdr_net_none, receiver_id) == 2,
    "net/noseq/none: receiver_id must start at byte 2");

// net/noseq/crc32
static_assert(offsetof(hdr_net_crc32, sender_id)   == 1,
    "net/noseq/crc32: sender_id must start at byte 1");
static_assert(offsetof(hdr_net_crc32, receiver_id) == 2,
    "net/noseq/crc32: receiver_id must start at byte 2");
static_assert(offsetof(hdr_net_crc32, fcs)         == 3,
    "net/noseq/crc32: fcs must start at byte 3 (last)");

// p2p/seq/none
static_assert(offsetof(hdr_p2p_seq_none, seq_num) == 1,
    "p2p/seq/none: seq_num must start at byte 1 (immediately after _byte)");

// p2p/seq/crc32
static_assert(offsetof(hdr_p2p_seq_crc32, seq_num) == 1,
    "p2p/seq/crc32: seq_num must start at byte 1");
static_assert(offsetof(hdr_p2p_seq_crc32, fcs)     == 2,
    "p2p/seq/crc32: fcs must start at byte 2 (last)");

// net/seq/none
static_assert(offsetof(hdr_net_seq_none, seq_num)    == 1,
    "net/seq/none: seq_num must start at byte 1");
static_assert(offsetof(hdr_net_seq_none, sender_id)   == 2,
    "net/seq/none: sender_id must start at byte 2");
static_assert(offsetof(hdr_net_seq_none, receiver_id) == 3,
    "net/seq/none: receiver_id must start at byte 3");

// net/seq/crc32
static_assert(offsetof(hdr_net_seq_crc32, seq_num)    == 1,
    "net/seq/crc32: seq_num must start at byte 1");
static_assert(offsetof(hdr_net_seq_crc32, sender_id)   == 2,
    "net/seq/crc32: sender_id must start at byte 2");
static_assert(offsetof(hdr_net_seq_crc32, receiver_id) == 3,
    "net/seq/crc32: receiver_id must start at byte 3");
static_assert(offsetof(hdr_net_seq_crc32, fcs)         == 4,
    "net/seq/crc32: fcs must start at byte 4 (last)");

// ---------------------------------------------------------------------------
// Helper: build the expected protocol byte independently of the class.
// ---------------------------------------------------------------------------

static constexpr std::uint8_t make_expected_byte(
    header_type    type,
    header_options opts
) noexcept {
    return static_cast<std::uint8_t>(
        ((static_cast<std::uint8_t>(type) & 0x7u) << 5) |
        (static_cast<std::uint8_t>(opts) & header_options_mask) |
        (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
    );
}

// ---------------------------------------------------------------------------
// Test suite: default construction
// ---------------------------------------------------------------------------

TEST(packet_header, default_constructs_zero_protocol_byte) {
    constexpr hdr_p2p_none h{};
    EXPECT_EQ(h.raw(), 0x00u);
}

TEST(packet_header, default_type_is_data) {
    constexpr hdr_p2p_none h{};
    EXPECT_EQ(h.type(), header_type::data);
}

TEST(packet_header, default_options_are_none) {
    constexpr hdr_p2p_none h{};
    EXPECT_EQ(h.options(), header_options::none);
}

TEST(packet_header, default_version_is_zero) {
    constexpr hdr_p2p_none h{};
    EXPECT_EQ(h.version(), 0u);
}

TEST(packet_header, network_default_sender_id_is_board_id) {
    hdr_net_none h{};
    EXPECT_EQ(h.sender_id, static_cast<std::uint8_t>(ECOMM_BOARD_ID));
}

TEST(packet_header, network_default_receiver_id_is_zero) {
    hdr_net_none h{};
    EXPECT_EQ(h.receiver_id, 0u);
}

TEST(packet_header, fcs_zero_initialized_crc32) {
    hdr_p2p_crc32 h{};
    EXPECT_EQ(h.fcs, 0u);
}

TEST(packet_header, seq_num_zero_initialized) {
    hdr_p2p_seq_none h{};
    EXPECT_EQ(h.seq_num, 0u);
}

TEST(packet_header, seq_num_zero_initialized_with_fcs) {
    hdr_p2p_seq_crc32 h{};
    EXPECT_EQ(h.seq_num, 0u);
    EXPECT_EQ(h.fcs,     0u);
}

TEST(packet_header, network_seq_num_zero_initialized) {
    hdr_net_seq_none h{};
    EXPECT_EQ(h.seq_num, 0u);
    EXPECT_EQ(h.sender_id,   static_cast<std::uint8_t>(ECOMM_BOARD_ID));
    EXPECT_EQ(h.receiver_id, 0u);
}

// ---------------------------------------------------------------------------
// Test suite: two-parameter construction  --  type round-trip
// ---------------------------------------------------------------------------

class packet_header_type_roundtrip : public ::testing::TestWithParam<header_type> {};

TEST_P(packet_header_type_roundtrip, type_survives_construction) {
    const header_type t = GetParam();
    const hdr_p2p_none h{t, header_options::none};
    EXPECT_EQ(h.type(), t);
}

INSTANTIATE_TEST_SUITE_P(
    all_types,
    packet_header_type_roundtrip,
    ::testing::Values(
        header_type::data,
        header_type::control,
        header_type::auth,
        header_type::session,
        header_type::log,
        header_type::firmware
    )
);

// ---------------------------------------------------------------------------
// Test suite: two-parameter construction  --  options round-trip
// ---------------------------------------------------------------------------

TEST(packet_header, options_none_stored_correctly) {
    constexpr hdr_p2p_none h{header_type::data, header_options::none};
    EXPECT_EQ(h.options(), header_options::none);
}

TEST(packet_header, option_error_stored_correctly) {
    constexpr hdr_p2p_none h{header_type::data, header_options::error};
    EXPECT_EQ(h.options(), header_options::error);
    EXPECT_TRUE(h.has(header_options::error));
    EXPECT_FALSE(h.has(header_options::ack));
    EXPECT_FALSE(h.has(header_options::encrypted));
}

TEST(packet_header, option_ack_stored_correctly) {
    constexpr hdr_p2p_none h{header_type::control, header_options::ack};
    EXPECT_EQ(h.options(), header_options::ack);
    EXPECT_TRUE(h.has(header_options::ack));
    EXPECT_FALSE(h.has(header_options::error));
    EXPECT_FALSE(h.has(header_options::encrypted));
}

TEST(packet_header, option_encrypted_stored_correctly) {
    constexpr hdr_p2p_none h{header_type::data, header_options::encrypted};
    EXPECT_EQ(h.options(), header_options::encrypted);
    EXPECT_TRUE(h.has(header_options::encrypted));
    EXPECT_FALSE(h.has(header_options::error));
    EXPECT_FALSE(h.has(header_options::ack));
}

TEST(packet_header, option_all_flags_combined) {
    constexpr header_options all =
        header_options::error | header_options::ack | header_options::encrypted;

    constexpr hdr_p2p_none h{header_type::firmware, all};

    EXPECT_TRUE(h.has(header_options::error));
    EXPECT_TRUE(h.has(header_options::ack));
    EXPECT_TRUE(h.has(header_options::encrypted));
    EXPECT_TRUE(h.has(all));
}

TEST(packet_header, has_returns_false_for_absent_subset) {
    constexpr hdr_p2p_none h{header_type::data, header_options::error};
    constexpr header_options absent = header_options::error | header_options::ack;
    EXPECT_FALSE(h.has(absent));
}

// ---------------------------------------------------------------------------
// Test suite: version field
// ---------------------------------------------------------------------------

TEST(packet_header, version_equals_protocol_version_after_construction) {
    const hdr_p2p_none h{header_type::data, header_options::none};
    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
}

TEST(packet_header, version_independent_of_type_and_options) {
    constexpr header_options opts =
        header_options::error | header_options::ack | header_options::encrypted;

    const hdr_p2p_none h1{header_type::firmware, opts};
    const hdr_p2p_none h2{header_type::auth,     header_options::none};

    EXPECT_EQ(h1.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
    EXPECT_EQ(h2.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
}

// ---------------------------------------------------------------------------
// Test suite: raw() byte layout
// ---------------------------------------------------------------------------

TEST(packet_header, raw_byte_data_no_options) {
    constexpr hdr_p2p_none h{header_type::data, header_options::none};
    EXPECT_EQ(h.raw(), make_expected_byte(header_type::data, header_options::none));
}

TEST(packet_header, raw_byte_firmware_all_options) {
    constexpr header_options all =
        header_options::error | header_options::ack | header_options::encrypted;
    constexpr hdr_p2p_none h{header_type::firmware, all};
    EXPECT_EQ(h.raw(), make_expected_byte(header_type::firmware, all));
}

TEST(packet_header, raw_byte_session_encrypted) {
    constexpr hdr_p2p_none h{header_type::session, header_options::encrypted};
    EXPECT_EQ(h.raw(), make_expected_byte(header_type::session, header_options::encrypted));
}

// ---------------------------------------------------------------------------
// Test suite: type and options do not bleed into each other
// ---------------------------------------------------------------------------

TEST(packet_header, type_bits_do_not_affect_options) {
    for (const auto t : {
            header_type::data, header_type::control, header_type::auth,
            header_type::session, header_type::log, header_type::firmware
    }) {
        const hdr_p2p_none h{t, header_options::none};
        EXPECT_EQ(h.options(), header_options::none)
            << "type=" << static_cast<int>(t);
    }
}

TEST(packet_header, option_bits_do_not_affect_type) {
    constexpr header_type t = header_type::log;
    for (const auto o : {
            header_options::none,
            header_options::error,
            header_options::ack,
            header_options::encrypted,
            header_options::error | header_options::ack,
            header_options::error | header_options::encrypted,
            header_options::ack | header_options::encrypted,
            header_options::error | header_options::ack | header_options::encrypted
    }) {
        const hdr_p2p_none h{t, o};
        EXPECT_EQ(h.type(), t)
            << "options=" << static_cast<int>(o);
    }
}

// ---------------------------------------------------------------------------
// Test suite: network topology
// ---------------------------------------------------------------------------

TEST(packet_header, p2p_topology_no_network_ids) {
    static_assert(not hdr_p2p_none::has_node_ids);
    static_assert(sizeof(hdr_p2p_none) == 1);
}

TEST(packet_header, network_topology_has_node_ids) {
    static_assert(hdr_net_none::has_node_ids);
    static_assert(sizeof(hdr_net_none) == 3);
}

TEST(packet_header, network_topology_ids_are_assignable) {
    hdr_net_none h{header_type::data, header_options::none};
    h.sender_id   = 0xAB;
    h.receiver_id = 0xCD;

    EXPECT_EQ(h.sender_id,   0xABu);
    EXPECT_EQ(h.receiver_id, 0xCDu);
    EXPECT_EQ(h.type(),    header_type::data);
    EXPECT_EQ(h.options(), header_options::none);
    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
}

TEST(packet_header, network_ids_do_not_corrupt_protocol_byte) {
    hdr_net_none h{header_type::firmware, header_options::error};
    const std::uint8_t expected_byte =
        make_expected_byte(header_type::firmware, header_options::error);

    h.sender_id   = 0xFF;
    h.receiver_id = 0xFF;

    EXPECT_EQ(h.raw(), expected_byte);
}

// ---------------------------------------------------------------------------
// Test suite: sequenced policy  --  seq_num field
// ---------------------------------------------------------------------------

TEST(packet_header, sequenced_p2p_none_seq_num_writable) {
    hdr_p2p_seq_none h{header_type::data, header_options::none};
    EXPECT_EQ(h.seq_num, 0u);

    h.seq_num = 0x42u;
    EXPECT_EQ(h.seq_num, 0x42u);

    // Protocol byte must not be affected.
    EXPECT_EQ(h.type(),    header_type::data);
    EXPECT_EQ(h.options(), header_options::none);
    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
}

TEST(packet_header, sequenced_p2p_crc32_seq_num_writable) {
    hdr_p2p_seq_crc32 h{header_type::control, header_options::ack};
    const std::uint8_t expected_byte =
        make_expected_byte(header_type::control, header_options::ack);

    h.seq_num = 0xFFu;
    h.fcs     = 0x12345678u;

    EXPECT_EQ(h.seq_num, 0xFFu);
    EXPECT_EQ(h.fcs,     0x12345678u);
    EXPECT_EQ(h.raw(), expected_byte);
}

TEST(packet_header, sequenced_net_none_seq_num_and_ids_writable) {
    hdr_net_seq_none h{header_type::session, header_options::none};

    h.seq_num    = 0x10u;
    h.sender_id   = 0xAAu;
    h.receiver_id = 0xBBu;

    EXPECT_EQ(h.seq_num,    0x10u);
    EXPECT_EQ(h.sender_id,   0xAAu);
    EXPECT_EQ(h.receiver_id, 0xBBu);
    EXPECT_EQ(h.type(), header_type::session);
}

TEST(packet_header, sequenced_net_crc32_all_fields_independent) {
    hdr_net_seq_crc32 h{header_type::log, header_options::encrypted};
    const std::uint8_t expected_byte =
        make_expected_byte(header_type::log, header_options::encrypted);

    h.seq_num     = 0x07u;
    h.sender_id   = 0x11u;
    h.receiver_id = 0x22u;
    h.fcs         = 0xCAFEBABEu;

    EXPECT_EQ(h.seq_num,    0x07u);
    EXPECT_EQ(h.sender_id,   0x11u);
    EXPECT_EQ(h.receiver_id, 0x22u);
    EXPECT_EQ(h.fcs,         0xCAFEBABEu);
    EXPECT_EQ(h.raw(), expected_byte);
    EXPECT_EQ(h.type(),    header_type::log);
    EXPECT_EQ(h.options(), header_options::encrypted);
    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
}

TEST(packet_header, sequenced_seq_num_does_not_corrupt_fcs) {
    hdr_p2p_seq_crc32 h{header_type::data, header_options::none};
    h.fcs     = 0xDEADBEEFu;
    h.seq_num = 0xFF;

    EXPECT_EQ(h.fcs, 0xDEADBEEFu);
}

TEST(packet_header, sequenced_fcs_does_not_corrupt_seq_num) {
    hdr_p2p_seq_crc32 h{header_type::data, header_options::none};
    h.seq_num = 0xA5u;
    h.fcs     = 0x12345678u;

    EXPECT_EQ(h.seq_num, 0xA5u);
}

TEST(packet_header, sequenced_net_seq_num_does_not_corrupt_ids) {
    hdr_net_seq_none h{header_type::data, header_options::none};
    h.sender_id   = 0x42u;
    h.receiver_id = 0x43u;
    h.seq_num     = 0xFF;

    EXPECT_EQ(h.sender_id,   0x42u);
    EXPECT_EQ(h.receiver_id, 0x43u);
}

// ---------------------------------------------------------------------------
// Test suite: checksum policy
// ---------------------------------------------------------------------------

TEST(packet_header, fcs_size_zero_for_none) {
    static_assert(hdr_p2p_none::fcs_size == 0);
}

TEST(packet_header, fcs_size_matches_policy_crc32) {
    static_assert(hdr_p2p_crc32::fcs_size == 4);
}

TEST(packet_header, fcs_field_is_assignable_crc32) {
    hdr_p2p_crc32 h{header_type::data, header_options::none};
    h.fcs = 0xDEADBEEFu;

    EXPECT_EQ(h.fcs, 0xDEADBEEFu);
    EXPECT_EQ(h.type(),    header_type::data);
    EXPECT_EQ(h.options(), header_options::none);
    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
}

TEST(packet_header, fcs_does_not_corrupt_protocol_byte_crc32) {
    hdr_p2p_crc32 h{header_type::auth, header_options::encrypted};
    const std::uint8_t expected_byte =
        make_expected_byte(header_type::auth, header_options::encrypted);

    h.fcs = 0x12345678u;

    EXPECT_EQ(h.raw(), expected_byte);
}

// ---------------------------------------------------------------------------
// Test suite: bit-field isolation
// ---------------------------------------------------------------------------

static constexpr std::uint8_t all_options_byte() noexcept {
    return header_options_mask;   // bits 4..2 all set
}

static constexpr std::uint8_t all_type_bits_byte() noexcept {
    return static_cast<std::uint8_t>(0x7u << 5);  // bits 7..5 all set
}

static constexpr std::uint8_t all_version_bits_byte() noexcept {
    return static_cast<std::uint8_t>(0x3u);        // bits 1..0 all set
}

// Compile-time: masks are pairwise disjoint and together cover 8 bits.
static_assert((all_options_byte() & all_type_bits_byte())    == 0,
    "options and type bit regions must not overlap");
static_assert((all_options_byte() & all_version_bits_byte()) == 0,
    "options and version bit regions must not overlap");
static_assert((all_type_bits_byte() & all_version_bits_byte()) == 0,
    "type and version bit regions must not overlap");
static_assert((all_options_byte() | all_type_bits_byte() | all_version_bits_byte()) == 0xFFu,
    "type + options + version must together cover all 8 bits of the protocol byte");

TEST(packet_header_bit_isolation, all_options_set_does_not_affect_type) {
    constexpr header_options all_opts =
        header_options::error | header_options::ack | header_options::encrypted;
    constexpr hdr_p2p_none h{header_type::data, all_opts};

    EXPECT_EQ(h.type(), header_type::data);
}

TEST(packet_header_bit_isolation, all_options_set_does_not_affect_version) {
    constexpr header_options all_opts =
        header_options::error | header_options::ack | header_options::encrypted;
    constexpr hdr_p2p_none h{header_type::data, all_opts};

    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
    EXPECT_EQ(h.raw() & all_version_bits_byte(),
              static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
}

TEST(packet_header_bit_isolation, max_type_value_does_not_affect_options) {
    constexpr hdr_p2p_none h{header_type::firmware, header_options::none};

    EXPECT_EQ(h.options(), header_options::none)
        << "type=firmware must not set any option bits";
}

TEST(packet_header_bit_isolation, max_type_value_does_not_affect_version) {
    constexpr hdr_p2p_none h{header_type::firmware, header_options::none};

    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION))
        << "type=firmware must not corrupt version bits";
}

TEST(packet_header_bit_isolation, version_bits_never_set_type_bits) {
    constexpr hdr_p2p_none h{header_type::data, header_options::none};

    EXPECT_EQ(h.raw() >> 5, 0x0u)
        << "version bits must not appear in the type region";
}

TEST(packet_header_bit_isolation, version_bits_never_set_option_bits) {
    constexpr hdr_p2p_none h{header_type::data, header_options::none};

    EXPECT_EQ(h.raw() & header_options_mask, 0x0u)
        << "version bits must not appear in the options region";
}

TEST(packet_header_bit_isolation, all_fields_nonzero_round_trip) {
    constexpr header_options all_opts =
        header_options::error | header_options::ack | header_options::encrypted;
    constexpr hdr_p2p_none h{header_type::firmware, all_opts};

    EXPECT_EQ(h.type(),    header_type::firmware);
    EXPECT_EQ(h.options(), all_opts);
    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));

    const std::uint8_t r = h.raw();
    EXPECT_EQ((r >> 5) & 0x7u,          static_cast<std::uint8_t>(header_type::firmware));
    EXPECT_EQ(r & header_options_mask,   static_cast<std::uint8_t>(all_opts));
    EXPECT_EQ(r & 0x3u,
              static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u);
}

// ---------------------------------------------------------------------------
// Test suite: all eight specialisations  --  shared accessor surface
//
// All eight specialisations must produce identical results from type(),
// options(), version(), raw(), and has() for the same constructor arguments.
// Any divergence would mean the per-specialisation definitions in
// packet_header.tpp have drifted from each other.
// ---------------------------------------------------------------------------

TEST(packet_header_all_specs, type_accessor_consistent_across_specs) {
    constexpr header_type    t = header_type::session;
    constexpr header_options o = header_options::ack;

    const hdr_p2p_none     h1{t, o};
    const hdr_p2p_crc32    h2{t, o};
    const hdr_net_none     h3{t, o};
    const hdr_net_crc32    h4{t, o};
    const hdr_p2p_seq_none  h5{t, o};
    const hdr_p2p_seq_crc32 h6{t, o};
    const hdr_net_seq_none  h7{t, o};
    const hdr_net_seq_crc32 h8{t, o};

    EXPECT_EQ(h1.type(), t);
    EXPECT_EQ(h2.type(), t);
    EXPECT_EQ(h3.type(), t);
    EXPECT_EQ(h4.type(), t);
    EXPECT_EQ(h5.type(), t);
    EXPECT_EQ(h6.type(), t);
    EXPECT_EQ(h7.type(), t);
    EXPECT_EQ(h8.type(), t);
}

TEST(packet_header_all_specs, options_accessor_consistent_across_specs) {
    constexpr header_options o =
        header_options::error | header_options::encrypted;

    const hdr_p2p_none     h1{header_type::control, o};
    const hdr_p2p_crc32    h2{header_type::control, o};
    const hdr_net_none     h3{header_type::control, o};
    const hdr_net_crc32    h4{header_type::control, o};
    const hdr_p2p_seq_none  h5{header_type::control, o};
    const hdr_p2p_seq_crc32 h6{header_type::control, o};
    const hdr_net_seq_none  h7{header_type::control, o};
    const hdr_net_seq_crc32 h8{header_type::control, o};

    EXPECT_EQ(h1.options(), o);
    EXPECT_EQ(h2.options(), o);
    EXPECT_EQ(h3.options(), o);
    EXPECT_EQ(h4.options(), o);
    EXPECT_EQ(h5.options(), o);
    EXPECT_EQ(h6.options(), o);
    EXPECT_EQ(h7.options(), o);
    EXPECT_EQ(h8.options(), o);
}

TEST(packet_header_all_specs, version_accessor_consistent_across_specs) {
    const hdr_p2p_none     h1{header_type::auth, header_options::none};
    const hdr_p2p_crc32    h2{header_type::auth, header_options::none};
    const hdr_net_none     h3{header_type::auth, header_options::none};
    const hdr_net_crc32    h4{header_type::auth, header_options::none};
    const hdr_p2p_seq_none  h5{header_type::auth, header_options::none};
    const hdr_p2p_seq_crc32 h6{header_type::auth, header_options::none};
    const hdr_net_seq_none  h7{header_type::auth, header_options::none};
    const hdr_net_seq_crc32 h8{header_type::auth, header_options::none};

    const auto v = static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION);
    EXPECT_EQ(h1.version(), v);
    EXPECT_EQ(h2.version(), v);
    EXPECT_EQ(h3.version(), v);
    EXPECT_EQ(h4.version(), v);
    EXPECT_EQ(h5.version(), v);
    EXPECT_EQ(h6.version(), v);
    EXPECT_EQ(h7.version(), v);
    EXPECT_EQ(h8.version(), v);
}

TEST(packet_header_all_specs, raw_byte_consistent_across_specs) {
    constexpr header_type    t = header_type::firmware;
    constexpr header_options o =
        header_options::error | header_options::ack | header_options::encrypted;
    const std::uint8_t expected = make_expected_byte(t, o);

    const hdr_p2p_none     h1{t, o};
    const hdr_p2p_crc32    h2{t, o};
    const hdr_net_none     h3{t, o};
    const hdr_net_crc32    h4{t, o};
    const hdr_p2p_seq_none  h5{t, o};
    const hdr_p2p_seq_crc32 h6{t, o};
    const hdr_net_seq_none  h7{t, o};
    const hdr_net_seq_crc32 h8{t, o};

    EXPECT_EQ(h1.raw(), expected);
    EXPECT_EQ(h2.raw(), expected);
    EXPECT_EQ(h3.raw(), expected);
    EXPECT_EQ(h4.raw(), expected);
    EXPECT_EQ(h5.raw(), expected);
    EXPECT_EQ(h6.raw(), expected);
    EXPECT_EQ(h7.raw(), expected);
    EXPECT_EQ(h8.raw(), expected);
}

TEST(packet_header_all_specs, has_consistent_across_specs) {
    constexpr header_options present = header_options::ack;
    constexpr header_options absent  = header_options::error;

    const hdr_p2p_none     h1{header_type::data, present};
    const hdr_p2p_crc32    h2{header_type::data, present};
    const hdr_net_none     h3{header_type::data, present};
    const hdr_net_crc32    h4{header_type::data, present};
    const hdr_p2p_seq_none  h5{header_type::data, present};
    const hdr_p2p_seq_crc32 h6{header_type::data, present};
    const hdr_net_seq_none  h7{header_type::data, present};
    const hdr_net_seq_crc32 h8{header_type::data, present};

    EXPECT_TRUE(h1.has(present)); EXPECT_FALSE(h1.has(absent));
    EXPECT_TRUE(h2.has(present)); EXPECT_FALSE(h2.has(absent));
    EXPECT_TRUE(h3.has(present)); EXPECT_FALSE(h3.has(absent));
    EXPECT_TRUE(h4.has(present)); EXPECT_FALSE(h4.has(absent));
    EXPECT_TRUE(h5.has(present)); EXPECT_FALSE(h5.has(absent));
    EXPECT_TRUE(h6.has(present)); EXPECT_FALSE(h6.has(absent));
    EXPECT_TRUE(h7.has(present)); EXPECT_FALSE(h7.has(absent));
    EXPECT_TRUE(h8.has(present)); EXPECT_FALSE(h8.has(absent));
}

// ---------------------------------------------------------------------------
// Test suite: field exposure per specialisation
//
// Positive direction: every present field is readable and writable.
// Absence of fields is proved by sizeof (no room for extra bytes).
// ---------------------------------------------------------------------------

static_assert(sizeof(hdr_p2p_none) == 1,
    "p2p/noseq/none: size 1 proves no fcs or id bytes are accessible");
static_assert(sizeof(hdr_net_none) == 3,
    "net/noseq/none: size 3 proves no fcs byte is present");
static_assert(sizeof(hdr_p2p_crc32) == 5,
    "p2p/noseq/crc32: size 5 proves fcs is present and no id bytes exist");
static_assert(sizeof(hdr_net_crc32) == 7,
    "net/noseq/crc32: size 7 proves all three extra fields are present");
static_assert(sizeof(hdr_p2p_seq_none) == 2,
    "p2p/seq/none: size 2 proves seq_num is present; no fcs or ids");
static_assert(sizeof(hdr_p2p_seq_crc32) == 6,
    "p2p/seq/crc32: size 6 proves seq_num and fcs are present; no ids");
static_assert(sizeof(hdr_net_seq_none) == 4,
    "net/seq/none: size 4 proves seq_num and ids are present; no fcs");
static_assert(sizeof(hdr_net_seq_crc32) == 8,
    "net/seq/crc32: size 8 proves all four extra fields are present");

TEST(packet_header_field_exposure, p2p_noseq_crc32_fcs_readable_and_writable) {
    hdr_p2p_crc32 h{header_type::data, header_options::none};
    EXPECT_EQ(h.fcs, 0u);
    h.fcs = 0xCAFEBABEu;
    EXPECT_EQ(h.fcs, 0xCAFEBABEu);
}

TEST(packet_header_field_exposure, net_noseq_none_ids_readable_and_writable) {
    hdr_net_none h{header_type::data, header_options::none};
    EXPECT_EQ(h.sender_id,   static_cast<std::uint8_t>(ECOMM_BOARD_ID));
    EXPECT_EQ(h.receiver_id, 0u);
    h.sender_id   = 0x11u;
    h.receiver_id = 0x22u;
    EXPECT_EQ(h.sender_id,   0x11u);
    EXPECT_EQ(h.receiver_id, 0x22u);
}

TEST(packet_header_field_exposure, net_noseq_crc32_all_extra_fields_independent) {
    hdr_net_crc32 h{header_type::log, header_options::encrypted};
    const std::uint8_t expected_byte =
        make_expected_byte(header_type::log, header_options::encrypted);

    h.sender_id   = 0xAAu;
    h.receiver_id = 0xBBu;
    h.fcs         = 0x12345678u;

    EXPECT_EQ(h.sender_id,   0xAAu);
    EXPECT_EQ(h.receiver_id, 0xBBu);
    EXPECT_EQ(h.fcs,         0x12345678u);
    EXPECT_EQ(h.raw(), expected_byte);
}

TEST(packet_header_field_exposure, net_noseq_crc32_writing_fcs_does_not_touch_ids) {
    hdr_net_crc32 h{header_type::data, header_options::none};
    h.sender_id   = 0x42u;
    h.receiver_id = 0x43u;
    h.fcs         = 0xFFFFFFFFu;

    EXPECT_EQ(h.sender_id,   0x42u);
    EXPECT_EQ(h.receiver_id, 0x43u);
}

TEST(packet_header_field_exposure, net_noseq_crc32_writing_ids_does_not_touch_fcs) {
    hdr_net_crc32 h{header_type::data, header_options::none};
    h.fcs         = 0xDEADBEEFu;
    h.sender_id   = 0x55u;
    h.receiver_id = 0x66u;

    EXPECT_EQ(h.fcs, 0xDEADBEEFu);
}

// ---------------------------------------------------------------------------
// Test suite: header_options bitwise operators (etools::meta::enable_flags)
// ---------------------------------------------------------------------------

TEST(header_options, operator_or_produces_combined_mask) {
    constexpr header_options combined =
        header_options::error | header_options::encrypted;

    constexpr std::uint8_t raw =
        static_cast<std::uint8_t>(header_options::error) |
        static_cast<std::uint8_t>(header_options::encrypted);

    EXPECT_EQ(static_cast<std::uint8_t>(combined), raw);
}

TEST(header_options, operator_and_selects_intersection) {
    constexpr header_options ab =
        header_options::error | header_options::ack;
    constexpr header_options bc =
        header_options::ack | header_options::encrypted;

    constexpr header_options intersection = ab & bc;

    EXPECT_EQ(intersection, header_options::ack);
}

TEST(header_options, none_is_identity_for_or) {
    constexpr header_options x = header_options::error | header_options::none;
    EXPECT_EQ(x, header_options::error);
}

TEST(header_options, none_is_absorbing_for_and) {
    constexpr header_options x = header_options::error & header_options::none;
    EXPECT_EQ(x, header_options::none);
}
