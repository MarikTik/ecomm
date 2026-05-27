// SPDX-License-Identifier: BSL-1.1
/**
* @file header_layout.hpp
*
* @brief `header_layout` — byte-exact wire storage for `packet_header`.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* Defines `details::header_layout<HasIds, ChecksumPolicy>`, the single
* private base struct that carries every data member of `packet_header`.
*
* @par Why this exists — the standard-layout requirement
* C++17 §11.4 requires that all non-static data members (including those of
* base classes) reside in exactly *one* class in the hierarchy for a type to
* be standard-layout. Only when the type is standard-layout are `offsetof` on
* its fields well-defined and raw byte-cast access to a wire buffer safe.
*
* `header_layout` is that single owner: it declares every field; `packet_header`
* inherits from it **privately** and adds no data members of its own.
* Private inheritance hides `_byte` completely — the caller can only reach it
* through `packet_header`'s typed accessors — while the two user-visible fields
* (`fcs`, `sender_id`/`receiver_id`) are selectively re-exposed by
* `packet_header` with `using` declarations.
*
* Private inheritance still satisfies C++17 standard-layout (the access
* specifier on the base class is irrelevant to the standard-layout check).
*
* @par Two parameters, four wire layouts
* `ChecksumPolicy` is its own presence flag: `none` means no FCS field;
* any other policy adds `fcs` of type `ChecksumPolicy::value_type`.
* There is no separate `HasFcs` boolean — it would be redundant and error-prone.
*
* FCS is the **last** field in every layout that carries it.  Node ids
* (if present) come immediately after `_byte`; FCS follows them.  This
* places all addressing information contiguously at the front of the header
* and leaves the FCS in a well-known trailing position.
*
* | HasIds | ChecksumPolicy | Wire layout (field declaration order)             |
* |--------|----------------|---------------------------------------------------|
* | false  | `none`         | `_byte`                                           |
* | false  | non-`none`     | `_byte`, `fcs`                                    |
* | true   | `none`         | `_byte`, `sender_id`, `receiver_id`               |
* | true   | non-`none`     | `_byte`, `sender_id`, `receiver_id`, `fcs`        |
*
* @par `#pragma pack(push, 1)`
* Applied around all four specialisations. No compiler-inserted padding appears
* between fields, ensuring a byte-exact wire layout on every target. Closed
* (`pop`) before the closing `#endif`.
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
*              Dropped redundant HasFcs parameter — specialise on ChecksumPolicy
*              directly so `none` vs non-`none` is the dispatch axis.
*              Moved FCS to the last field position (was between _byte and ids).
*              Constructor bodies moved to header_layout.tpp per .hpp/.tpp rule.
*/
#ifndef ECOMM_PROTOCOL_HEADER_LAYOUT_HPP_
#define ECOMM_PROTOCOL_HEADER_LAYOUT_HPP_

#include <cstdint>

#include "checksum.hpp"
#include "node_ids.hpp"

namespace ecomm::protocol::details {

    /**
    * @struct header_layout
    *
    * @brief Primary template — intentionally left undefined.
    *
    * Only the four explicit partial specialisations below are valid.
    * Any other `<HasIds, ChecksumPolicy>` combination produces a compile error
    * rather than silently generating a wrong layout.
    *
    * @tparam HasIds         `true` when `Topology == topology::network`.
    *                         Adds `sender_id` and `receiver_id` between
    *                         `_byte` and the trailing `fcs` (if any).
    * @tparam ChecksumPolicy Checksum tag from `checksum.hpp`.
    *                         `none` → no `fcs` field.
    *                         Any other policy → `fcs` field of type
    *                         `ChecksumPolicy::value_type` at the end of the struct.
    */
    template<bool HasIds, typename ChecksumPolicy>
    struct header_layout;

    #pragma pack(push, 1)

    // -------------------------------------------------------------------------
    // Specialisation 1 — point-to-point, no checksum
    // Wire layout: [ _byte ]
    // -------------------------------------------------------------------------

    /**
    * @brief Minimal header: a single protocol byte, no FCS, no node ids.
    */
    template<>
    struct header_layout<false, none> {

