// SPDX-License-Identifier: BSL-1.1
/**
* @file test_validator.cpp
*
* @brief Unit tests for ecomm::protocol::validator.
*
* @ingroup ecomm_tests
*
* These tests assume compute<> produces a correct checksum (covered separately)
* and focus purely on validator behaviour:
*
*   - none specialization: is_valid always returns true; seal is a no-op.
*   - Checksum specialization: a freshly constructed (unsealed) packet fails
*     is_valid; a sealed packet passes; corrupting any payload byte causes
*     is_valid to return false; corrupting the header byte causes is_valid to
*     return false; re-sealing after corruption restores validity.
*   - seal does not corrupt the header type, options, or payload.
*   - is_valid does not modify the packet (fcs and payload survive the call
*     intact — verifies the const_cast + restore contract).
*   - Tested checksum policies: sum8, crc8, crc16, crc32 (representative
*     widths; the compute logic is assumed correct for all).
*   - Tested topologies: point_to_point and network (verifies that the id
*     bytes are included in the checksum region — corrupting them must also
*     invalidate the packet).
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

#include <ecomm/protocol/validator.hpp>

using namespace ecomm::protocol;

// ---------------------------------------------------------------------------
// Typed test suite — run the same battery across multiple checksum policies
// ---------------------------------------------------------------------------

template<typename ChecksumPolicy>
struct validator_typed_test : ::testing::Test {
    using pkt_t = packet<32, topology::point_to_point, ChecksumPolicy>;
    using val_t = validator<pkt_t>;

    pkt_t pkt{header_type::data, header_options::none};
    val_t v{};

    void SetUp() override {
        // Seed the payload with a non-zero, non-uniform pattern so the checksum
        // of the unsealed packet is never accidentally zero, and single-byte
        // flips produce a different checksum for every policy (including sum8).
        for (std::size_t i = 0; i < pkt_t::payload_size; ++i)
            pkt.payload[i] = static_cast<std::byte>(0xA5 ^ (i & 0xFF));
    }
};

using checksum_policies = ::testing::Types<sum8, crc8, crc16, crc32>;
TYPED_TEST_SUITE(validator_typed_test, checksum_policies);

// ---------------------------------------------------------------------------
// A fresh packet (fcs == 0, never sealed) must not validate.
// ---------------------------------------------------------------------------

TYPED_TEST(validator_typed_test, unsealed_packet_is_invalid) {
    // fcs is zero-initialized; the correct checksum is almost certainly not zero.
    // The only degenerate case where this could pass is if the checksum of an
    // all-zero packet happens to equal zero — that does not occur for any of the
    // policies under test.
    EXPECT_FALSE(this->v.is_valid(this->pkt));
}

// ---------------------------------------------------------------------------
// seal then is_valid must pass.
// ---------------------------------------------------------------------------

TYPED_TEST(validator_typed_test, sealed_packet_is_valid) {
    this->v.seal(this->pkt);
    EXPECT_TRUE(this->v.is_valid(this->pkt));
}

// ---------------------------------------------------------------------------
// Corrupting a single payload byte must break validity.
// ---------------------------------------------------------------------------

TYPED_TEST(validator_typed_test, payload_corruption_detected) {
    this->v.seal(this->pkt);

    // Flip the first payload byte.
    this->pkt.payload[0] ^= std::byte{0xFF};

    EXPECT_FALSE(this->v.is_valid(this->pkt));
}

TYPED_TEST(validator_typed_test, payload_corruption_last_byte_detected) {
    this->v.seal(this->pkt);

    using pkt_t = typename TestFixture::pkt_t;
    this->pkt.payload[pkt_t::payload_size - 1] ^= std::byte{0x01};

    EXPECT_FALSE(this->v.is_valid(this->pkt));
}

// ---------------------------------------------------------------------------
// Corrupting the protocol (header) byte must break validity.
// ---------------------------------------------------------------------------

TYPED_TEST(validator_typed_test, header_byte_corruption_detected) {
    this->v.seal(this->pkt);

    // Flip a bit in the stored protocol byte via raw().
    // We write back through the raw byte by flipping the type field.
    // Easiest: rebuild the header with a different type.
    this->pkt.header = typename TestFixture::pkt_t::header_t{
        header_type::firmware, header_options::none
    };

    EXPECT_FALSE(this->v.is_valid(this->pkt));
}

// ---------------------------------------------------------------------------
// Re-sealing after corruption restores validity.
// ---------------------------------------------------------------------------

TYPED_TEST(validator_typed_test, reseal_after_corruption_restores_validity) {
    this->v.seal(this->pkt);
    this->pkt.payload[0] ^= std::byte{0xAB};

    // Must be invalid after corruption.
    EXPECT_FALSE(this->v.is_valid(this->pkt));

    // Reseal clears the old fcs first (sets it to 0), then recomputes.
    this->pkt.header.fcs = {};
    this->v.seal(this->pkt);

    EXPECT_TRUE(this->v.is_valid(this->pkt));
}

// ---------------------------------------------------------------------------
// is_valid must not modify the packet (const_cast + restore contract).
// ---------------------------------------------------------------------------

TYPED_TEST(validator_typed_test, is_valid_does_not_modify_fcs) {
    this->v.seal(this->pkt);
    const auto fcs_before = this->pkt.header.fcs;

    static_cast<void>(this->v.is_valid(this->pkt));

    EXPECT_EQ(this->pkt.header.fcs, fcs_before);
}

TYPED_TEST(validator_typed_test, is_valid_does_not_modify_payload) {
    // Fill payload with a known pattern, seal, then call is_valid.
    for (std::size_t i = 0; i < TestFixture::pkt_t::payload_size; ++i)
        this->pkt.payload[i] = static_cast<std::byte>(i & 0xFF);

    this->v.seal(this->pkt);
    static_cast<void>(this->v.is_valid(this->pkt));

    for (std::size_t i = 0; i < TestFixture::pkt_t::payload_size; ++i) {
        EXPECT_EQ(this->pkt.payload[i], static_cast<std::byte>(i & 0xFF))
            << "payload corrupted at index " << i;
    }
}

// ---------------------------------------------------------------------------
// seal must not corrupt the header type/options or the payload.
// ---------------------------------------------------------------------------

TYPED_TEST(validator_typed_test, seal_preserves_header_type_and_options) {
    using pkt_t = typename TestFixture::pkt_t;
    pkt_t p{header_type::firmware, header_options::encrypted};
    this->v.seal(p);

    EXPECT_EQ(p.header.type(),    header_type::firmware);
    EXPECT_TRUE(p.header.has(header_options::encrypted));
}

TYPED_TEST(validator_typed_test, seal_preserves_payload) {
    for (std::size_t i = 0; i < TestFixture::pkt_t::payload_size; ++i)
        this->pkt.payload[i] = static_cast<std::byte>(0xAA);

    this->v.seal(this->pkt);

    for (std::size_t i = 0; i < TestFixture::pkt_t::payload_size; ++i) {
        EXPECT_EQ(this->pkt.payload[i], std::byte{0xAA})
            << "payload corrupted at index " << i;
    }
}

// ---------------------------------------------------------------------------
// Network topology: id field corruption must also be detected.
// The id bytes live inside the header, which is part of the hashed region.
// ---------------------------------------------------------------------------

TEST(validator_network, sender_id_corruption_detected) {
    using pkt_t = packet<32, topology::network, crc32>;
    validator<pkt_t> v{};
    pkt_t p{header_type::data, header_options::none};
    p.header.sender_id   = 0x01;
    p.header.receiver_id = 0x02;

    v.seal(p);
    EXPECT_TRUE(v.is_valid(p));

    p.header.sender_id ^= 0xFF; // corrupt sender id
    EXPECT_FALSE(v.is_valid(p));
}

TEST(validator_network, receiver_id_corruption_detected) {
    using pkt_t = packet<32, topology::network, crc32>;
    validator<pkt_t> v{};
    pkt_t p{header_type::data, header_options::none};
    p.header.sender_id   = 0x01;
    p.header.receiver_id = 0x02;

    v.seal(p);
    EXPECT_TRUE(v.is_valid(p));

    p.header.receiver_id ^= 0xFF; // corrupt receiver id
    EXPECT_FALSE(v.is_valid(p));
}

// ---------------------------------------------------------------------------
// none specialization: is_valid always true, seal is a no-op.
// ---------------------------------------------------------------------------

TEST(validator_none, is_valid_always_true) {
    using pkt_t = packet<32>;
    validator<pkt_t> v{};
    pkt_t p{header_type::data, header_options::none};

    // Unseal, corrupted payload — none specialization must still say valid.
    p.payload[0] = std::byte{0xFF};
    EXPECT_TRUE(v.is_valid(p));
}

TEST(validator_none, seal_is_noop) {
    using pkt_t = packet<32>;
    validator<pkt_t> v{};
    pkt_t p{header_type::firmware, header_options::ack};

    // Fill payload with a pattern.
    for (std::size_t i = 0; i < pkt_t::payload_size; ++i)
        p.payload[i] = static_cast<std::byte>(i);

    v.seal(p);

    // Header must be unchanged.
    EXPECT_EQ(p.header.type(),    header_type::firmware);
    EXPECT_TRUE(p.header.has(header_options::ack));

    // Payload must be unchanged.
    for (std::size_t i = 0; i < pkt_t::payload_size; ++i) {
        EXPECT_EQ(p.payload[i], static_cast<std::byte>(i))
            << "payload corrupted at index " << i;
    }
}
