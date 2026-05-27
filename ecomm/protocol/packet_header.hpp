// SPDX-License-Identifier: BSL-1.1
/**
* @file packet_header.hpp
*
* @brief Protocol packet header for the ecomm communication library.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* `packet_header<Topology, ChecksumPolicy>` is the compile-time-parameterised
* header that sits at the start of every ecomm packet. It owns every
* protocol-level field — type, options, version, optional node ids, optional FCS
* — and exposes typed accessors. Validation lives in `validator.hpp`.
*
* @par Standard-layout guarantee
* `packet_header` declares **no data members of its own**. All storage is
* inherited from `details::packet_layout`, a single standard-layout base struct
* selected by partial specialisation. This ensures that `offsetof` on any field
* is well-defined and the in-memory byte order exactly matches the wire layout
* described below.
*
* @par Wire layout
* ```
* +-----------------+------------------------------+----------------+------------------+
* | proto byte (1B) | fcs (ChecksumPolicy::size) $ | sender_id (*)  | receiver_id (*)  |
* +-----------------+------------------------------+----------------+------------------+
* (*) only present when Topology == topology::network
* ($) only present when ChecksumPolicy != ecomm::protocol::none
* ```
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
* - 2026-05-27 Standard-layout refactor: data moved to packet_layout base;
*              packet_header now owns zero data members. header_type,
*              header_options, node_ids, and packet_layout extracted to
*              separate headers. Added header_options::ack; version field
*              narrowed to 1 bit (bit 0) to free bit 1 for ack.
*/
#ifndef ECOMM_PROTOCOL_PACKET_HEADER_HPP_
#define ECOMM_PROTOCOL_PACKET_HEADER_HPP_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "config.hpp"
#include "header_options.hpp"
#include "header_type.hpp"
#include "packet_layout.hpp"
#include "topology.hpp"

namespace ecomm::protocol {

    #pragma pack(push, 1)

    /**
    * @class packet_header
    *
    * @brief Compact, standard-layout protocol header. Data only; logic is
    *        accessor-only. Validation lives in `validator.hpp`.
    *
    * Inherits all data members from `details::packet_layout` and adds no
    * storage of its own, satisfying the C++17 standard-layout rules so that
    * `offsetof` and raw byte-cast access to any field is well-defined.
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
    class packet_header
        : public details::packet_layout<
              (Topology == topology::network),
              !std::is_same_v<ChecksumPolicy, none>,
              ChecksumPolicy
          >
    {
        using layout = details::packet_layout<
            (Topology == topology::network),
            !std::is_same_v<ChecksumPolicy, none>,
            ChecksumPolicy
        >;

    public:

        /**
        * @brief Number of bytes the FCS field occupies in the wire layout.
        *
        * Zero when `ChecksumPolicy` is `none` — the header carries no FCS bytes.
        */
        static constexpr std::size_t fcs_size = ChecksumPolicy::size;

        /**
        * @brief `true` when this instantiation carries sender and receiver node ids.
        *
        * `true` for `topology::network`; `false` for `topology::point_to_point`.
        */
        static constexpr bool has_node_ids = (Topology == topology::network);

        /**
        * @brief Default constructor. Zero-initialises every field.
        *
        * Protocol byte is `0x00`: type = `data`, no options, version = 0.
        * Note version bits are **not** set to `ECOMM_PROTOCOL_VERSION` —
        * use the two-parameter constructor for wire-ready headers.
        *
        * @post `raw() == 0`. `type() == header_type::data`.
        *       `options() == header_options::none`. `version() == 0`.
        */
        constexpr packet_header() noexcept = default;

        /**
        * @brief Construct a wire-ready header with a given type and options.
        *
        * Packs `type` into bits 7..5, `opts` (masked to the option bits)
        * into bits 4..1, and `ECOMM_PROTOCOL_VERSION` into bit 0.
        *
        * @param[in] type  Packet classification. Must be one of the six defined
        *                  `header_type` enumerators; reserved encodings (0x6, 0x7)
        *                  are stored as-is and will confuse the receiving peer.
        * @param[in] opts  OR-combination of `header_options` flags. Bits outside
        *                  `header_options_mask` are silently stripped before storage.
        *
        * @post `this->type() == type`. `has(opts)` is `true`.
        *       `version() == ECOMM_PROTOCOL_VERSION`.
        */
        constexpr packet_header(header_type type, header_options opts) noexcept;

        /**
        * @brief Extract the packet type from the protocol byte.
        *
        * @return `header_type` encoded in bits 7..5. One of the six defined
        *         enumerators for well-formed packets.
        */
        [[nodiscard]] constexpr header_type type() const noexcept;

        /**
        * @brief Extract the complete option-flag set from the protocol byte.
        *
        * @return All currently-set `header_options` flags combined. May be
        *         `header_options::none` when no flags are active.
        *
        * @note Prefer `has(opt)` for single-flag membership tests.
        */
        [[nodiscard]] constexpr header_options options() const noexcept;

        /**
        * @brief Test whether all bits in `opt` are set in this header.
        *
        * Returns `true` iff every bit in `opt` is also set in `options()`.
        * For a multi-flag argument (e.g. `error | encrypted`) all flags must
        * be present simultaneously.
        *
        * @param[in] opt Flag or flag-combination to test.
        * @return `true` if every bit in `opt` is set; `false` otherwise.
        */
        [[nodiscard]] constexpr bool has(header_options opt) const noexcept;

        /**
        * @brief Extract the protocol version from the protocol byte.
        *
        * @return Version value in bits 1..0. Equals `ECOMM_PROTOCOL_VERSION` for
        *         headers produced by the two-parameter constructor.
        */
        [[nodiscard]] constexpr std::uint8_t version() const noexcept;

        /**
        * @brief Return the raw protocol byte without decoding.
        *
        * Intended for serialisation and low-level debugging. Prefer the typed
        * accessors for all protocol-logic use.
        *
        * @return The eight-bit protocol byte: bits 7..5 = type, 4..2 = options,
        *         1..0 = version.
        */
        [[nodiscard]] constexpr std::uint8_t raw() const noexcept;
    };

    #pragma pack(pop)

} // namespace ecomm::protocol

#include "packet_header.tpp"
#endif // ECOMM_PROTOCOL_PACKET_HEADER_HPP_
