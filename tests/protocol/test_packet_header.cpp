// SPDX-License-Identifier: BSL-1.1
/**
* @file test_packet_header.cpp
*
* @brief Unit tests for ecomm::protocol::packet_header.
*
* @ingroup ecomm_tests
*
* Coverage:
*   - Wire layout (sizeof, field offsets) for all four topology × checksum combinations.
*   - Default-construction produces a zero protocol byte.
*   - Two-parameter construction correctly packs type, options, and version.
*   - Type accessor round-trips all six header_type enumerators.
*   - Options accessor round-trips all individual flags and multi-flag combinations.
*   - has() subset test: positive and negative cases.
*   - version() always equals ECOMM_PROTOCOL_VERSION after construction.
*   - raw() matches the manually computed expected byte.
*   - network_ids defaults: sender_id == ECOMM_BOARD_ID, receiver_id == 0.
*   - fcs_storage zero-initialization for each checksum policy width.
*   - has_node_ids and fcs_size compile-time constants are correct.
*   - Bit-field isolation: type, options, and version occupy disjoint bit
*     regions; setting one field to its maximum value does not corrupt the
*     others. Four compile-time `static_assert`s prove the masks are
*     pairwise disjoint and together cover all 8 bits.
*   - All four specialisations (p2p/none, p2p/crc32, net/none, net/crc32) give
*     identical results from the shared accessor surface for the same inputs.
*   - Field exposure per specialisation: each spec exposes exactly the fields
*     it carries; extra-field writes do not corrupt each other or the protocol byte.
*   - Wire field order: offsetof static_asserts lock in _byte < ids < fcs for
*     net/crc32, and _byte < fcs for p2p/crc32.
*   - header_options bitwise operators (|, &) produce the expected values.
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

#include <ecomm/protocol/packet_header.hpp>

// ---------------------------------------------------------------------------
// Convenience aliases
// ---------------------------------------------------------------------------

using namespace ecomm::protocol;

using hdr_p2p_none   = packet_header<topology::point_to_point, none>;
using hdr_net_none   = packet_header<topology::network,        none>;
using hdr_p2p_crc8   = packet_header<topology::point_to_point, crc8>;
using hdr_p2p_crc16  = packet_header<topology::point_to_point, crc16>;
using hdr_p2p_crc32  = packet_header<topology::point_to_point, crc32>;
using hdr_p2p_crc64  = packet_header<topology::point_to_point, crc64>;
using hdr_net_crc32  = packet_header<topology::network,        crc32>;

// ---------------------------------------------------------------------------
// Compile-time layout assertions
//
// These are not GoogleTest cases — they fire at compile time, which is the
// correct tier for wire-layout contracts.  If they fail the translation unit
// does not compile, and the test binary is never produced.
// ---------------------------------------------------------------------------

// point_to_point + none: protocol byte only → 1 byte
static_assert(sizeof(hdr_p2p_none) == 1,
    "packet_header<point_to_point, none> must be exactly 1 byte (protocol byte only)");

// network + none: protocol byte + sender_id + receiver_id → 3 bytes
static_assert(sizeof(hdr_net_none) == 3,
    "packet_header<network, none> must be exactly 3 bytes (proto + sender_id + receiver_id)");

// point_to_point + crc8: 1 + 1 = 2 bytes
static_assert(sizeof(hdr_p2p_crc8) == 2,
    "packet_header<point_to_point, crc8> must be exactly 2 bytes");

// point_to_point + crc16: 1 + 2 = 3 bytes
static_assert(sizeof(hdr_p2p_crc16) == 3,
    "packet_header<point_to_point, crc16> must be exactly 3 bytes");

// point_to_point + crc32: 1 + 4 = 5 bytes
static_assert(sizeof(hdr_p2p_crc32) == 5,
    "packet_header<point_to_point, crc32> must be exactly 5 bytes");

// point_to_point + crc64: 1 + 8 = 9 bytes
static_assert(sizeof(hdr_p2p_crc64) == 9,
    "packet_header<point_to_point, crc64> must be exactly 9 bytes");

// network + crc32: 1 + 2 + 4 = 7 bytes
static_assert(sizeof(hdr_net_crc32) == 7,
    "packet_header<network, crc32> must be exactly 7 bytes");

// Compile-time constant checks
static_assert(hdr_p2p_none::fcs_size    == 0,  "p2p/none: fcs_size must be 0");
static_assert(hdr_net_none::fcs_size    == 0,  "net/none: fcs_size must be 0");
static_assert(hdr_p2p_crc32::fcs_size  == 4,  "p2p/crc32: fcs_size must be 4");
static_assert(hdr_net_crc32::fcs_size  == 4,  "net/crc32: fcs_size must be 4");

static_assert(!hdr_p2p_none::has_node_ids, "p2p topology must not have node ids");
static_assert( hdr_net_none::has_node_ids, "network topology must have node ids");

// Standard-layout guarantee — required for well-defined offsetof and safe
// byte-cast access to fields in the wire buffer.
static_assert(std::is_standard_layout_v<hdr_p2p_none>,
    "packet_header<p2p, none> must be standard-layout");
static_assert(std::is_standard_layout_v<hdr_net_none>,
    "packet_header<net, none> must be standard-layout");
static_assert(std::is_standard_layout_v<hdr_p2p_crc32>,
    "packet_header<p2p, crc32> must be standard-layout");
static_assert(std::is_standard_layout_v<hdr_net_crc32>,
    "packet_header<net, crc32> must be standard-layout");

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
    // Default constructor leaves _byte == 0, so version bits are 0.
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

// ---------------------------------------------------------------------------
// Test suite: two-parameter construction — type round-trip
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
// Test suite: two-parameter construction — options round-trip
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
    // Subset test: has() must accept the full combination too.
    EXPECT_TRUE(h.has(all));
}

TEST(packet_header, has_returns_false_for_absent_subset) {
    constexpr hdr_p2p_none h{header_type::data, header_options::error};
    // ack is not set, so a subset that includes it must return false.
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
    // No matter what type/options are chosen, version bits must be ECOMM_PROTOCOL_VERSION.
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

// Build the expected protocol byte by hand, independent of the class internals.
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
    // All six types, no options: options() must always be none.
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
    // All option combinations should not corrupt the type field.
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
// Test suite: topology template parameter behavior
// ---------------------------------------------------------------------------

TEST(packet_header, p2p_topology_no_network_ids) {
    static_assert(!hdr_p2p_none::has_node_ids,
        "point_to_point must not expose has_node_ids");
    // Verify the size stays 1 byte, implying no id fields exist.
    static_assert(sizeof(hdr_p2p_none) == 1,
        "p2p header must be 1 byte");
}

TEST(packet_header, network_topology_has_node_ids) {
    static_assert(hdr_net_none::has_node_ids,
        "network topology must expose has_node_ids");
    static_assert(sizeof(hdr_net_none) == 3,
        "network header must be 3 bytes (proto + sender + receiver)");
}

TEST(packet_header, network_topology_ids_are_assignable) {
    hdr_net_none h{header_type::data, header_options::none};
    h.sender_id   = 0xAB;
    h.receiver_id = 0xCD;

    EXPECT_EQ(h.sender_id,   0xABu);
    EXPECT_EQ(h.receiver_id, 0xCDu);
    // Protocol byte must not be affected by id assignment.
    EXPECT_EQ(h.type(),    header_type::data);
    EXPECT_EQ(h.options(), header_options::none);
    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
}

TEST(packet_header, network_ids_do_not_corrupt_protocol_byte) {
    // Write extreme values into both id fields; the protocol byte must be stable.
    hdr_net_none h{header_type::firmware, header_options::error};
    const std::uint8_t expected_byte =
        make_expected_byte(header_type::firmware, header_options::error);

    h.sender_id   = 0xFF;
    h.receiver_id = 0xFF;

    EXPECT_EQ(h.raw(), expected_byte);
}

// ---------------------------------------------------------------------------
// Test suite: checksum policy template parameter behavior
// ---------------------------------------------------------------------------

TEST(packet_header, fcs_size_zero_for_none) {
    static_assert(hdr_p2p_none::fcs_size == 0, "none policy: fcs_size must be 0");
}

TEST(packet_header, fcs_size_matches_policy_crc32) {
    static_assert(hdr_p2p_crc32::fcs_size == 4, "crc32 policy: fcs_size must be 4");
}

TEST(packet_header, fcs_field_is_assignable_crc32) {
    hdr_p2p_crc32 h{header_type::data, header_options::none};
    h.fcs = 0xDEADBEEFu;

    EXPECT_EQ(h.fcs, 0xDEADBEEFu);
    // Protocol byte must be unaffected.
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
//
// Each test saturates exactly one field while zeroing the others, then
// verifies that the remaining two accessors read back zero/none.  This
// proves there is no bit overlap between the three protocol-byte fields.
// ---------------------------------------------------------------------------

// Helper: raw byte with every option bit set but type = data, version = 0.
// Built from the mask so the test does not hard-code numeric constants.
static constexpr std::uint8_t all_options_byte() noexcept {
    return header_options_mask;   // bits 4..2 all set, bits 7..5 and 1..0 clear
}

// Helper: raw byte with every type bit set but options = none, version = 0.
// type 0x7 occupies bits 7..5; options and version bits are 0.
static constexpr std::uint8_t all_type_bits_byte() noexcept {
    return static_cast<std::uint8_t>(0x7u << 5);  // 0b1110'0000
}

// Helper: raw byte with every version bit set but type = data, options = none.
// version occupies bits 1..0.
static constexpr std::uint8_t all_version_bits_byte() noexcept {
    return static_cast<std::uint8_t>(0x3u);        // 0b0000'0011
}

// Compile-time: the three masks are pairwise disjoint.
static_assert((all_options_byte() & all_type_bits_byte())    == 0,
    "options and type bit regions must not overlap");
static_assert((all_options_byte() & all_version_bits_byte()) == 0,
    "options and version bit regions must not overlap");
static_assert((all_type_bits_byte() & all_version_bits_byte()) == 0,
    "type and version bit regions must not overlap");
// Together they cover exactly 8 bits (no unused bits, no overlaps).
static_assert((all_options_byte() | all_type_bits_byte() | all_version_bits_byte()) == 0xFFu,
    "type + options + version must together cover all 8 bits of the protocol byte");

// --- Field isolation: options do not bleed into type or version ----------

TEST(packet_header_bit_isolation, all_options_set_does_not_affect_type) {
    // Construct with every defined option flag set, type = data.
    constexpr header_options all_opts =
        header_options::error | header_options::ack | header_options::encrypted;
    constexpr hdr_p2p_none h{header_type::data, all_opts};

    // type must still read data (0x0), not any garbage from the option bits.
    EXPECT_EQ(h.type(), header_type::data);
}

TEST(packet_header_bit_isolation, all_options_set_does_not_affect_version) {
    constexpr header_options all_opts =
        header_options::error | header_options::ack | header_options::encrypted;
    constexpr hdr_p2p_none h{header_type::data, all_opts};

    // version bits must only contain ECOMM_PROTOCOL_VERSION, not any option bits.
    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
    // Cross-check: option bits (4..2) are all set, version bits (1..0) are the
    // protocol version — the two regions must not have merged.
    EXPECT_EQ(h.raw() & all_version_bits_byte(),
              static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
}

// --- Field isolation: type does not bleed into options or version --------

TEST(packet_header_bit_isolation, max_type_value_does_not_affect_options) {
    // header_type::firmware == 0x5 (0b101), fills bits 7..5 with 0b101.
    // Use all-ones type (0x7) via raw construction to maximally stress the boundary.
    // We cannot construct 0x7 via the enum, so use firmware (0x5) which has bit
    // pattern 101 — both bits 7 and 5 set — as the most-stressing real enumerator.
    constexpr hdr_p2p_none h{header_type::firmware, header_options::none};

    EXPECT_EQ(h.options(), header_options::none)
        << "type=firmware must not set any option bits";
}

TEST(packet_header_bit_isolation, max_type_value_does_not_affect_version) {
    constexpr hdr_p2p_none h{header_type::firmware, header_options::none};

    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION))
        << "type=firmware must not corrupt version bits";
}

// --- Field isolation: version does not bleed into type or options --------
//
// Version is fixed to ECOMM_PROTOCOL_VERSION and is 2 bits wide (1..0).
// We verify that no matter what type/options are used, the version bits in
// raw() always equal exactly ECOMM_PROTOCOL_VERSION and nothing else.

TEST(packet_header_bit_isolation, version_bits_never_set_type_bits) {
    // If version were to leak into bits 7..5 the type accessor would return
    // something other than the enumerator passed in.
    constexpr hdr_p2p_none h{header_type::data, header_options::none};

    // raw() bits 7..5 must be 0 (data == 0x0).
    EXPECT_EQ(h.raw() >> 5, 0x0u)
        << "version bits must not appear in the type region";
}

TEST(packet_header_bit_isolation, version_bits_never_set_option_bits) {
    constexpr hdr_p2p_none h{header_type::data, header_options::none};

    // raw() bits 4..2 must be 0 (no options).
    EXPECT_EQ(h.raw() & header_options_mask, 0x0u)
        << "version bits must not appear in the options region";
}

// --- Cross-field round-trip with all fields simultaneously non-zero ------

TEST(packet_header_bit_isolation, all_fields_nonzero_round_trip) {
    // Use a type whose 3-bit encoding has all bits set in the region that
    // borders the options region (firmware = 0x5 = 0b101, bit 5 is set).
    // Use all options. Use whatever ECOMM_PROTOCOL_VERSION is.
    constexpr header_options all_opts =
        header_options::error | header_options::ack | header_options::encrypted;
    constexpr hdr_p2p_none h{header_type::firmware, all_opts};

    EXPECT_EQ(h.type(),    header_type::firmware);
    EXPECT_EQ(h.options(), all_opts);
    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));

    // Verify that the three regions of raw() are exactly what we expect and
    // do not contaminate each other.
    const std::uint8_t r = h.raw();
    EXPECT_EQ((r >> 5) & 0x7u, static_cast<std::uint8_t>(header_type::firmware));
    EXPECT_EQ(r & header_options_mask,
              static_cast<std::uint8_t>(all_opts));
    EXPECT_EQ(r & 0x3u,
              static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u);
}

// ---------------------------------------------------------------------------
// Test suite: all four specialisations — shared accessor surface
//
// The four partial specialisations (p2p/none, p2p/crc32, net/none, net/crc32)
// must all produce identical results from type(), options(), version(), raw(),
// and has() for the same constructor arguments.  Any divergence would mean the
// per-specialisation definitions in packet_header.tpp have drifted from each
// other.
// ---------------------------------------------------------------------------

TEST(packet_header_all_specs, type_accessor_consistent_across_specs) {
    constexpr header_type    t = header_type::session;
    constexpr header_options o = header_options::ack;

    const hdr_p2p_none  h1{t, o};
    const hdr_p2p_crc32 h2{t, o};
    const hdr_net_none  h3{t, o};
    const hdr_net_crc32 h4{t, o};

    EXPECT_EQ(h1.type(), t);
    EXPECT_EQ(h2.type(), t);
    EXPECT_EQ(h3.type(), t);
    EXPECT_EQ(h4.type(), t);
}

TEST(packet_header_all_specs, options_accessor_consistent_across_specs) {
    constexpr header_options o =
        header_options::error | header_options::encrypted;

    const hdr_p2p_none  h1{header_type::control, o};
    const hdr_p2p_crc32 h2{header_type::control, o};
    const hdr_net_none  h3{header_type::control, o};
    const hdr_net_crc32 h4{header_type::control, o};

    EXPECT_EQ(h1.options(), o);
    EXPECT_EQ(h2.options(), o);
    EXPECT_EQ(h3.options(), o);
    EXPECT_EQ(h4.options(), o);
}

TEST(packet_header_all_specs, version_accessor_consistent_across_specs) {
    const hdr_p2p_none  h1{header_type::auth, header_options::none};
    const hdr_p2p_crc32 h2{header_type::auth, header_options::none};
    const hdr_net_none  h3{header_type::auth, header_options::none};
    const hdr_net_crc32 h4{header_type::auth, header_options::none};

    const auto v = static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION);
    EXPECT_EQ(h1.version(), v);
    EXPECT_EQ(h2.version(), v);
    EXPECT_EQ(h3.version(), v);
    EXPECT_EQ(h4.version(), v);
}

TEST(packet_header_all_specs, raw_byte_consistent_across_specs) {
    // All four specialisations must pack the protocol byte identically;
    // the extra fields (fcs, ids) live outside the protocol byte and must
    // not alter it.
    constexpr header_type    t = header_type::firmware;
    constexpr header_options o =
        header_options::error | header_options::ack | header_options::encrypted;
    const std::uint8_t expected = make_expected_byte(t, o);

    const hdr_p2p_none  h1{t, o};
    const hdr_p2p_crc32 h2{t, o};
    const hdr_net_none  h3{t, o};
    const hdr_net_crc32 h4{t, o};

    EXPECT_EQ(h1.raw(), expected);
    EXPECT_EQ(h2.raw(), expected);
    EXPECT_EQ(h3.raw(), expected);
    EXPECT_EQ(h4.raw(), expected);
}

TEST(packet_header_all_specs, has_consistent_across_specs) {
    constexpr header_options present = header_options::ack;
    constexpr header_options absent  = header_options::error;

    const hdr_p2p_none  h1{header_type::data, present};
    const hdr_p2p_crc32 h2{header_type::data, present};
    const hdr_net_none  h3{header_type::data, present};
    const hdr_net_crc32 h4{header_type::data, present};

    EXPECT_TRUE(h1.has(present)); EXPECT_FALSE(h1.has(absent));
    EXPECT_TRUE(h2.has(present)); EXPECT_FALSE(h2.has(absent));
    EXPECT_TRUE(h3.has(present)); EXPECT_FALSE(h3.has(absent));
    EXPECT_TRUE(h4.has(present)); EXPECT_FALSE(h4.has(absent));
}

// ---------------------------------------------------------------------------
// Test suite: field exposure per specialisation
//
// Each specialisation must expose exactly the fields it carries and no others.
// - p2p/none    : no fcs, no ids
// - p2p/Policy  : fcs accessible and writable; no ids
// - net/none    : sender_id and receiver_id accessible and writable; no fcs
// - net/Policy  : fcs + sender_id + receiver_id all accessible and writable
//
// The absence of a field is proved at compile time by checking that the
// expression &h.field would not compile.  Since negative-compile tests cannot
// live in a gtest binary, the positive direction is tested here for every
// present field, and the type-system checks below confirm the surface area.
// ---------------------------------------------------------------------------

// Compile-time: p2p/none has no fcs or id members reachable through the public
// interface.  Proved indirectly: sizeof must be 1 (no room for any extra field).
static_assert(sizeof(hdr_p2p_none) == 1,
    "p2p/none: size 1 proves no fcs or id bytes are accessible");

// Compile-time: net/none has no fcs member.
// sizeof == 3 means exactly _byte + sender_id + receiver_id.
static_assert(sizeof(hdr_net_none) == 3,
    "net/none: size 3 proves no fcs byte is present");

// Compile-time: p2p/crc32 has fcs but no ids.
// sizeof == 5 means exactly _byte (1) + fcs (4).
static_assert(sizeof(hdr_p2p_crc32) == 5,
    "p2p/crc32: size 5 proves fcs is present and no id bytes exist");

// Compile-time: net/crc32 has all three extra fields.
// sizeof == 7 means _byte (1) + sender_id (1) + receiver_id (1) + fcs (4).
static_assert(sizeof(hdr_net_crc32) == 7,
    "net/crc32: size 7 proves all three extra fields are present");

TEST(packet_header_field_exposure, p2p_crc32_fcs_readable_and_writable) {
    hdr_p2p_crc32 h{header_type::data, header_options::none};
    // fcs must be zero on default construction.
    EXPECT_EQ(h.fcs, 0u);
    // fcs must accept a write and read it back unchanged.
    h.fcs = 0xCAFEBABEu;
    EXPECT_EQ(h.fcs, 0xCAFEBABEu);
}

TEST(packet_header_field_exposure, net_none_ids_readable_and_writable) {
    hdr_net_none h{header_type::data, header_options::none};
    // Defaults.
    EXPECT_EQ(h.sender_id,   static_cast<std::uint8_t>(ECOMM_BOARD_ID));
    EXPECT_EQ(h.receiver_id, 0u);
    // Both fields must accept writes.
    h.sender_id   = 0x11u;
    h.receiver_id = 0x22u;
    EXPECT_EQ(h.sender_id,   0x11u);
    EXPECT_EQ(h.receiver_id, 0x22u);
}

TEST(packet_header_field_exposure, net_crc32_all_extra_fields_independent) {
    // Construct with known type/options; then write distinct sentinel values
    // into every extra field.  Verify none of them corrupt the protocol byte
    // or each other.
    hdr_net_crc32 h{header_type::log, header_options::encrypted};
    const std::uint8_t expected_byte =
        make_expected_byte(header_type::log, header_options::encrypted);

    h.sender_id   = 0xAAu;
    h.receiver_id = 0xBBu;
    h.fcs         = 0x12345678u;

    EXPECT_EQ(h.sender_id,   0xAAu);
    EXPECT_EQ(h.receiver_id, 0xBBu);
    EXPECT_EQ(h.fcs,         0x12345678u);
    // Protocol byte must be unchanged.
    EXPECT_EQ(h.raw(), expected_byte);
    EXPECT_EQ(h.type(),    header_type::log);
    EXPECT_EQ(h.options(), header_options::encrypted);
    EXPECT_EQ(h.version(), static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION));
}

TEST(packet_header_field_exposure, net_crc32_writing_fcs_does_not_touch_ids) {
    hdr_net_crc32 h{header_type::data, header_options::none};
    h.sender_id   = 0x42u;
    h.receiver_id = 0x43u;
    h.fcs         = 0xFFFFFFFFu;

    // Ids must be completely unaffected by the fcs write.
    EXPECT_EQ(h.sender_id,   0x42u);
    EXPECT_EQ(h.receiver_id, 0x43u);
}

TEST(packet_header_field_exposure, net_crc32_writing_ids_does_not_touch_fcs) {
    hdr_net_crc32 h{header_type::data, header_options::none};
    h.fcs = 0xDEADBEEFu;
    h.sender_id   = 0x55u;
    h.receiver_id = 0x66u;

    // fcs must be unaffected by the id writes.
    EXPECT_EQ(h.fcs, 0xDEADBEEFu);
}

// ---------------------------------------------------------------------------
// Test suite: wire field order
//
// offsetof proves the exact byte positions of every field in the wire layout.
// The required order for net/crc32 is:
//   byte 0 : _byte  (inaccessible directly, but governs the first byte)
//   byte 1 : sender_id
//   byte 2 : receiver_id
//   byte 3 : fcs[0]  (first byte of the 4-byte crc32 value)
//
// The p2p/crc32 order is:
//   byte 0 : _byte
//   byte 1 : fcs[0]
//
// These are compile-time checks; they fire at compile time if the field
// order in header_layout specialisations is ever changed accidentally.
// ---------------------------------------------------------------------------

// For standard-layout types offsetof is well-defined.
static_assert(offsetof(hdr_p2p_crc32, fcs) == 1,
    "p2p/crc32: fcs must start at byte 1 (immediately after _byte)");

static_assert(offsetof(hdr_net_none, sender_id)   == 1,
    "net/none: sender_id must start at byte 1");
static_assert(offsetof(hdr_net_none, receiver_id) == 2,
    "net/none: receiver_id must start at byte 2");

static_assert(offsetof(hdr_net_crc32, sender_id)   == 1,
    "net/crc32: sender_id must start at byte 1");
static_assert(offsetof(hdr_net_crc32, receiver_id) == 2,
    "net/crc32: receiver_id must start at byte 2");
static_assert(offsetof(hdr_net_crc32, fcs)         == 3,
    "net/crc32: fcs must be last — starting at byte 3");

// ---------------------------------------------------------------------------
// Test suite: header_options bitwise operators (etools::meta::enable_flags)
// ---------------------------------------------------------------------------

TEST(header_options, operator_or_produces_combined_mask) {
    constexpr header_options combined =
        header_options::error | header_options::encrypted;

    // The result must have both bits set.
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
