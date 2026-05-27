// SPDX-License-Identifier: BSL-1.1
/**
* @file packet_header.hpp
*
* @brief Protocol packet header for the ecomm communication library.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* `packet_header<Topology, ChecksumPolicy>` is the compile-time-parameterised
* header that sits at the front of every ecomm packet. It carries every
* protocol-level field — type, options, version, optional node ids, optional
* FCS — and exposes them through typed accessors. Validation lives in
* `validator.hpp`.
*
* @par Standard-layout guarantee
* `packet_header` declares **no data members of its own**. All storage is
* inherited from `details::header_layout`, a single standard-layout base struct
* selected by partial specialisation. This ensures that `offsetof` on every
* field is well-defined and the in-memory byte order matches the wire layout
* described below exactly.
*
* @par Encapsulation of the protocol byte
* `packet_header` uses **private inheritance** from `details::header_layout`.
* This hides the raw `_byte` field completely — callers can only reach it
* through the typed accessors `type()`, `options()`, `version()`, and `raw()`.
* The user-visible fields `fcs`, `sender_id`, and `receiver_id` are promoted
* with `using` declarations in each partial specialisation that carries them.
*
* Private inheritance still satisfies C++17 standard-layout (the access
* specifier on the base class does not affect the standard-layout check).
*
* @par Wire layout
* ```
* +------------------+-----------------+------------------+-------------------------------+
* | proto byte  (1B) | sender_id (*)   | receiver_id (*)  | fcs  (ChecksumPolicy::size) $ |
* +------------------+-----------------+------------------+-------------------------------+
* (*) only present when Topology == topology::network
* ($) only present when ChecksumPolicy != ecomm::protocol::none
* ```
* FCS is always the **last** field of the header, trailing all addressing bytes.
*
* @par Protocol byte layout
* ```
*  7..5 : type      (3 bits) — header_type enum, 6 values used (2 reserved)
*     4 : error     (1 bit)  — header_options::error
*     3 : ack       (1 bit)  — header_options::ack
*     2 : encrypted (1 bit)  — header_options::encrypted
*  1..0 : version   (2 bits) — internal, locked to ECOMM_PROTOCOL_VERSION
* ```
*
* @note Version is not a constructor parameter. It is set automatically to
*       `ECOMM_PROTOCOL_VERSION` by the two-parameter constructor.
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
* - 2025-07-03 Initial creation (legacy layout).
* - 2026-05-26 Major rewrite: templated on Topology + ChecksumPolicy.
* - 2026-05-27 Standard-layout refactor: data moved to header_layout base;
*              packet_header owns zero data members. header_type, header_options,
*              node_ids, and header_layout extracted to separate headers.
*              Added header_options::ack; version narrowed to bits 1..0 (2 bits).
* - 2026-05-27 Private inheritance: _byte hidden from callers; fcs / node ids
*              re-exported via using in each partial specialisation. FCS moved to
*              the last field position. Primary template is now undefined; four
*              explicit partial specialisations cover all valid combinations.
*/
#ifndef ECOMM_PROTOCOL_PACKET_HEADER_HPP_
#define ECOMM_PROTOCOL_PACKET_HEADER_HPP_

#include <cstddef>
#include <cstdint>

#include "config.hpp"
#include "header_layout.hpp"
#include "header_options.hpp"
#include "header_type.hpp"
#include "topology.hpp"

namespace ecomm::protocol {

    #pragma pack(push, 1)

    // =========================================================================
    // Primary template — intentionally left undefined.
    //
    // Only the four partial specialisations below are valid instantiations.
    // Any unsupported <Topology, ChecksumPolicy> pair produces a compile error
    // rather than silently generating a wrong type.
    // =========================================================================

    /**
    * @class packet_header
    *
    * @brief Compact, standard-layout protocol header.
    *
    * Data-only type; logic is accessor-only. Validation lives in `validator.hpp`.
    * Four partial specialisations cover the topology × checksum combinations.
    *
    * @tparam Topology       Wire shape. `point_to_point` omits node ids;
    *                        `network` adds `sender_id` and `receiver_id`.
    *                        Defaults to `default_topology`.
    * @tparam ChecksumPolicy Checksum algorithm tag from `checksum.hpp`. `none`
    *                        removes the FCS field entirely. Defaults to `none`.
    */
    template<
        topology Topology       = default_topology,
        typename ChecksumPolicy = none
    >
    class packet_header;

