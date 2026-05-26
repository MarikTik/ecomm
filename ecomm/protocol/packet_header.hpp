// SPDX-License-Identifier: BSL-1.1
/**
* @file packet_header.hpp
*
* @brief Protocol packet header for the ecomm communication library.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* The header is a data-only POD that owns every protocol-level field for a packet:
* type, options (error, heartbeat, encrypted), an internal version field, optional
* sender/receiver identifiers (when topology is `network`), and the FCS bytes (when a
* checksum policy other than `checksum::none` is used).
*
* Two compile-time policies parameterize the layout:
* - `Topology` selects whether the header carries `sender_id`/`receiver_id` (network)
*   or omits them entirely (point-to-point), saving two bytes per packet.
* - `ChecksumPolicy` selects the FCS width. Using `none` produces a header with no FCS
*   bytes. Using `crc32` etc. produces a header that owns the FCS. There is no longer a
*   separate "basic" vs "framed" packet type.
*
* Wire layout (logical, conditional fields are omitted when their gate is false):
* ```
* +-----------------+----------------+------------------+-------------------------------+
* | proto byte (1B) | sender_id (*)  | receiver_id (*)  | fcs (ChecksumPolicy::size) ($)|
* +-----------------+----------------+------------------+-------------------------------+
* (*) only present when Topology == network
* ($) only present when ChecksumPolicy != ecomm::protocol::none
* ```
*
* **Protocol byte layout** (manual shift/mask in accessors so the wire bit order is the
* code, not implementation-defined):
* ```
*  7..5 : type      (3 bits)   — header_type enum, 6 values used (2 reserved)
*     4 : error     (1 bit)    — header_options::error
*     3 : heartbeat (1 bit)    — header_options::heartbeat
*     2 : encrypted (1 bit)    — header_options::encrypted
*  1..0 : version   (2 bits)   — internal, locked to ECOMM_PROTOCOL_VERSION
* ```
*
* @note Version is *not* a constructor parameter or part of `header_options`. It is
*       reserved internal bits set by every constructor to `ECOMM_PROTOCOL_VERSION`.
*       The `version()` accessor exists for inspection only.
*
* @note Priority was deliberately removed: fixed-length packets with no send-side
*       buffering give nowhere for priority to express itself.
*
* @note `validated` is no longer a runtime bit: whether a packet carries an FCS is
*       fully determined by `ChecksumPolicy` (a type-system property).
*
* @note `task_id`, `status_code`, fragmentation, and ordering bits are intentionally
*       absent. Anything application-flavored lives in the packet payload, not the header.
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
* - 2025-07-03 Initial creation (legacy layout, since rewritten).
* - 2026-05-26
*      - Major rewrite. Now templated on `<Topology, ChecksumPolicy>`.
*      - `sender_id` / `receiver_id` present only when Topology == network.
*      - FCS bytes moved into the header (was previously the tail of `framed_packet`).
*      - Removed: `task_id`, `status_code`, `validated` bit, `priority`, fragmentation bit.
*      - Trimmed `header_type` to 6 values (3 bits): data, control, auth, session, log, firmware.
*      - Replaced boolean flag parameters with a single `header_options` bit-flag enum,
*        opted-in to bitwise operators via `etools::meta::enable_flags`.
*      - Constructor signature collapsed to `packet_header(header_type, header_options)`.
*      - Whole protocol field set fits in exactly 1 byte (8 bits used, 0 wasted).
*      - Fixed `EBOARD_ID` typo (now correctly uses `ECOMM_BOARD_ID`).
*/
#ifndef ECOMM_PROTOCOL_PACKET_HEADER_HPP_
#define ECOMM_PROTOCOL_PACKET_HEADER_HPP_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <etools/meta/flags.hpp>

#include "checksum.hpp"
#include "config.hpp"
#include "topology.hpp"

namespace ecomm::protocol {

    /**
    * @enum header_type
    *
    * @brief Top-level classification of what a packet carries.
    *
    * Stored in bits 7..5 of the protocol byte (3 bits). Six values are defined;
    * encodings `0x6` and `0x7` are reserved for future packet kinds and must not
    * appear on the wire until assigned.
    *
    * @note These values are wire-stable. Never renumber an existing enumerator —
    *       doing so would silently break backward compatibility with any peer
    *       running an older firmware.
    */
    enum class header_type : std::uint8_t {
        data     = 0x0, ///< Generic application data. Most packets use this type.
        control  = 0x1, ///< Protocol-level commands (reset, sync, configuration).
        auth     = 0x2, ///< Authentication or credential exchange.
        session  = 0x3, ///< Session lifecycle: initiation, teardown, handshake.
        log      = 0x4, ///< Diagnostic log messages or telemetry.
        firmware = 0x5, ///< Firmware image chunks or update-related payloads.
    };

