// SPDX-License-Identifier: MIT
/**
* @file error.hpp
*
* @brief Error envelope for the ecomm communication protocol.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* When `header_flags::error` is set on a packet, its payload region is reinterpreted as an
* `error_envelope`: a fixed-size `error_code` followed by a length-prefixed message. This
* file defines the wire format, the read-side view (`error_view`), and the write/read free
* functions (`write_error`, `as_error`).
*
* Wire layout inside the packet payload (for a payload of size `P`):
* ```
* +----------------+----------------------------+---------------------+----------+
* |   error_code   |   error_message_length_t   |    message bytes    |   pad    |
* |   (uint16_t)   |  (uint8/16/32, see below)  |    length bytes     |   ...    |
* +----------------+----------------------------+---------------------+----------+
*         2                  1, 2, or 4                  length          rest of P
* ```
*
* The width of the length field is selected at compile time from `ECOMM_MAX_ERROR_MESSAGE_LENGTH`
* (default 65535) via `etools::meta::smallest_uint_t`. Lowering the macro shrinks the wire
* footprint of every error envelope across the build.
*
* @note `status_code` on the packet header is orthogonal to the protocol-level error code
*       carried here. `status_code` belongs to the application layer; this envelope
*       belongs to the protocol layer.
*
* @warning Endianness: the ecomm wire protocol is defined as little-endian. Until a
*          dedicated LE/BE conversion sweep lands across the whole protocol, this file
*          performs raw `memcpy` between the wire bytes and the in-memory integers.
*          A `static_assert` below makes a build on a big-endian host fail loudly rather
*          than silently produce wrong wire bytes. When the sweep lands, the asserts go
*          away and explicit byte-swap helpers take their place.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-25
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-25 Initial creation.
*/
#ifndef ECOMM_PROTOCOL_ERROR_HPP_
#define ECOMM_PROTOCOL_ERROR_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <etools/meta/traits.hpp>

#include "config.hpp"

// See @warning in this file's header block. This is a load-bearing assumption of every
// memcpy in error.tpp; remove it only as part of the protocol-wide endianness sweep.
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
    "ecomm currently assumes a little-endian host. A big-endian build would silently "
    "produce wrong on-wire bytes for the error envelope. The protocol-wide LE/BE sweep "
    "is pending; until then, building on a big-endian target is not supported."
);
#endif

namespace ecomm::protocol {

    /**
    * @enum error_code
    *
    * @brief Protocol-level error identifiers carried in an error envelope.
    *
    * The top byte is a subsystem tag so host-side dispatch can fan out without a giant switch:
    * - `0x00xx`  --  framing / packet structure
    * - `0x01xx`  --  transport (serial, wifi, ...)
    * - `0x02xx`  --  dispatch / hub
    * - `0x40xx`+  --  reserved for user-defined application errors (see `user_range_begin`).
    *
    * Codes `0x0000` .. `0x3FFF` are reserved for the ecomm library itself. Codes
    * `0x4000` .. `0xFFFF` (`user_range_begin` and above) belong to the application
    * consuming ecomm  --  assign them however you like.
    *
    * @note Treat values as part of the wire protocol: once assigned, do not reuse or
    *       renumber across protocol versions.
    */
    enum class error_code : std::uint16_t {
        // 0x00xx  --  framing / packet structure
        ok                       = 0x0000, ///< Sentinel for "no error"; should rarely appear on the wire.
        malformed_header         = 0x0001, ///< Header bits could not be parsed.
        checksum_mismatch        = 0x0002, ///< Recomputed FCS did not match the packet's FCS.
        version_mismatch         = 0x0003, ///< Sender uses an incompatible protocol version.
        payload_too_small        = 0x0004, ///< Declared payload smaller than the envelope it claims to carry.
        malformed_error_envelope = 0x0005, ///< Returned via `as_error` when the envelope itself is corrupt.

