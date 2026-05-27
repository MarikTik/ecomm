// SPDX-License-Identifier: BSL-1.1
/**
* @file packet_header.tpp
*
* @brief Definitions for the `packet_header` constructors and accessors.
*
* @ingroup ecomm_protocol ecomm::protocol
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
* - 2026-05-26 Initial creation alongside the packet_header rewrite.
* - 2026-05-27 Standard-layout refactor: _byte is now inherited from
*              packet_layout; constructor delegates to the base. Version
*              narrowed to bit 0 (was bits 1..0); bit 1 now carries ack.
*/
#ifndef ECOMM_PROTOCOL_PACKET_HEADER_TPP_
#define ECOMM_PROTOCOL_PACKET_HEADER_TPP_

#include "packet_header.hpp"

namespace ecomm::protocol {

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------

    template<topology Topology, typename ChecksumPolicy>
    constexpr packet_header<Topology, ChecksumPolicy>::packet_header(
        header_type    type,
        header_options opts
    ) noexcept
        // Delegate to the packet_layout base constructor that accepts a uint8_t.
        // Pack three fields into one byte using explicit shifts and masks —
        // never C++ bitfields (bit-field layout is implementation-defined).
        //
        //  Bits 7..5  — type    : shift the 3-bit enumerator into the top position.
        //  Bits 4..2  — options : already at their final positions (see header_options);
        //                         mask away any stray bits outside header_options_mask.
        //  Bits 1..0  — version : 2 bits; constant ECOMM_PROTOCOL_VERSION.
        : layout{static_cast<std::uint8_t>(
              ((static_cast<std::uint8_t>(type) & 0x7u) << 5) |
              (static_cast<std::uint8_t>(opts) & header_options_mask)  |
              (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
          )}
    {}

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

    template<topology Topology, typename ChecksumPolicy>
    constexpr header_type
    packet_header<Topology, ChecksumPolicy>::type() const noexcept {
        // Right-shift to align bits 7..5 at the LSB, then mask to 3 bits.
        return static_cast<header_type>((this->_byte >> 5) & 0x7u);
    }

    template<topology Topology, typename ChecksumPolicy>
    constexpr header_options
    packet_header<Topology, ChecksumPolicy>::options() const noexcept {
        // Mask retains only the option bits; values already at correct positions.
        return static_cast<header_options>(this->_byte & header_options_mask);
    }

    template<topology Topology, typename ChecksumPolicy>
    constexpr bool
    packet_header<Topology, ChecksumPolicy>::has(header_options opt) const noexcept {
        return (options() & opt) == opt;
    }

    template<topology Topology, typename ChecksumPolicy>
    constexpr std::uint8_t
    packet_header<Topology, ChecksumPolicy>::version() const noexcept {
        // Version lives in bit 0.
        return static_cast<std::uint8_t>(this->_byte & 0x1u);
    }

    template<topology Topology, typename ChecksumPolicy>
    constexpr std::uint8_t
    packet_header<Topology, ChecksumPolicy>::raw() const noexcept {
        return this->_byte;
    }

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_PACKET_HEADER_TPP_