    // =========================================================================
    // Specialisation 1 — point-to-point, no checksum
    // Public surface: typed accessors only. No extra fields.
    // =========================================================================

    /**
    * @brief Point-to-point header with no checksum. Exactly 1 byte on the wire.
    */
    template<>
    class packet_header<topology::point_to_point, none>
        : private details::header_layout<false, none>
    {
        using layout = details::header_layout<false, none>;

    public:

        /// @brief FCS field size. Always 0 for the `none` checksum policy.
        static constexpr std::size_t fcs_size = none::size;

        /// @brief `false` — this specialisation carries no node ids.
        static constexpr bool has_node_ids = false;

        /**
        * @brief Default constructor. Zero-initialises `_byte`.
        *
        * @post `raw() == 0`. `type() == header_type::data`.
        *       `options() == header_options::none`. `version() == 0`.
        */
        constexpr packet_header() noexcept = default;

        /**
        * @brief Construct a wire-ready header.
        *
        * Packs `type` into bits 7..5, `opts` (masked to `header_options_mask`)
        * into bits 4..2, and `ECOMM_PROTOCOL_VERSION` into bits 1..0.
        *
        * @param[in] type  Packet classification.
        * @param[in] opts  OR-combination of `header_options` flags.
        *
        * @post `this->type() == type`. `has(opts)` is `true`.
        *       `version() == ECOMM_PROTOCOL_VERSION`.
        */
        constexpr packet_header(header_type type, header_options opts) noexcept;

        /// @brief Extract the packet type from bits 7..5.
        [[nodiscard]] constexpr header_type    type()    const noexcept;

        /// @brief Extract all option flags from bits 4..2.
        /// @note Prefer `has(opt)` for single-flag tests.
        [[nodiscard]] constexpr header_options options() const noexcept;

        /// @brief Test whether every bit in `opt` is set.
        [[nodiscard]] constexpr bool           has(header_options opt) const noexcept;

        /// @brief Extract the protocol version from bits 1..0.
        [[nodiscard]] constexpr std::uint8_t   version() const noexcept;

        /// @brief Return the raw protocol byte.
        [[nodiscard]] constexpr std::uint8_t   raw()     const noexcept;
    };

    // =========================================================================
    // Specialisation 2 — point-to-point, with checksum
    // Public surface: typed accessors + fcs.
    // =========================================================================

    /**
    * @brief Point-to-point header with a trailing FCS field.
    *
    * Wire layout: `[ _byte (1B) | fcs (ChecksumPolicy::size B) ]`.
    */
    template<typename ChecksumPolicy>
    class packet_header<topology::point_to_point, ChecksumPolicy>
        : private details::header_layout<false, ChecksumPolicy>
    {
        using layout = details::header_layout<false, ChecksumPolicy>;

    public:

        /// @brief FCS field size in bytes (`ChecksumPolicy::size`).
        static constexpr std::size_t fcs_size = ChecksumPolicy::size;

        /// @brief `false` — this specialisation carries no node ids.
        static constexpr bool has_node_ids = false;

        /**
        * @brief Frame check sequence — zero until `validator::seal` is called.
        *
        * This is the last field in the wire layout. Width is `fcs_size` bytes.
        * Written by `validator::seal`; checked by `validator::is_valid`.
        */
        using layout::fcs;

        /// @copydoc packet_header<topology::point_to_point,none>::packet_header()
        constexpr packet_header() noexcept = default;

        /// @copydoc packet_header<topology::point_to_point,none>::packet_header(header_type,header_options)
        constexpr packet_header(header_type type, header_options opts) noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::type()
        [[nodiscard]] constexpr header_type    type()    const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::options()
        [[nodiscard]] constexpr header_options options() const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::has()
        [[nodiscard]] constexpr bool           has(header_options opt) const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::version()
        [[nodiscard]] constexpr std::uint8_t   version() const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::raw()
        [[nodiscard]] constexpr std::uint8_t   raw()     const noexcept;
    };