        // 0x01xx  --  transport
        transport_timeout        = 0x0100, ///< Peer did not respond in the configured window.
        transport_disconnected   = 0x0101, ///< Underlying link reported a disconnect.

        // 0x02xx  --  dispatch / hub
        unknown_handler_id       = 0x0200, ///< Received handler id has no registered handler.
        handler_not_registered   = 0x0201, ///< Handler removed or never installed for this id.

        /// First value reserved for user-defined error codes. Anything < `user_range_begin`
        /// belongs to the ecomm library; anything >= `user_range_begin` is free for the
        /// consuming application.
        user_range_begin         = 0x4000,
    };

    /**
    * @brief `true` iff `c` is in the user-defined range (>= `error_code::user_range_begin`).
    *
    * Use this to decide whether an incoming error code came from the ecomm library or
    * from the application  --  useful for host-side routing.
    */
    [[nodiscard]] constexpr bool is_user_error_code(error_code c) noexcept {
        return static_cast<std::uint16_t>(c) >=
               static_cast<std::uint16_t>(error_code::user_range_begin);
    }

    /**
    * @brief Compile-time-selected unsigned integer type used for the length field
    *        of the error envelope.
    *
    * Width is the smallest unsigned integer that can hold
    * `ECOMM_MAX_ERROR_MESSAGE_LENGTH`:
    * - macro <= 255 -> `std::uint8_t`
    * - macro <= 65535 -> `std::uint16_t` (default)
    * - macro <= 4294967295 -> `std::uint32_t`
    */
    using error_message_length_t =
        etools::meta::smallest_uint_t<ECOMM_MAX_ERROR_MESSAGE_LENGTH>;

    /**
    * @struct error_envelope
    *
    * @brief Wire-format constants and writer for an error envelope sized to a specific
    *        packet payload.
    *
    * This is a static, stateless helper  --  it carries the compile-time arithmetic for a
    * given payload size and exposes a single `write` operation. It is not a value type
    * and is not instantiated as an object.
    *
    * @tparam PayloadSize Size of the packet's `payload` region in bytes (i.e.
    *         `Packet::payload_size`).
    */
    template<std::size_t PayloadSize>
    struct error_envelope {
        /// @brief Size of the payload region this envelope was sized against.
        static constexpr std::size_t payload_size = PayloadSize;

        /// @brief Number of bytes consumed by the fixed-size code + length prefix.
        static constexpr std::size_t prefix_size =
            sizeof(error_code) + sizeof(error_message_length_t);

        /// @brief Largest message length (in bytes) physically representable inside this
        ///        envelope for this payload size.
        static constexpr std::size_t max_message_length_in_payload =
            PayloadSize - prefix_size;

        // Layer-2 guard: the user-configurable macro must not promise more space than the
        // chosen packet can actually carry. This fires at template instantiation, naming
        // the offending payload size.
        static_assert(
            PayloadSize >= prefix_size,
            "error_envelope: PayloadSize is too small to even hold the error code and "
            "length prefix. Increase the packet size."
        );
        static_assert(
            ECOMM_MAX_ERROR_MESSAGE_LENGTH <= max_message_length_in_payload,
            "error_envelope: ECOMM_MAX_ERROR_MESSAGE_LENGTH exceeds the message capacity "
            "of this packet's payload. Either reduce ECOMM_MAX_ERROR_MESSAGE_LENGTH "
            "(this also shrinks the on-wire length field) or use a larger packet."
        );

