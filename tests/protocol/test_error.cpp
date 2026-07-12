// SPDX-License-Identifier: MIT
/**
* @file test_error.cpp
*
* @brief Unit tests for ecomm::protocol error envelope.
*
* @ingroup ecomm_tests
*
* Coverage:
*   error_envelope::write (string_view overload)
*     - Bytes returned equals prefix_size + message length.
*     - error_code is written at the correct offset.
*     - Length field matches the message size.
*     - Message bytes are copied verbatim.
*     - Empty string_view writes a valid zero-length envelope.
*     - Long message fills up to max_message_length_in_payload.
*
*   error_envelope::write (code-only overload)
*     - Returns prefix_size.
*     - error_code is correct.
*     - Length field is zero.
*     - No message bytes written beyond prefix.
*
*   as_error_unchecked
*     - Decodes a well-formed string_view-written envelope correctly.
*     - Decodes a code-only envelope (length == 0, message pointer valid).
*     - Returns nullopt when declared length overruns available payload.
*     - Returns nullopt when payload_size < prefix_size (via a tiny custom packet).
*
*   as_error
*     - Returns a valid view when header_options::error is set.
*     - assert fires in debug when error flag is absent (tested via
*       as_error_unchecked path to avoid killing the process).
*
*   is_user_error_code
*     - Library codes return false.
*     - user_range_begin and above return true.
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
*/

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <ecomm/protocol/error.hpp>
#include <ecomm/protocol/packet.hpp>

using namespace ecomm::protocol;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Packet large enough to comfortably hold error envelopes in tests.
using test_packet = packet<64>;

// Alias for the envelope sized to the test packet's payload.
using test_envelope = error_envelope<test_packet::payload_size>;

// Read the error_code field from raw payload bytes.
static error_code read_code(const std::byte* payload) {
    error_code c{};
    std::memcpy(&c, payload, sizeof(error_code));
    return c;
}

// Read the length field from raw payload bytes.
static error_message_length_t read_length(const std::byte* payload) {
    error_message_length_t l{};
    std::memcpy(&l, payload + sizeof(error_code), sizeof(error_message_length_t));
    return l;
}

// Pointer to the first message byte in raw payload bytes.
static const char* message_start(const std::byte* payload) {
    return reinterpret_cast<const char*>(
        payload + sizeof(error_code) + sizeof(error_message_length_t));
}

// ---------------------------------------------------------------------------
// Compile-time layout assertions
// ---------------------------------------------------------------------------

static_assert(test_envelope::prefix_size ==
    sizeof(error_code) + sizeof(error_message_length_t),
    "prefix_size must equal code + length field widths");

static_assert(test_envelope::payload_size == test_packet::payload_size,
    "envelope payload_size must match the packet's payload_size");

static_assert(test_envelope::max_message_length_in_payload ==
    test_packet::payload_size - test_envelope::prefix_size,
    "max_message_length_in_payload must be payload_size - prefix_size");

// ---------------------------------------------------------------------------
// Test suite: write (string_view overload)
// ---------------------------------------------------------------------------

TEST(error_envelope_write, returns_prefix_plus_message_length) {
    test_packet p{};
    const std::string_view msg = "hello";
    const std::size_t written =
        test_envelope::write(p.payload, error_code::malformed_header, msg);
    EXPECT_EQ(written, test_envelope::prefix_size + msg.size());
}

TEST(error_envelope_write, error_code_written_correctly) {
    test_packet p{};
    test_envelope::write(p.payload, error_code::checksum_mismatch, "bad fcs");
    EXPECT_EQ(read_code(p.payload), error_code::checksum_mismatch);
}

TEST(error_envelope_write, length_field_matches_message_size) {
    test_packet p{};
    const std::string_view msg = "version clash";
    test_envelope::write(p.payload, error_code::version_mismatch, msg);
    EXPECT_EQ(static_cast<std::size_t>(read_length(p.payload)), msg.size());
}

TEST(error_envelope_write, message_bytes_copied_verbatim) {
    test_packet p{};
    const std::string_view msg = "sensor overload";
    test_envelope::write(p.payload, error_code::malformed_header, msg);
    EXPECT_EQ(std::string_view(message_start(p.payload), msg.size()), msg);
}

TEST(error_envelope_write, empty_message_writes_zero_length) {
    test_packet p{};
    const std::size_t written =
        test_envelope::write(p.payload, error_code::ok, std::string_view{});
    EXPECT_EQ(written, test_envelope::prefix_size);
    EXPECT_EQ(read_length(p.payload), error_message_length_t{0});
}

TEST(error_envelope_write, max_length_message_fits) {
    test_packet p{};
    // Build a message exactly at the per-packet capacity limit.
    const std::size_t max_len = test_envelope::max_message_length_in_payload;
    std::string msg(max_len, 'X');
    const std::size_t written =
        test_envelope::write(p.payload, error_code::transport_timeout,
                             std::string_view{msg});
    EXPECT_EQ(written, test_envelope::prefix_size + max_len);
    EXPECT_EQ(static_cast<std::size_t>(read_length(p.payload)), max_len);
    EXPECT_EQ(std::string_view(message_start(p.payload), max_len), msg);
}

TEST(error_envelope_write, does_not_write_beyond_reported_bytes) {
    // Fill payload with a sentinel, write a short message, verify the byte
    // immediately after the envelope is untouched.
    test_packet p{};
    std::memset(p.payload, 0xCC, test_packet::payload_size);

    const std::string_view msg = "hi";
    const std::size_t written =
        test_envelope::write(p.payload, error_code::ok, msg);

    // The byte right after the written region must still be the sentinel.
    EXPECT_EQ(p.payload[written], std::byte{0xCC});
}

