// SPDX-License-Identifier: BSL-1.1
/**
* @file header_layout.hpp
*
* @brief `header_layout` -- byte-exact wire storage for `packet_header`.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* Defines `details::header_layout<Topology, SequencePolicy, ChecksumPolicy>`,
* the single private base struct that carries every data member of
* `packet_header`.
*
* @par Why this exists -- the standard-layout requirement
* C++17 sec. 11.4 requires that all non-static data members (including those of
* base classes) reside in exactly one class in the hierarchy for a type to be
* standard-layout.  Only then are `offsetof` on its fields well-defined and
* raw byte-cast access to a wire buffer safe.
*
* `header_layout` is that single owner: it declares every field; `packet_header`
* inherits from it privately and adds no data members of its own.  Private
* inheritance hides `_byte` completely -- the caller can only reach it through
* `packet_header`'s typed accessors -- while the user-visible fields
* (`seq_num`, `fcs`, `sender_id`, `receiver_id`) are selectively re-exposed by
* `packet_header` with `using` declarations.
*
* Private inheritance still satisfies C++17 standard-layout (the access
* specifier on the base class does not affect the standard-layout check).
*
* @par Three parameters, eight wire layouts
* `SequencePolicy` controls the presence of `seq_num` (see `sequence.hpp`).
* `ChecksumPolicy` controls the presence of `fcs` (see `checksum.hpp`).
* Neither needs a separate boolean -- specialising on the tag type directly
* is unambiguous and eliminates a redundant invariant.
*
* Field declaration order (wire order):
*   `_byte`      -- always present (the packed protocol byte)
*   `seq_num`    -- only when SequencePolicy == sequenced (1 byte)
*   `sender_id`  -- only when Topology == network (1 byte)
*   `receiver_id`-- only when Topology == network (1 byte)
*   `fcs`        -- only when ChecksumPolicy != none (ChecksumPolicy::size bytes)
*
* | Topology | SequencePolicy | ChecksumPolicy | Wire layout                                    |
* |----------|----------------|----------------|------------------------------------------------|
* | p2p      | no_sequence    | none           | _byte                                          |
* | p2p      | no_sequence    | Policy         | _byte, fcs                                     |
* | p2p      | sequenced      | none           | _byte, seq_num                                 |
* | p2p      | sequenced      | Policy         | _byte, seq_num, fcs                            |
* | network  | no_sequence    | none           | _byte, sender_id, receiver_id                  |
* | network  | no_sequence    | Policy         | _byte, sender_id, receiver_id, fcs             |
* | network  | sequenced      | none           | _byte, seq_num, sender_id, receiver_id         |
* | network  | sequenced      | Policy         | _byte, seq_num, sender_id, receiver_id, fcs    |
*
* @par `#pragma pack(push, 1)`
* Applied around all eight specialisations.  No compiler-inserted padding
* appears between fields, giving a byte-exact wire layout on every target.
* Closed (`pop`) before the closing `#endif`.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-27
*
* @copyright
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2026 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-27 Initial creation; replaces packet_layout.hpp.
*              Dropped redundant HasFcs boolean -- specialise on ChecksumPolicy.
*              Replaced bool HasIds with topology.
*              Added SequencePolicy parameter; eight specialisations total.
*              Constructor bodies in header_layout.tpp per .hpp/.tpp rule.
*/
#ifndef ECOMM_PROTOCOL_HEADER_LAYOUT_HPP_
#define ECOMM_PROTOCOL_HEADER_LAYOUT_HPP_

#include <cstdint>

#include "checksum.hpp"
#include "node_ids.hpp"
#include "sequence.hpp"
#include "topology.hpp"

namespace ecomm::protocol::details {

    /**
    * @struct header_layout
    *
    * @brief Primary template -- intentionally left undefined.
    *
    * Only the eight explicit partial specialisations below are valid.
    * Any other combination of template arguments produces a compile error
    * rather than silently generating a wrong layout.
    *
    * @tparam Topology        Wire shape. `point_to_point` omits node id fields;
    *                          `network` adds `sender_id` and `receiver_id`.
    * @tparam SequencePolicy  Tag from `sequence.hpp`. `no_sequence` adds no field;
    *                          `sequenced` adds a one-byte `seq_num` field immediately
    *                          after `_byte`.
    * @tparam ChecksumPolicy  Tag from `checksum.hpp`. `none` adds no field; any
    *                          other policy adds a `fcs` field of type
    *                          `ChecksumPolicy::value_type` at the end of the struct.
    */
    template<topology Topology, typename SequencePolicy, typename ChecksumPolicy>
    struct header_layout;