        /**
        * @brief Write an error code with a human-readable message into the payload.
        *
        * Wire layout: `[error_code (2B)] [length (1 - 4B)] [message bytes]`.
        * The remainder of the payload beyond the written bytes is left untouched.
        *
        * For string literals the compiler deduces `std::string_view` at compile
        * time and the length is never a runtime parameter.
        *
        * @param[in] payload_begin  Pointer to the first byte of the packet's payload
        *                           (`&packet.payload[0]`). Must be non-null and point
        *                           to at least `payload_size` writable bytes.
        * @param[in] code           Protocol-level error identifier.
        * @param[in] message        View of the message bytes. The string is copied into
        *                           the payload; it need not be null-terminated. An empty
        *                           view is valid and produces a zero-length field on the wire.
        * @return Total bytes written into the payload (`prefix_size + message.size()`).
        *
        * @pre `message.size() <= ECOMM_MAX_ERROR_MESSAGE_LENGTH`
        * @pre `message.size() <= max_message_length_in_payload`
        */
        static inline std::size_t write(
            std::byte*       payload_begin,
            error_code       code,
            std::string_view message
        ) noexcept;

        /**
        * @brief Write an error code with no message into the payload.
        *
        * Convenience overload for cases where the error code alone is sufficient
        * and no human-readable explanation is needed. Writes a zero-length field
        * into the length slot so the wire format stays self-consistent and
        * `as_error` / `as_error_unchecked` can decode the envelope normally.
        *
        * Equivalent to `write(payload_begin, code, std::string_view{})`.
        *
        * @param[in] payload_begin  Pointer to the first byte of the packet's payload.
        *                           Must be non-null and point to at least `payload_size`
        *                           writable bytes.
        * @param[in] code           Protocol-level error identifier.
        * @return Total bytes written into the payload (`prefix_size`).
        */
        static inline std::size_t write(
            std::byte* payload_begin,
            error_code code
        ) noexcept;
    };

    /**
    * @struct error_view
    *
    * @brief Non-owning view of an error envelope decoded from a packet's payload.
    *
    * `message` points into the underlying packet's payload buffer and is valid for the
    * lifetime of that packet. The string is **not** null-terminated; use `length`.
    */
    struct error_view {
        error_code  code;     ///< Decoded protocol-level error identifier.
        const char* message;  ///< Pointer into the packet's payload; not owned, not null-terminated.
        std::size_t length;   ///< Number of valid bytes at `message`. May be zero.
    };

    /**
    * @brief Decode an error envelope from a packet whose header has `header_options::error` set.
    *
    * Returns `std::nullopt` if the envelope is malformed (declared length overruns the
    * available payload). A malformed envelope is a wire condition, not a programmer error,
    * so it is reported through the return value rather than via assert.
    *
    * @tparam Packet Any packet type that exposes a `std::byte payload[N]` member and a
    *                `static constexpr std::size_t payload_size` constant (i.e. `packet<>`).
    *
    * @param[in] packet The packet to interpret.
    * @return Decoded `error_view` on success, `std::nullopt` on a structurally invalid envelope.
    *
    * @pre `packet.header.has(header_options::error)`. Calling this on a packet that does not
    *      have the error flag set is a programmer error and is checked via `assert` in debug builds.
    *
    * @see as_error_unchecked  --  same decoder, no header precondition.
    */
    template<typename Packet>
    [[nodiscard]] inline std::optional<error_view> as_error(const Packet& packet) noexcept;

    /**
    * @brief Decode an error envelope from a packet **without** asserting the error flag.
    *
    * Same decoding logic as `as_error`, but with no precondition on the packet header.
    * Useful for:
    * - Unit tests that want to exercise the decoder without constructing a full header.
    * - Code paths that have already routed on the error flag through a different mechanism
    *   and would find the duplicate assert noisy.
    *
    * @tparam Packet Any packet type that exposes a `std::byte payload[N]` member and a
    *                `static constexpr std::size_t payload_size` constant.
    *
    * @param packet The packet whose payload should be interpreted as an error envelope.
    * @return Decoded `error_view` on success, `std::nullopt` on a structurally invalid envelope.
    */
    template<typename Packet>
    [[nodiscard]] inline std::optional<error_view> as_error_unchecked(const Packet& packet) noexcept;

} // namespace ecomm::protocol

#include "error.tpp"
#endif // ECOMM_PROTOCOL_ERROR_HPP_