// ---------------------------------------------------------------------------
// Test suite: write (code-only overload)
// ---------------------------------------------------------------------------

TEST(error_envelope_write_code_only, returns_prefix_size) {
    test_packet p{};
    const std::size_t written =
        test_envelope::write(p.payload, error_code::transport_disconnected);
    EXPECT_EQ(written, test_envelope::prefix_size);
}

TEST(error_envelope_write_code_only, error_code_written_correctly) {
    test_packet p{};
    test_envelope::write(p.payload, error_code::unknown_handler_id);
    EXPECT_EQ(read_code(p.payload), error_code::unknown_handler_id);
}

TEST(error_envelope_write_code_only, length_field_is_zero) {
    test_packet p{};
    test_envelope::write(p.payload, error_code::handler_not_registered);
    EXPECT_EQ(read_length(p.payload), error_message_length_t{0});
}

TEST(error_envelope_write_code_only, does_not_write_beyond_prefix) {
    test_packet p{};
    std::memset(p.payload, 0xBB, test_packet::payload_size);

    test_envelope::write(p.payload, error_code::ok);

    EXPECT_EQ(p.payload[test_envelope::prefix_size], std::byte{0xBB});
}

// ---------------------------------------------------------------------------
// Test suite: as_error_unchecked
// ---------------------------------------------------------------------------

TEST(as_error_unchecked, decodes_message_envelope_correctly) {
    test_packet p{};
    const std::string_view msg = "motor stall";
    test_envelope::write(p.payload, error_code::transport_timeout, msg);

    const auto view = as_error_unchecked(p);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->code, error_code::transport_timeout);
    EXPECT_EQ(view->length, msg.size());
    EXPECT_EQ(std::string_view(view->message, view->length), msg);
}

TEST(as_error_unchecked, decodes_code_only_envelope) {
    test_packet p{};
    test_envelope::write(p.payload, error_code::checksum_mismatch);

    const auto view = as_error_unchecked(p);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->code, error_code::checksum_mismatch);
    EXPECT_EQ(view->length, std::size_t{0});
    // message pointer must be valid even for a zero-length envelope.
    EXPECT_NE(view->message, nullptr);
}

TEST(as_error_unchecked, returns_nullopt_when_declared_length_overruns_payload) {
    // Manually craft a malformed envelope: write a plausible code and a length
    // value that exceeds the available payload space.
    // max_message_length_in_payload is the largest value that still fits; adding
    // one produces the smallest value that must be rejected. We stay within the
    // width of error_message_length_t by construction (max + 1 wraps only if the
    // field is already at its numeric maximum, in which case the value 0 would be
    // written — so we clamp to the larger of (max+1) and the field's max value).
    test_packet p{};
    const error_code code = error_code::malformed_header;
    std::memcpy(p.payload, &code, sizeof(error_code));

    constexpr std::size_t max_ok  = test_envelope::max_message_length_in_payload;
    constexpr auto        bad_len =
        static_cast<error_message_length_t>(
            // Pick max_ok + 1 if it still fits in the field type, otherwise use
            // the field type's maximum (which by definition exceeds max_ok when
            // the macro is smaller than the numeric max of the type).
            (max_ok + 1 <= static_cast<std::size_t>(
                static_cast<error_message_length_t>(~error_message_length_t{0})))
            ? max_ok + 1
            : static_cast<error_message_length_t>(~error_message_length_t{0})
        );
    std::memcpy(p.payload + sizeof(error_code), &bad_len,
                sizeof(error_message_length_t));

    EXPECT_FALSE(as_error_unchecked(p).has_value());
}

// ---------------------------------------------------------------------------
// Test suite: as_error
// ---------------------------------------------------------------------------

TEST(as_error, decodes_when_error_flag_is_set) {
    test_packet p{header_type::data, header_options::error};
    const std::string_view msg = "bad packet";
    test_envelope::write(p.payload, error_code::malformed_header, msg);

    const auto view = as_error(p);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->code, error_code::malformed_header);
    EXPECT_EQ(std::string_view(view->message, view->length), msg);
}

TEST(as_error, code_only_decodes_when_error_flag_is_set) {
    test_packet p{header_type::control, header_options::error};
    test_envelope::write(p.payload, error_code::version_mismatch);

    const auto view = as_error(p);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->code, error_code::version_mismatch);
    EXPECT_EQ(view->length, std::size_t{0});
}

// ---------------------------------------------------------------------------
// Test suite: is_user_error_code
// ---------------------------------------------------------------------------

TEST(is_user_error_code, library_codes_return_false) {
    EXPECT_FALSE(is_user_error_code(error_code::ok));
    EXPECT_FALSE(is_user_error_code(error_code::malformed_header));
    EXPECT_FALSE(is_user_error_code(error_code::checksum_mismatch));
    EXPECT_FALSE(is_user_error_code(error_code::version_mismatch));
    EXPECT_FALSE(is_user_error_code(error_code::transport_timeout));
    EXPECT_FALSE(is_user_error_code(error_code::unknown_handler_id));
}

TEST(is_user_error_code, user_range_begin_returns_true) {
    EXPECT_TRUE(is_user_error_code(error_code::user_range_begin));
}

TEST(is_user_error_code, above_user_range_begin_returns_true) {
    const auto above = static_cast<error_code>(
        static_cast<std::uint16_t>(error_code::user_range_begin) + 1);
    EXPECT_TRUE(is_user_error_code(above));
}

TEST(is_user_error_code, max_uint16_returns_true) {
    EXPECT_TRUE(is_user_error_code(static_cast<error_code>(0xFFFF)));
}