    #pragma pack(push, 1)

    // =========================================================================
    // Specialisation 1 -- point-to-point, no sequence, no checksum
    // Wire layout: [ _byte ]
    // =========================================================================

    /**
    * @brief Minimal header: one protocol byte, no sequence number, no FCS.
    */
    template<>
    struct header_layout<topology::point_to_point, no_sequence, none> {

        /**
        * @brief Packed protocol byte.
        *
        * Bit layout: bits 7..5 = header_type (3 bits),
        *             bits 4..2 = header_options (3 bits: error, ack, encrypted),
        *             bits 1..0 = protocol version (2 bits).
        *
        * Not part of the public API of `packet_header`. Access it through
        * the typed accessors `type()`, `options()`, `version()`, and `raw()`.
        */
        std::uint8_t _byte{};

    protected:
        /// Initialise `_byte` directly; used by `packet_header`'s constructor.
        constexpr explicit header_layout(std::uint8_t b) noexcept;
        constexpr header_layout() noexcept = default;
    };

    // =========================================================================
    // Specialisation 2 -- point-to-point, no sequence, with checksum
    // Wire layout: [ _byte | fcs ]
    // =========================================================================

    /**
    * @brief Point-to-point header with FCS and no sequence number.
    *
    * `fcs` is the last field.  It is zeroed on construction, written by
    * `validator::seal`, and checked by `validator::is_valid`.
    */
    template<typename ChecksumPolicy>
    struct header_layout<topology::point_to_point, no_sequence, ChecksumPolicy> {

        /**
        * @brief Packed protocol byte.
        *
        * Bit layout: bits 7..5 = header_type (3 bits),
        *             bits 4..2 = header_options (3 bits: error, ack, encrypted),
        *             bits 1..0 = protocol version (2 bits).
        *
        * Not part of the public API of `packet_header`. Access it through
        * the typed accessors `type()`, `options()`, `version()`, and `raw()`.
        */
        std::uint8_t _byte{};

        /**
        * @brief Frame check sequence -- zero until `validator::seal` is called.
        *
        * Width is `ChecksumPolicy::size` bytes.  Placed last so the checksum
        * covers every preceding field and any payload that follows the header.
        */
        typename ChecksumPolicy::value_type fcs{};

    protected:
        /// Initialise `_byte` directly; used by `packet_header`'s constructor.
        constexpr explicit header_layout(std::uint8_t b) noexcept;
        constexpr header_layout() noexcept = default;
    };

    // =========================================================================
    // Specialisation 3 -- point-to-point, sequenced, no checksum
    // Wire layout: [ _byte | seq_num ]
    // =========================================================================

    /**
    * @brief Point-to-point header with a sequence number and no FCS.
    *
    * `seq_num` sits immediately after `_byte`.  It is managed by
    * `reliable_channel`; application code should treat it as opaque.
    */
    template<>
    struct header_layout<topology::point_to_point, sequenced, none> {

        /**
        * @brief Packed protocol byte.
        *
        * Bit layout: bits 7..5 = header_type (3 bits),
        *             bits 4..2 = header_options (3 bits: error, ack, encrypted),
        *             bits 1..0 = protocol version (2 bits).
        *
        * Not part of the public API of `packet_header`. Access it through
        * the typed accessors `type()`, `options()`, `version()`, and `raw()`.
        */
        std::uint8_t _byte{};

        /**
        * @brief Per-direction sequence counter, wrapping at 255.
        *
        * Incremented by `reliable_channel` on each outbound packet.
        * Echoed verbatim in the corresponding acknowledgement so the sender
        * can match the ack to its outstanding transmission.
        */
        std::uint8_t seq_num{};

    protected:
        /// Initialise `_byte` directly; used by `packet_header`'s constructor.
        constexpr explicit header_layout(std::uint8_t b) noexcept;
        constexpr header_layout() noexcept = default;
    };