    /**
    * @enum header_options
    *
    * @brief Independent single-bit flags that modify how a packet is interpreted.
    *        Flags may be combined with `|` (bitwise OR).
    *
    * Each enumerator's numeric value is already shifted to its final bit position
    * within the protocol byte, so the `packet_header` constructor can OR the
    * parameter directly into storage without additional shifting.
    *
    * Opted-in to bitwise operators (`|`, `&`, `^`, `~`) via `etools::meta::enable_flags`
    * (specialization appears immediately after this namespace block).
    *
    * @note These values are wire-stable. Never renumber or move an existing enumerator —
    *       doing so would silently break backward compatibility with any peer running an
    *       older firmware.
    *
    * @see header_options_mask for the bitmask that covers all legitimate option bits.
    * @see error.hpp for the error-envelope layout that `error` implies.
    */
    enum class header_options : std::uint8_t {
        none      = 0,        ///< No flags set. Use as a neutral argument to the constructor.
        error     = 1u << 4,  ///< Payload must be interpreted as an error envelope (see error.hpp).
        heartbeat = 1u << 3,  ///< Keepalive signal. Payload content is unspecified by the protocol.
        encrypted = 1u << 2,  ///< Payload bytes are encrypted. Decryption is the caller's responsibility.
    };

    /**
    * @brief Bitmask covering every bit position that `header_options` may legitimately occupy.
    *
    * Spans bits 4..2 of the protocol byte: `0b0001'1100`.
    *
    * The `packet_header` constructor masks the caller-supplied `opts` value with this constant
    * before ORing it into `_byte`, ensuring that any spurious bits the caller set (e.g. via an
    * unsafe cast) are silently discarded rather than corrupting the type or version fields.
    *
    * Can also serve as a "select all options" argument: `header & header_options_mask` extracts
    * every option bit simultaneously.
    */
    inline constexpr std::uint8_t header_options_mask = 0b0001'1100;

} // namespace ecomm::protocol

// Opt header_options in to the etools bitwise operators. Must be visible wherever
// the operators are used; placed in the same header as the enum per etools' guidance.
template<>
struct etools::meta::enable_flags<ecomm::protocol::header_options> : std::true_type {};

namespace ecomm::protocol {

    // The etools flag operators live in `etools::meta`. Pull them into the protocol
    // namespace so ordinary lookup at call sites resolves `a | b` for header_options
    // without forcing users to `using namespace etools::meta;`.
    using namespace etools::meta;

    namespace details {

        /**
        * @brief Empty base class used to represent an absent optional field.
        *
        * Inherited instead of a real field type when a template condition disables
        * a feature (e.g. no network ids in point-to-point mode). Because
        * `empty_field` has no non-static data members, the Empty Base Optimization
        * (EBO) collapses it to zero bytes in the derived class layout.
        */
        struct empty_field {};

        /**
        * @brief Storage for the sender and receiver node identifiers.
        *
        * Present in `packet_header` only when `Topology == topology::network`.
        * Both fields are one byte wide, matching the `ECOMM_BOARD_ID` granularity.
        *
        * @note `sender_id` is default-initialised to `ECOMM_BOARD_ID` (the local
        *       board identifier defined in `config.hpp`). `receiver_id` is
        *       zero-initialized and must be set explicitly before transmission.
        */
        struct network_ids {
            std::uint8_t sender_id{ECOMM_BOARD_ID}; ///< Identity of the sending node.
            std::uint8_t receiver_id{};              ///< Identity of the intended recipient.
        };

        /**
        * @brief Select the id-storage type based on the topology policy.
        *
        * Resolves to `network_ids` (2 bytes on the wire) when `Topology` is
        * `topology::network`, and to `empty_field` (0 bytes) otherwise.
        *
        * @tparam Topology The topology policy tag; drives conditional field inclusion.
        */
        template<topology Topology>
        using ids_storage_t =
            std::conditional_t<Topology == topology::network, network_ids, empty_field>;

