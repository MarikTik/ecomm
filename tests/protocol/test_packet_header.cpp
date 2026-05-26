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
*   - has_network_ids and fcs_size compile-time constants are correct.
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

static_assert(!hdr_p2p_none::has_network_ids, "p2p topology must not have network ids");
static_assert( hdr_net_none::has_network_ids, "network topology must have network ids");

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
    EXPECT_FALSE(h.has(header_options::heartbeat));
    EXPECT_FALSE(h.has(header_options::encrypted));
}

TEST(packet_header, option_heartbeat_stored_correctly) {
    constexpr hdr_p2p_none h{header_type::control, header_options::heartbeat};
    EXPECT_EQ(h.options(), header_options::heartbeat);
    EXPECT_TRUE(h.has(header_options::heartbeat));
    EXPECT_FALSE(h.has(header_options::error));
    EXPECT_FALSE(h.has(header_options::encrypted));
}

TEST(packet_header, option_encrypted_stored_correctly) {
    constexpr hdr_p2p_none h{header_type::data, header_options::encrypted};
    EXPECT_EQ(h.options(), header_options::encrypted);
    EXPECT_TRUE(h.has(header_options::encrypted));
    EXPECT_FALSE(h.has(header_options::error));
    EXPECT_FALSE(h.has(header_options::heartbeat));
}

TEST(packet_header, option_all_flags_combined) {
    constexpr header_options all =
        header_options::error | header_options::heartbeat | header_options::encrypted;

    constexpr hdr_p2p_none h{header_type::firmware, all};

    EXPECT_TRUE(h.has(header_options::error));
    EXPECT_TRUE(h.has(header_options::heartbeat));
    EXPECT_TRUE(h.has(header_options::encrypted));
    // Subset test: has() must accept the full combination too.
    EXPECT_TRUE(h.has(all));
}

TEST(packet_header, has_returns_false_for_absent_subset) {
    constexpr hdr_p2p_none h{header_type::data, header_options::error};
    // heartbeat is not set, so a subset that includes it must return false.
    constexpr header_options absent = header_options::error | header_options::heartbeat;
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
        header_options::error | header_options::heartbeat | header_options::encrypted;

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
        header_options::error | header_options::heartbeat | header_options::encrypted;
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
            header_options::heartbeat,
            header_options::encrypted,
            header_options::error | header_options::heartbeat,
            header_options::error | header_options::encrypted,
            header_options::heartbeat | header_options::encrypted,
            header_options::error | header_options::heartbeat | header_options::encrypted
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
    static_assert(!hdr_p2p_none::has_network_ids,
        "point_to_point must not expose has_network_ids");
    // Verify the size stays 1 byte, implying no id fields exist.
    static_assert(sizeof(hdr_p2p_none) == 1,
        "p2p header must be 1 byte");
}

TEST(packet_header, network_topology_has_network_ids) {
    static_assert(hdr_net_none::has_network_ids,
        "network topology must expose has_network_ids");
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
        header_options::error | header_options::heartbeat;
    constexpr header_options bc =
        header_options::heartbeat | header_options::encrypted;

    constexpr header_options intersection = ab & bc;

    EXPECT_EQ(intersection, header_options::heartbeat);
}

TEST(header_options, none_is_identity_for_or) {
    constexpr header_options x = header_options::error | header_options::none;
    EXPECT_EQ(x, header_options::error);
}

TEST(header_options, none_is_absorbing_for_and) {
    constexpr header_options x = header_options::error & header_options::none;
    EXPECT_EQ(x, header_options::none);
}