        /**
        * @brief Packed protocol byte.
        *
        * Bit layout: bits 7..5 = header_type (3 bits),
        *             bits 4..2 = header_options (3 bits: error, ack, encrypted),
        *             bits 1..0 = protocol version (2 bits).
        *
        * Not part of the public API of `packet_header`. Access it through the
        * typed accessors `type()`, `options()`, `version()`, and `raw()`.
        */
        std::uint8_t _byte{};

    protected:

        /// Initialise `_byte` directly; used by `packet_header`'s constructor.
        constexpr explicit header_layout(std::uint8_t b) noexcept;

        constexpr header_layout() noexcept = default;
    };

    // -------------------------------------------------------------------------
    // Specialisation 2 — point-to-point, with checksum
    // Wire layout: [ _byte | fcs ]
    // -------------------------------------------------------------------------

    /**
    * @brief Point-to-point header with a trailing FCS field.
    *
    * `fcs` is the last (and only non-`_byte`) field. It is zeroed on
    * construction, written by `validator::seal`, and checked by
    * `validator::is_valid`.
    */
    template<typename ChecksumPolicy>
    struct header_layout<false, ChecksumPolicy> {

        /// @copydoc header_layout<false,none>::_byte
        std::uint8_t _byte{};

        /**
        * @brief Frame check sequence — zero until `validator::seal` is called.
        *
        * Width is `ChecksumPolicy::size` bytes. Placed last so that the
        * checksum covers `_byte` and any payload that follows the header.
        */
        typename ChecksumPolicy::value_type fcs{};

    protected:

        /// @copydoc header_layout<false,none>::header_layout(std::uint8_t)
        constexpr explicit header_layout(std::uint8_t b) noexcept;

        constexpr header_layout() noexcept = default;
    };

    // -------------------------------------------------------------------------
    // Specialisation 3 — network topology, no checksum
    // Wire layout: [ _byte | sender_id | receiver_id ]
    // -------------------------------------------------------------------------

    /**
    * @brief Network header with node ids and no FCS.
    *
    * `sender_id` defaults to `ECOMM_BOARD_ID`; `receiver_id` defaults to 0.
    * Both are public fields in `packet_header` via `using` re-export.
    */
    template<>
    struct header_layout<true, none> {

        /// @copydoc header_layout<false,none>::_byte
        std::uint8_t _byte{};

        /// Identifier of the node that originated this packet. Defaults to `ECOMM_BOARD_ID`.
        std::uint8_t sender_id{ECOMM_BOARD_ID};

        /// Identifier of the intended recipient. Caller must set before transmission.
        std::uint8_t receiver_id{};

    protected:

        /// @copydoc header_layout<false,none>::header_layout(std::uint8_t)
        constexpr explicit header_layout(std::uint8_t b) noexcept;

        constexpr header_layout() noexcept = default;
    };

    // -------------------------------------------------------------------------
    // Specialisation 4 — network topology, with checksum
    // Wire layout: [ _byte | sender_id | receiver_id | fcs ]
    // -------------------------------------------------------------------------

    /**
    * @brief Network header with node ids and a trailing FCS field.
    *
    * Field order keeps all addressing information contiguous at the front
    * (`_byte`, `sender_id`, `receiver_id`) and places the integrity field
    * last (`fcs`), consistent with the no-ids layout.
    */
    template<typename ChecksumPolicy>
    struct header_layout<true, ChecksumPolicy> {

        /// @copydoc header_layout<false,none>::_byte
        std::uint8_t _byte{};

        /// @copydoc header_layout<true,none>::sender_id
        std::uint8_t sender_id{ECOMM_BOARD_ID};

        /// @copydoc header_layout<true,none>::receiver_id
        std::uint8_t receiver_id{};

        /// @copydoc header_layout<false,ChecksumPolicy>::fcs
        typename ChecksumPolicy::value_type fcs{};

    protected:

        /// @copydoc header_layout<false,none>::header_layout(std::uint8_t)
        constexpr explicit header_layout(std::uint8_t b) noexcept;

        constexpr header_layout() noexcept = default;
    };

    #pragma pack(pop)

} // namespace ecomm::protocol::details

#include "header_layout.tpp"
#endif // ECOMM_PROTOCOL_HEADER_LAYOUT_HPP_