    // =========================================================================
    // Specialisation 4 -- point-to-point, sequenced, with checksum
    // Wire layout: [ _byte | seq_num | fcs ]
    // =========================================================================

    /**
    * @brief Point-to-point header with both a sequence number and FCS.
    *
    * `seq_num` immediately follows `_byte`; `fcs` is the last field and
    * covers `_byte`, `seq_num`, and any payload that follows.
    */
    template<typename ChecksumPolicy>
    struct header_layout<topology::point_to_point, sequenced, ChecksumPolicy> {

        /**
        * @brief Packed protocol byte.
        *
        * Bit layout: bits 7..5 = header_type (3 bits),
        *             bits 4..2 = header_options (3 bits: error, ack, encrypted),
        *             bits 1..0 = protocol version (2 bits).
        *
        * Not part of the public API of `packet_header`. Access it through
        * the typed accessors `type()`, `options()`, `version()`, and `raw()`.
        */
        std::uint8_t _byte{};

        /**
        * @brief Per-direction sequence counter, wrapping at 255.
        *
        * Incremented by `reliable_channel` on each outbound packet.
        * Echoed verbatim in the corresponding acknowledgement so the sender
        * can match the ack to its outstanding transmission.
        */
        std::uint8_t seq_num{};

        /**
        * @brief Frame check sequence -- zero until `validator::seal` is called.
        *
        * Width is `ChecksumPolicy::size` bytes.  Placed last so the checksum
        * covers every preceding field and any payload that follows the header.
        */
        typename ChecksumPolicy::value_type fcs{};

    protected:
        /// Initialise `_byte` directly; used by `packet_header`'s constructor.
        constexpr explicit header_layout(std::uint8_t b) noexcept;
        constexpr header_layout() noexcept = default;
    };

    // =========================================================================
    // Specialisation 5 -- network, no sequence, no checksum
    // Wire layout: [ _byte | sender_id | receiver_id ]
    // =========================================================================

    /**
    * @brief Network header with node ids, no sequence number, no FCS.
    *
    * `sender_id` defaults to `ECOMM_BOARD_ID`; `receiver_id` defaults to 0.
    */
    template<>
    struct header_layout<topology::network, no_sequence, none> {

        /**
        * @brief Packed protocol byte.
        *
        * Bit layout: bits 7..5 = header_type (3 bits),
        *             bits 4..2 = header_options (3 bits: error, ack, encrypted),
        *             bits 1..0 = protocol version (2 bits).
        *
        * Not part of the public API of `packet_header`. Access it through
        * the typed accessors `type()`, `options()`, `version()`, and `raw()`.
        */
        std::uint8_t _byte{};

        /// Identifier of the node that originated this packet. Defaults to `ECOMM_BOARD_ID`.
        std::uint8_t sender_id{ECOMM_BOARD_ID};

        /// Identifier of the intended recipient. Caller must set before transmission.
        std::uint8_t receiver_id{};

    protected:
        /// Initialise `_byte` directly; used by `packet_header`'s constructor.
        constexpr explicit header_layout(std::uint8_t b) noexcept;
        constexpr header_layout() noexcept = default;
    };

    // =========================================================================
    // Specialisation 6 -- network, no sequence, with checksum
    // Wire layout: [ _byte | sender_id | receiver_id | fcs ]
    // =========================================================================

    /**
    * @brief Network header with node ids and FCS, no sequence number.
    *
    * `fcs` is the last field and covers all preceding fields.
    */
    template<typename ChecksumPolicy>
    struct header_layout<topology::network, no_sequence, ChecksumPolicy> {

        /**
        * @brief Packed protocol byte.
        *
        * Bit layout: bits 7..5 = header_type (3 bits),
        *             bits 4..2 = header_options (3 bits: error, ack, encrypted),
        *             bits 1..0 = protocol version (2 bits).
        *
        * Not part of the public API of `packet_header`. Access it through
        * the typed accessors `type()`, `options()`, `version()`, and `raw()`.
        */
        std::uint8_t _byte{};

        /// Identifier of the node that originated this packet. Defaults to `ECOMM_BOARD_ID`.
        std::uint8_t sender_id{ECOMM_BOARD_ID};

        /// Identifier of the intended recipient. Caller must set before transmission.
        std::uint8_t receiver_id{};

