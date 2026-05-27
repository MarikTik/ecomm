// SPDX-License-Identifier: BSL-1.1
/**
* @file packet_layout.hpp
*
* @brief `packet_layout` — data-driven wire layout for `packet_header`.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* Defines `details::packet_layout<HasIds, HasFcs, ChecksumPolicy>`, the single
* base struct from which `packet_header` inherits all its data members.
*
* @par Why this exists — the standard-layout requirement
* `packet_header` previously inherited from two separate bases (`ids_storage_t`
* and `fcs_storage`) and declared `_byte` as its own member. Under C++17 a class
* is standard-layout only if all non-static data members (including those of base
* classes) belong to the *same* class in the hierarchy. With data split across a
* base and the derived class that rule is violated, making `offsetof` on any member
* undefined behaviour and breaking any assumption about the wire byte order.
*
* `packet_layout` gathers every data member into one struct. `packet_header`
* inherits from it and declares **no data members of its own**, so the layout
* is exactly as if all fields were declared directly in `packet_header` in the
* order they appear in `packet_layout`.
*
* @par Four specialisations — four wire layouts
* The two boolean template parameters select one of four concrete structs:
*
* | HasIds | HasFcs | Wire layout (in declaration order) |
* |--------|--------|-------------------------------------|
* | false  | false  | `_byte` |
* | false  | true   | `_byte`, `fcs` |
* | true   | false  | `_byte`, `sender_id`, `receiver_id` |
* | true   | true   | `_byte`, `fcs`, `sender_id`, `receiver_id` |
*
* The FCS is placed immediately after `_byte` so that `validator` can zero it,
* compute the checksum over the whole header+payload block, and restore it — all
* with a fixed offset regardless of whether node ids are present.
*
* @par `#pragma pack(push, 1)`
* The pragma is applied around every specialisation so that there is no
* compiler-inserted padding between fields, giving a byte-exact wire layout
* on any target. The pragma is closed (`pop`) at the end of the file.
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
* - 2026-05-27 Initial creation; replaces the ids_storage_t + fcs_storage
*              dual-inheritance approach to achieve standard-layout.
*/
#ifndef ECOMM_PROTOCOL_PACKET_LAYOUT_HPP_
#define ECOMM_PROTOCOL_PACKET_LAYOUT_HPP_

#include <cstdint>

#include "checksum.hpp"
#include "node_ids.hpp"

namespace ecomm::protocol::details {

    /**
    * @struct packet_layout
    *
    * @brief Primary template — intentionally left undefined.
    *
    * Only the four explicit partial specialisations below are valid.
    * Instantiating any other combination of `<HasIds, HasFcs, ChecksumPolicy>`
    * produces a linker error rather than silently generating a wrong layout.
    *
    * @tparam HasIds          `true` when `Topology == topology::network`; adds
    *                          `sender_id` and `receiver_id` to the wire layout.
    * @tparam HasFcs          `true` when `ChecksumPolicy != none`; adds the `fcs`
    *                          field immediately after `_byte`.
    * @tparam ChecksumPolicy  Checksum tag from `checksum.hpp`. Provides the
    *                          `value_type` used for the `fcs` field when `HasFcs`
    *                          is `true`. Ignored (but still required as a parameter
    *                          for uniformity) when `HasFcs` is `false`.
    */
    template<bool HasIds, bool HasFcs, typename ChecksumPolicy>
    struct packet_layout;

    #pragma pack(push, 1)

    // -------------------------------------------------------------------------
    // Specialisation 1: neither node ids nor FCS
    // Wire: [ _byte ]
    // -------------------------------------------------------------------------

    /**
    * @brief Layout for point-to-point topology with no checksum policy.
    *
    * The minimal header — a single protocol byte.
    */
    template<typename ChecksumPolicy>
    struct packet_layout<false, false, ChecksumPolicy> {
        std::uint8_t _byte{}; ///< Packed protocol byte: [type:3][opts:3][version:2].

    protected:
        /// Allow `packet_header` to initialise `_byte` via its own constructor.
        constexpr explicit packet_layout(std::uint8_t b) noexcept : _byte{b} {}
        constexpr packet_layout() noexcept = default;
    };

    // -------------------------------------------------------------------------
    // Specialisation 2: FCS only (no node ids)
    // Wire: [ _byte | fcs ]
    // -------------------------------------------------------------------------

    /**
    * @brief Layout for point-to-point topology with a checksum policy.
    *
    * FCS is placed directly after `_byte` so its offset is fixed and
    * independent of whether node ids are present.
    */
    template<typename ChecksumPolicy>
    struct packet_layout<false, true, ChecksumPolicy> {
        std::uint8_t                         _byte{}; ///< Packed protocol byte.
        typename ChecksumPolicy::value_type  fcs{};   ///< Frame check sequence (zero until sealed).

    protected:
        constexpr explicit packet_layout(std::uint8_t b) noexcept : _byte{b} {}
        constexpr packet_layout() noexcept = default;
    };

    // -------------------------------------------------------------------------
    // Specialisation 3: node ids only (no FCS)
    // Wire: [ _byte | sender_id | receiver_id ]
    // -------------------------------------------------------------------------

    /**
    * @brief Layout for network topology with no checksum policy.
    */
    template<typename ChecksumPolicy>
    struct packet_layout<true, false, ChecksumPolicy> {
        std::uint8_t _byte{};                         ///< Packed protocol byte.
        std::uint8_t sender_id{ECOMM_BOARD_ID};       ///< Sending node identifier.
        std::uint8_t receiver_id{};                   ///< Destination node identifier.

    protected:
        constexpr explicit packet_layout(std::uint8_t b) noexcept : _byte{b} {}
        constexpr packet_layout() noexcept = default;
    };

    // -------------------------------------------------------------------------
    // Specialisation 4: both node ids and FCS
    // Wire: [ _byte | fcs | sender_id | receiver_id ]
    // -------------------------------------------------------------------------

    /**
    * @brief Layout for network topology with a checksum policy.
    *
    * FCS remains at a fixed offset (immediately after `_byte`) so the
    * validator can zero it, hash the whole header+payload block, and
    * restore it without needing to know whether node ids are present.
    */
    template<typename ChecksumPolicy>
    struct packet_layout<true, true, ChecksumPolicy> {
        std::uint8_t                         _byte{};              ///< Packed protocol byte.
        typename ChecksumPolicy::value_type  fcs{};                ///< Frame check sequence.
        std::uint8_t                         sender_id{ECOMM_BOARD_ID}; ///< Sending node identifier.
        std::uint8_t                         receiver_id{};        ///< Destination node identifier.

    protected:
        constexpr explicit packet_layout(std::uint8_t b) noexcept : _byte{b} {}
        constexpr packet_layout() noexcept = default;
    };

    #pragma pack(pop)

} // namespace ecomm::protocol::details

#endif // ECOMM_PROTOCOL_PACKET_LAYOUT_HPP_
