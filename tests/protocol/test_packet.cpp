// SPDX-License-Identifier: BSL-1.1
/**
* @file test_packet.cpp
*
* @brief Unit tests for ecomm::protocol::packet.
*
* @ingroup ecomm_tests
*
* These tests assume packet_header is correct (covered by test_packet_header.cpp)
* and focus exclusively on packet-level consistency:
*
*   - sizeof(packet) == PacketSize for every topology x checksum combination.
*   - payload_size == PacketSize - sizeof(header_t) for every combination.
*   - packet_size constant equals PacketSize.
*   - Default construction: header byte is zero, all payload bytes are zero.
*   - Two-parameter construction: header reflects the given type/opts,
*     payload is zero-initialized.
*   - Header and payload are non-overlapping and jointly cover the full PacketSize.
*   - Writing into payload does not corrupt the header; writing into the header
*     does not corrupt the payload.
*   - network topology: header id fields are accessible and assignable without
*     touching the payload.
*   - checksum policy: header fcs field is accessible and assignable without
*     touching the payload.
*   - header_t type alias resolves to the correct packet_header instantiation.
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
#include <type_traits>

#include <ecomm/protocol/packet.hpp>
#include <ecomm/protocol/checksum.hpp>

// ---------------------------------------------------------------------------
// Convenience aliases
// ---------------------------------------------------------------------------

using namespace ecomm::protocol;

using pkt_p2p_none  = packet<32, topology::point_to_point, none>;
using pkt_net_none  = packet<32, topology::network,        none>;
using pkt_p2p_crc8  = packet<32, topology::point_to_point, crc8>;
using pkt_p2p_crc16 = packet<32, topology::point_to_point, crc16>;
using pkt_p2p_crc32 = packet<32, topology::point_to_point, crc32>;
using pkt_p2p_crc64 = packet<32, topology::point_to_point, crc64>;
using pkt_net_crc32 = packet<32, topology::network,        crc32>;

// ---------------------------------------------------------------------------
// Compile-time layout assertions
//
// These fire at compile time. If any fails the translation unit does not
// compile and the binary is never produced.
// ---------------------------------------------------------------------------

// sizeof(packet) must equal PacketSize exactly.
static_assert(sizeof(pkt_p2p_none)  == 32, "p2p/none:  sizeof must equal PacketSize");
static_assert(sizeof(pkt_net_none)  == 32, "net/none:  sizeof must equal PacketSize");
static_assert(sizeof(pkt_p2p_crc8)  == 32, "p2p/crc8:  sizeof must equal PacketSize");
static_assert(sizeof(pkt_p2p_crc16) == 32, "p2p/crc16: sizeof must equal PacketSize");
static_assert(sizeof(pkt_p2p_crc32) == 32, "p2p/crc32: sizeof must equal PacketSize");
static_assert(sizeof(pkt_p2p_crc64) == 32, "p2p/crc64: sizeof must equal PacketSize");
static_assert(sizeof(pkt_net_crc32) == 32, "net/crc32: sizeof must equal PacketSize");

// packet_size constant must equal PacketSize.
static_assert(pkt_p2p_none::packet_size  == 32);
static_assert(pkt_net_none::packet_size  == 32);
static_assert(pkt_p2p_crc32::packet_size == 32);
static_assert(pkt_net_crc32::packet_size == 32);

// payload_size == PacketSize - sizeof(header_t).
// header sizes: p2p/none=1, net/none=3, p2p/crc8=2, p2p/crc16=3, p2p/crc32=5, p2p/crc64=9, net/crc32=7
static_assert(pkt_p2p_none::payload_size  == 31, "p2p/none:  payload_size == 32 - 1");
static_assert(pkt_net_none::payload_size  == 29, "net/none:  payload_size == 32 - 3");
static_assert(pkt_p2p_crc8::payload_size  == 30, "p2p/crc8:  payload_size == 32 - 2");
static_assert(pkt_p2p_crc16::payload_size == 29, "p2p/crc16: payload_size == 32 - 3");
static_assert(pkt_p2p_crc32::payload_size == 27, "p2p/crc32: payload_size == 32 - 5");
static_assert(pkt_p2p_crc64::payload_size == 23, "p2p/crc64: payload_size == 32 - 9");
static_assert(pkt_net_crc32::payload_size == 25, "net/crc32: payload_size == 32 - 7");

// header_t alias must resolve to the matching packet_header instantiation.
static_assert(std::is_same_v<
    pkt_p2p_none::header_t,
    packet_header<topology::point_to_point, none>
>, "header_t alias must match packet_header<point_to_point, none>");

static_assert(std::is_same_v<
    pkt_net_crc32::header_t,
    packet_header<topology::network, crc32>
>, "header_t alias must match packet_header<network, crc32>");

// payload_size + sizeof(header_t) must exactly reconstruct PacketSize.
static_assert(pkt_p2p_none::payload_size  + sizeof(pkt_p2p_none::header_t)  == 32);
static_assert(pkt_net_none::payload_size  + sizeof(pkt_net_none::header_t)  == 32);
static_assert(pkt_p2p_crc32::payload_size + sizeof(pkt_p2p_crc32::header_t) == 32);
static_assert(pkt_net_crc32::payload_size + sizeof(pkt_net_crc32::header_t) == 32);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns true iff all bytes in [ptr, ptr+n) are zero.
static bool all_zero(const std::byte* ptr, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        if (ptr[i] != std::byte{0}) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Test suite: default construction
// ---------------------------------------------------------------------------

TEST(packet, default_header_byte_is_zero) {
    constexpr pkt_p2p_none p{};
    EXPECT_EQ(p.header.raw(), 0x00u);
}

TEST(packet, default_payload_is_zero) {
    pkt_p2p_none p{};
    EXPECT_TRUE(all_zero(p.payload, pkt_p2p_none::payload_size));
}

TEST(packet, default_entire_packet_is_zero) {
    pkt_p2p_none p{};
    std::byte raw[sizeof(p)];
    std::memcpy(raw, &p, sizeof(p));
    EXPECT_TRUE(all_zero(raw, sizeof(p)));
}

// ---------------------------------------------------------------------------
// Test suite: two-parameter construction
// ---------------------------------------------------------------------------

TEST(packet, construction_sets_header_correctly) {
    pkt_p2p_none p{header_type::firmware, header_options::encrypted};
    EXPECT_EQ(p.header.type(),    header_type::firmware);
    EXPECT_TRUE(p.header.has(header_options::encrypted));
}

TEST(packet, construction_zero_initializes_payload) {
    pkt_p2p_none p{header_type::data, header_options::error};
    EXPECT_TRUE(all_zero(p.payload, pkt_p2p_none::payload_size));
}

// ---------------------------------------------------------------------------
// Test suite: memory layout — header and payload are adjacent and non-overlapping
// ---------------------------------------------------------------------------

TEST(packet, header_is_at_start_of_packet) {
    pkt_p2p_none p{};
    const void* packet_addr = &p;
    const void* header_addr = &p.header;
    EXPECT_EQ(packet_addr, header_addr);
}

TEST(packet, payload_immediately_follows_header) {
    pkt_p2p_none p{};
    const std::byte* expected_payload_start =
        reinterpret_cast<const std::byte*>(&p.header) + sizeof(p.header);
    EXPECT_EQ(reinterpret_cast<const std::byte*>(p.payload), expected_payload_start);
}

TEST(packet, header_and_payload_jointly_span_packet_size) {
    // Verify there is no gap or overlap: the byte immediately after payload
    // is the byte immediately after the whole packet.
    pkt_p2p_none p{};
    const std::byte* packet_end   = reinterpret_cast<const std::byte*>(&p) + sizeof(p);
    const std::byte* payload_end  = p.payload + pkt_p2p_none::payload_size;
    EXPECT_EQ(payload_end, packet_end);
}

// Same layout checks for a larger header (network + crc32).
TEST(packet, layout_network_crc32) {
    pkt_net_crc32 p{};
    const std::byte* base        = reinterpret_cast<const std::byte*>(&p);
    const std::byte* payload_ptr = reinterpret_cast<const std::byte*>(p.payload);

    EXPECT_EQ(reinterpret_cast<const std::byte*>(&p.header), base);
    EXPECT_EQ(payload_ptr, base + sizeof(pkt_net_crc32::header_t));
    EXPECT_EQ(payload_ptr + pkt_net_crc32::payload_size, base + 32);
}

// ---------------------------------------------------------------------------
// Test suite: isolation — payload writes do not corrupt the header
// ---------------------------------------------------------------------------

TEST(packet, payload_write_does_not_corrupt_header) {
    pkt_p2p_none p{header_type::session, header_options::ack};
    const std::uint8_t expected_raw = p.header.raw();

    std::memset(p.payload, 0xFF, pkt_p2p_none::payload_size);

    EXPECT_EQ(p.header.raw(), expected_raw);
}

TEST(packet, header_write_does_not_corrupt_payload) {
    pkt_p2p_none p{};
    std::memset(p.payload, 0xAB, pkt_p2p_none::payload_size);

    // Rebuild the header by constructing a fresh one and copying it in.
    p.header = pkt_p2p_none::header_t{header_type::auth, header_options::error};

    // Every payload byte must still be 0xAB.
    for (std::size_t i = 0; i < pkt_p2p_none::payload_size; ++i) {
        EXPECT_EQ(p.payload[i], std::byte{0xAB}) << "corrupted at index " << i;
    }
}

// ---------------------------------------------------------------------------
// Test suite: network topology — id fields accessible, isolated from payload
// ---------------------------------------------------------------------------

TEST(packet, network_ids_assignable) {
    pkt_net_none p{header_type::control, header_options::none};
    p.header.sender_id   = 0x11;
    p.header.receiver_id = 0x22;

    EXPECT_EQ(p.header.sender_id,   0x11u);
    EXPECT_EQ(p.header.receiver_id, 0x22u);
}

TEST(packet, network_id_write_does_not_corrupt_payload) {
    pkt_net_none p{};
    std::memset(p.payload, 0x55, pkt_net_none::payload_size);

    p.header.sender_id   = 0xFF;
    p.header.receiver_id = 0xFF;

    EXPECT_TRUE(all_zero(
        p.payload,
        0  // replaced below with a manual loop for the non-zero fill
    ));
    // Actually verify each byte is still 0x55.
    for (std::size_t i = 0; i < pkt_net_none::payload_size; ++i) {
        EXPECT_EQ(p.payload[i], std::byte{0x55}) << "corrupted at index " << i;
    }
}

// ---------------------------------------------------------------------------
// Test suite: checksum policy — fcs field accessible, isolated from payload
// ---------------------------------------------------------------------------

TEST(packet, fcs_field_assignable_crc32) {
    pkt_p2p_crc32 p{header_type::data, header_options::none};
    p.header.fcs = 0xCAFEBABEu;

    EXPECT_EQ(p.header.fcs, 0xCAFEBABEu);
}

TEST(packet, fcs_write_does_not_corrupt_payload) {
    pkt_p2p_crc32 p{};
    std::memset(p.payload, 0x77, pkt_p2p_crc32::payload_size);

    p.header.fcs = 0xFFFFFFFFu;

    for (std::size_t i = 0; i < pkt_p2p_crc32::payload_size; ++i) {
        EXPECT_EQ(p.payload[i], std::byte{0x77}) << "corrupted at index " << i;
    }
}

TEST(packet, payload_write_does_not_corrupt_fcs) {
    pkt_p2p_crc32 p{};
    p.header.fcs = 0x12345678u;

    std::memset(p.payload, 0xFF, pkt_p2p_crc32::payload_size);

    EXPECT_EQ(p.header.fcs, 0x12345678u);
}

// ---------------------------------------------------------------------------
// Test suite: different PacketSizes
// ---------------------------------------------------------------------------

TEST(packet, size_16_p2p_none) {
    using p16 = packet<16>;
    static_assert(sizeof(p16) == 16);
    static_assert(p16::payload_size == 15);
    p16 p{header_type::log, header_options::none};
    EXPECT_EQ(p.header.type(), header_type::log);
    EXPECT_TRUE(all_zero(p.payload, p16::payload_size));
}

TEST(packet, size_64_net_crc32) {
    using p64 = packet<64, topology::network, crc32>;
    static_assert(sizeof(p64) == 64);
    static_assert(p64::payload_size == 57); // 64 - 7
    p64 p{header_type::firmware, header_options::encrypted};
    EXPECT_EQ(p.header.type(), header_type::firmware);
    EXPECT_TRUE(p.header.has(header_options::encrypted));
    EXPECT_TRUE(all_zero(p.payload, p64::payload_size));
}