        /**
        * @brief Frame check sequence -- zero until `validator::seal` is called.
        *
        * Width is `ChecksumPolicy::size` bytes.  Placed last so the checksum
        * covers `_byte`, `sender_id`, `receiver_id`, and any payload.
        */
        typename ChecksumPolicy::value_type fcs{};

    protected:
        /// Initialise `_byte` directly; used by `packet_header`'s constructor.
        constexpr explicit header_layout(std::uint8_t b) noexcept;
        constexpr header_layout() noexcept = default;
    };

    // =========================================================================
    // Specialisation 7 -- network, sequenced, no checksum
    // Wire layout: [ _byte | seq_num | sender_id | receiver_id ]
    // =========================================================================

    /**
    * @brief Network header with sequence number and node ids, no FCS.
    *
    * `seq_num` immediately follows `_byte`; node ids follow `seq_num`.
    */
    template<>
    struct header_layout<topology::network, sequenced, none> {

        /**
        * @brief Packed protocol byte.
        *
        * Bit layout: bits 7..5 = header_type (3 bits),
        *             bits 4..2 = header_options (3 bits: error, ack, encrypted),
        *             bits 1..0 = protocol version (2 bits).
        *
        * Not part of the public API of `packet_header`. Access it through
        * the typed accessors `type()`, `options()`, `version()`, and `raw()`.
        */
        std::uint8_t _byte{};

        /**
        * @brief Per-direction sequence counter, wrapping at 255.
        *
        * Incremented by `reliable_channel` on each outbound packet.
        * Echoed verbatim in the corresponding acknowledgement so the sender
        * can match the ack to its outstanding transmission.
        */
        std::uint8_t seq_num{};

        /// Identifier of the node that originated this packet. Defaults to `ECOMM_BOARD_ID`.
        std::uint8_t sender_id{ECOMM_BOARD_ID};

        /// Identifier of the intended recipient. Caller must set before transmission.
        std::uint8_t receiver_id{};

    protected:
        /// Initialise `_byte` directly; used by `packet_header`'s constructor.
        constexpr explicit header_layout(std::uint8_t b) noexcept;
        constexpr header_layout() noexcept = default;
    };

    // =========================================================================
    // Specialisation 8 -- network, sequenced, with checksum
    // Wire layout: [ _byte | seq_num | sender_id | receiver_id | fcs ]
    // =========================================================================

    /**
    * @brief Network header with sequence number, node ids, and FCS.
    *
    * Full header: `seq_num` follows `_byte`, then node ids, then `fcs` last.
    */
    template<typename ChecksumPolicy>
    struct header_layout<topology::network, sequenced, ChecksumPolicy> {

        /**
        * @brief Packed protocol byte.
        *
        * Bit layout: bits 7..5 = header_type (3 bits),
        *             bits 4..2 = header_options (3 bits: error, ack, encrypted),
        *             bits 1..0 = protocol version (2 bits).
        *
        * Not part of the public API of `packet_header`. Access it through
        * the typed accessors `type()`, `options()`, `version()`, and `raw()`.
        */
        std::uint8_t _byte{};

        /**
        * @brief Per-direction sequence counter, wrapping at 255.
        *
        * Incremented by `reliable_channel` on each outbound packet.
        * Echoed verbatim in the corresponding acknowledgement so the sender
        * can match the ack to its outstanding transmission.
        */
        std::uint8_t seq_num{};

        /// Identifier of the node that originated this packet. Defaults to `ECOMM_BOARD_ID`.
        std::uint8_t sender_id{ECOMM_BOARD_ID};

        /// Identifier of the intended recipient. Caller must set before transmission.
        std::uint8_t receiver_id{};

        /**
        * @brief Frame check sequence -- zero until `validator::seal` is called.
        *
        * Width is `ChecksumPolicy::size` bytes.  Placed last so the checksum
        * covers every preceding field and any payload that follows the header.
        */
        typename ChecksumPolicy::value_type fcs{};

    protected:
        /// Initialise `_byte` directly; used by `packet_header`'s constructor.
        constexpr explicit header_layout(std::uint8_t b) noexcept;
        constexpr header_layout() noexcept = default;
    };

    #pragma pack(pop)

} // namespace ecomm::protocol::details

#include "header_layout.tpp"
#endif // ECOMM_PROTOCOL_HEADER_LAYOUT_HPP_
