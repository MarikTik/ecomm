// SPDX-License-Identifier: BSL-1.1
/**
* @file error.tpp
*
* @brief Implementation of error envelope read/write operations.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-25
*
* @copyright
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2026 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-25 Initial creation.
*/
#ifndef ECOMM_PROTOCOL_ERROR_TPP_
#define ECOMM_PROTOCOL_ERROR_TPP_

#include <cassert>
#include <cstring>

#include "error.hpp"
#include "packet_header.hpp"

namespace ecomm::protocol {

    template<std::size_t PayloadSize>
    inline std::size_t error_envelope<PayloadSize>::write(
        std::byte* payload_begin,
        error_code code,
        const char* message,
        error_message_length_t length
    ) noexcept {
        assert(payload_begin != nullptr && "error_envelope::write: payload_begin is null");
        assert(length <= max_message_length_in_payload &&
               "error_envelope::write: length exceeds this packet's message capacity");
        assert((length == 0 || message != nullptr) &&
               "error_envelope::write: non-zero length with null message");

        std::byte* cursor = payload_begin;

        // error_code (uint16, LE on supported targets)
        std::memcpy(cursor, &code, sizeof(error_code));
        cursor += sizeof(error_code);

        // length (width depends on ECOMM_MAX_ERROR_MESSAGE_LENGTH)
        std::memcpy(cursor, &length, sizeof(error_message_length_t));
        cursor += sizeof(error_message_length_t);

        // message body
        if (length != 0) {
            std::memcpy(cursor, message, length);
        }

        return prefix_size + length;
    }

    template<typename Packet>
    inline std::optional<error_view> as_error_unchecked(const Packet& packet) noexcept {
        constexpr std::size_t prefix_size =
            sizeof(error_code) + sizeof(error_message_length_t);

        // Structural check: payload must at least fit the prefix.
        if (Packet::payload_size < prefix_size) {
            return std::nullopt;
        }

        const std::byte* cursor = &packet.payload[0];

        error_code code{};
        std::memcpy(&code, cursor, sizeof(error_code));
        cursor += sizeof(error_code);

        error_message_length_t length{};
        std::memcpy(&length, cursor, sizeof(error_message_length_t));
        cursor += sizeof(error_message_length_t);

        // Wire-validity check: declared length must fit in the remaining payload.
        const std::size_t available = Packet::payload_size - prefix_size;
        if (static_cast<std::size_t>(length) > available) {
            return std::nullopt;
        }

        return error_view{
            code,
            reinterpret_cast<const char*>(cursor),
            static_cast<std::size_t>(length),
        };
    }

    template<typename Packet>
    inline std::optional<error_view> as_error(const Packet& packet) noexcept {
        assert(
            packet.header.has(header_options::error) &&
            "as_error: called on a packet whose header does not have the error flag set"
        );
        return as_error_unchecked(packet);
    }

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_ERROR_TPP_