        /**
        * @brief Storage for the Frame Check Sequence (FCS) produced by `ChecksumPolicy`.
        *
        * The `fcs` field's underlying type is `ChecksumPolicy::value_type`, so its
        * width matches the checksum algorithm (e.g. `std::uint32_t` for CRC-32).
        *
        * @tparam ChecksumPolicy A checksum tag type from `checksum.hpp`. Must expose
        *         a nested `value_type` typedef and a `size` constant.
        *
        * @warning The `fcs` field is zero-initialized on construction. Callers that
        *          produce a checksum must write it into this field explicitly after
        *          serializing the payload.
        */
        template<typename ChecksumPolicy>
        struct fcs_storage {
            typename ChecksumPolicy::value_type fcs{}; ///< Computed checksum value (zero until filled).
        };

        /**
        * @brief Specialization for `none` — produces no FCS storage whatsoever.
        *
        * When `ChecksumPolicy` is `ecomm::protocol::none`, this empty specialization
        * is selected. EBO collapses it to zero bytes in the derived class layout,
        * so there is no wire overhead and no runtime cost.
        */
        template<>
        struct fcs_storage<none> {};

    } // namespace details

    #pragma pack(push, 1)
    /**
    * @class packet_header
    *
    * @brief Compact protocol header. SRP-by-construction: data class only.
    *        Validation lives in `validator.hpp`.
    *
    * @tparam Topology       Wire shape. `point_to_point` omits sender/receiver ids;
    *                        `network` includes them. Defaults to `default_topology`
    *                        (driven by `ECOMM_DEFAULT_TOPOLOGY` in `config.hpp`).
    * @tparam ChecksumPolicy Checksum algorithm tag from `checksum.hpp`. `none` removes
    *                        the FCS bytes entirely. Defaults to `none`.
    * Wire layout (logical, conditional fields are omitted when their gate is false):
    * ```
    * +-----------------+----------------+------------------+-------------------------------+
    * | proto byte (1B) | sender_id (*)  | receiver_id (*)  | fcs (ChecksumPolicy::size) ($)|
    * +-----------------+----------------+------------------+-------------------------------+
    * (*) only present when Topology == network
    * ($) only present when ChecksumPolicy != ecomm::protocol::none
    * ```
    */
    template<
        topology Topology       = default_topology,
        typename ChecksumPolicy = none
    >
    class packet_header
        : public details::ids_storage_t<Topology>
        , public details::fcs_storage<ChecksumPolicy>
    {
    public:

        /**
        * @brief Number of bytes the FCS field occupies in the wire layout.
        *
        * Equals `ChecksumPolicy::size`. Zero when `ChecksumPolicy` is `none`,
        * meaning the header carries no FCS bytes at all.
        */
        static constexpr std::size_t fcs_size = ChecksumPolicy::size;

        /**
        * @brief Whether this header instantiation carries sender and receiver id bytes.
        *
        * `true` when `Topology == topology::network` (the inherited `network_ids` base
        * contributes two bytes to the wire layout). `false` for point-to-point topology
        * (the `empty_field` base contributes zero bytes).
        */
        static constexpr bool has_network_ids = (Topology == topology::network);

        /**
        * @brief Default constructor. Leaves every field at its zero-initialized value.
        *
        * The protocol byte is `0x00`: type = `data` (0), no options, version = 0.
        *
        * @note Version bits are **not** set to `ECOMM_PROTOCOL_VERSION` by this
        *       constructor. Use the two-parameter constructor to produce a properly
        *       versioned header ready for the wire.
        *
        * @post `raw() == 0x00`. `type() == header_type::data`. `options() == header_options::none`.
        *       `version() == 0`. If `has_network_ids`, `sender_id` and `receiver_id` are both 0.
        *       If `fcs_size > 0`, `fcs == 0`.
        */
        constexpr packet_header() noexcept = default;

        /**
        * @brief Construct a header with a given packet type and option flags.
        *
        * Packs `type` into bits 7..5, `opts` (masked to bits 4..2) into bits 4..2,
        * and `ECOMM_PROTOCOL_VERSION` into bits 1..0 of the single protocol byte.
        *
        * Sender/receiver ids (when `has_network_ids`) and the FCS field (when
        * `fcs_size > 0`) are left at their zero-initialized defaults; callers that
        * need to populate them write through the inherited base class members directly.
        *
        * @param[in] type  Top-level packet classification. Must be one of the six
        *                  defined `header_type` enumerators; encodings 0x6 and 0x7
        *                  are reserved and produce undefined protocol behavior.
        * @param[in] opts  OR-combination of `header_options` flags. Pass
        *                  `header_options::none` when no flags are needed. Any bits
        *                  outside `header_options_mask` are silently stripped before
        *                  being stored, so out-of-range casts do not corrupt the type
        *                  or version fields.
        *
        * @pre  `type` must be a valid enumerator (0x0 – 0x5). Passing a reserved
        *       encoding produces a well-formed byte but an unspecified packet type on
        *       the receiving end.
        *
        * @post `this->type() == type`. `(options() & opts) == opts`. `version() == ECOMM_PROTOCOL_VERSION`.
        */
        constexpr packet_header(header_type type, header_options opts) noexcept;

        /**
        * @brief Extract the packet type from the protocol byte.
        *
        * Reads bits 7..5 of the stored protocol byte and casts the result to
        * `header_type`. No range check is performed — if the peer sent a reserved
        * encoding (0x6 or 0x7), the returned value will not match any named
        * enumerator. Use the validator to reject such packets before inspecting
        * the type.
        *
        * @return The `header_type` encoded in bits 7..5. One of the six defined
        *         enumerators for well-formed packets; an unspecified value for
        *         reserved encodings.
        */
        [[nodiscard]] constexpr header_type type() const noexcept;

        /**
        * @brief Extract the complete option-flag mask from the protocol byte.
        *
        * Returns a `header_options` value whose bits correspond exactly to bits 4..2
        * of the stored protocol byte. The result is suitable for `&` membership tests
        * or for passing to `has()`.
        *
        * @return The combined `header_options` flags currently set. May be
        *         `header_options::none` (zero) when no options are active.
        *
        * @note Prefer `has(opt)` for single-flag tests — it is marginally clearer
        *       at call sites. Use `options()` when you need the full mask (e.g. to
        *       compare or forward the entire flag field).
        */
        [[nodiscard]] constexpr header_options options() const noexcept;

        /**
        * @brief Test whether all bits in `opt` are set in this header's option field.
        *
        * Performs a subset test: returns `true` iff every bit that is set in `opt`
        * is also set in `options()`. When `opt` combines multiple flags (e.g.
        * `header_options::error | header_options::encrypted`), *all* of them must
        * be present for this function to return `true`.
        *
        * @param[in] opt  The flag or flag-combination to test. Must not carry bits
        *                 outside `header_options_mask`; any such bits will always
        *                 cause the test to return `false` (they are never stored).
        *
        * @return `true` if every bit in `opt` is set in `options()`; `false` otherwise.
        *
        * @note To test a single flag, prefer the named enumerator directly:
        *       `hdr.has(header_options::error)` rather than comparing `options()`.
        */
        [[nodiscard]] constexpr bool has(header_options opt) const noexcept;

        /**
        * @brief Extract the protocol version from the protocol byte.
        *
        * Returns the raw two-bit value stored in bits 1..0. For a header
        * constructed via the two-parameter constructor this will equal
        * `ECOMM_PROTOCOL_VERSION`. For a default-constructed header it is 0.
        *
        * @return Protocol version in the range [0, 3]. The library currently
        *         defines version 0; other values indicate a future or mismatched
        *         peer and should be rejected by the validator.
        *
        * @note This accessor exists for inspection and debugging. The version
        *       field is set automatically by the constructor and is not a
        *       constructor parameter.
        */
        [[nodiscard]] constexpr std::uint8_t version() const noexcept;

        /**
        * @brief Return the raw protocol byte without any decoding.
        *
        * Exposes the underlying eight-bit storage directly. Intended for
        * serialization (writing the byte onto the wire) and low-level
        * debugging. Prefer the typed accessors (`type()`, `options()`,
        * `version()`) for all protocol-logic use.
        *
        * @return The eight-bit protocol byte: bits 7..5 = type, 4..2 = options,
        *         1..0 = version.
        */
        [[nodiscard]] constexpr std::uint8_t raw() const noexcept;

    private:
        std::uint8_t _byte{}; ///< Packed protocol byte: [type:3][error:1][heartbeat:1][encrypted:1][version:2].
    };
    #pragma pack(pop)

} // namespace ecomm::protocol

#include "packet_header.tpp"
#endif // ECOMM_PROTOCOL_PACKET_HEADER_HPP_