    // =========================================================================
    // Specialisation 3 — network topology, no checksum
    // Public surface: typed accessors + sender_id + receiver_id.
    // =========================================================================

    /**
    * @brief Network header with node ids and no FCS.
    *
    * Wire layout: `[ _byte (1B) | sender_id (1B) | receiver_id (1B) ]`.
    */
    template<>
    class packet_header<topology::network, none>
        : private details::header_layout<true, none>
    {
        using layout = details::header_layout<true, none>;

    public:

        /// @brief FCS field size. Always 0 for the `none` checksum policy.
        static constexpr std::size_t fcs_size = none::size;

        /// @brief `true` — this specialisation carries `sender_id` and `receiver_id`.
        static constexpr bool has_node_ids = true;

        /**
        * @brief Identifier of the node that originated this packet.
        *
        * Defaults to `ECOMM_BOARD_ID` on construction. Set this before
        * forwarding on behalf of another node.
        */
        using layout::sender_id;

        /**
        * @brief Identifier of the intended recipient.
        *
        * Defaults to `0`. Caller must assign a valid node id before the packet
        * is passed to a channel's `send()`.
        */
        using layout::receiver_id;

        /// @copydoc packet_header<topology::point_to_point,none>::packet_header()
        constexpr packet_header() noexcept = default;

        /// @copydoc packet_header<topology::point_to_point,none>::packet_header(header_type,header_options)
        constexpr packet_header(header_type type, header_options opts) noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::type()
        [[nodiscard]] constexpr header_type    type()    const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::options()
        [[nodiscard]] constexpr header_options options() const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::has()
        [[nodiscard]] constexpr bool           has(header_options opt) const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::version()
        [[nodiscard]] constexpr std::uint8_t   version() const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::raw()
        [[nodiscard]] constexpr std::uint8_t   raw()     const noexcept;
    };

    // =========================================================================
    // Specialisation 4 — network topology, with checksum
    // Public surface: typed accessors + sender_id + receiver_id + fcs.
    // =========================================================================

    /**
    * @brief Network header with node ids and a trailing FCS field.
    *
    * Wire layout:
    * `[ _byte (1B) | sender_id (1B) | receiver_id (1B) | fcs (ChecksumPolicy::size B) ]`.
    *
    * All addressing fields (`sender_id`, `receiver_id`) precede the integrity
    * field (`fcs`) so that the FCS trails the complete address region.
    */
    template<typename ChecksumPolicy>
    class packet_header<topology::network, ChecksumPolicy>
        : private details::header_layout<true, ChecksumPolicy>
    {
        using layout = details::header_layout<true, ChecksumPolicy>;

    public:

        /// @brief FCS field size in bytes (`ChecksumPolicy::size`).
        static constexpr std::size_t fcs_size = ChecksumPolicy::size;

        /// @brief `true` — this specialisation carries `sender_id` and `receiver_id`.
        static constexpr bool has_node_ids = true;

        /// @copydoc packet_header<topology::network,none>::sender_id
        using layout::sender_id;

        /// @copydoc packet_header<topology::network,none>::receiver_id
        using layout::receiver_id;

        /// @copydoc packet_header<topology::point_to_point,ChecksumPolicy>::fcs
        using layout::fcs;

        /// @copydoc packet_header<topology::point_to_point,none>::packet_header()
        constexpr packet_header() noexcept = default;

        /// @copydoc packet_header<topology::point_to_point,none>::packet_header(header_type,header_options)
        constexpr packet_header(header_type type, header_options opts) noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::type()
        [[nodiscard]] constexpr header_type    type()    const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::options()
        [[nodiscard]] constexpr header_options options() const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::has()
        [[nodiscard]] constexpr bool           has(header_options opt) const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::version()
        [[nodiscard]] constexpr std::uint8_t   version() const noexcept;

        /// @copydoc packet_header<topology::point_to_point,none>::raw()
        [[nodiscard]] constexpr std::uint8_t   raw()     const noexcept;
    };

    #pragma pack(pop)

} // namespace ecomm::protocol

#include "packet_header.tpp"
#endif // ECOMM_PROTOCOL_PACKET_HEADER_HPP_
