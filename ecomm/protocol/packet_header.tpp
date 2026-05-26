// SPDX-License-Identifier: BSL-1.1
/**
* @file packet_header.tpp
*
* @brief Definitions for the templated `packet_header` class.
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
*/
#ifndef ECOMM_PROTOCOL_PACKET_HEADER_TPP_
#define ECOMM_PROTOCOL_PACKET_HEADER_TPP_

#include "packet_header.hpp"

namespace ecomm::protocol {

    // -------------------------------------------------------------------------
    // packet_header — constructor
    // -------------------------------------------------------------------------

    template<topology Topology, typename ChecksumPolicy>
    constexpr packet_header<Topology, ChecksumPolicy>::packet_header(
        header_type    type,
        header_options opts
    ) noexcept
        // Pack three fields into one byte using explicit shifts and masks.
        // No C++ bitfields: bit-field layout is implementation-defined, which
        // would make the wire format non-portable across compilers and targets.
        //
        //  Bits 7..5  — type    : shift the 3-bit enumerator into the top position.
        //  Bits 4..2  — options : already at their final positions (see header_options);
        //                         mask away any stray bits outside header_options_mask.
        //  Bits 1..0  — version : mask to 2 bits; constant ECOMM_PROTOCOL_VERSION.
        : _byte{static_cast<std::uint8_t>(
              ((static_cast<std::uint8_t>(type) & 0x7) << 5) |
              (static_cast<std::uint8_t>(opts) & header_options_mask) |
              (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3)
          )}
    {}

    // -------------------------------------------------------------------------
    // packet_header — accessors
    // -------------------------------------------------------------------------

    template<topology Topology, typename ChecksumPolicy>
    constexpr header_type
    packet_header<Topology, ChecksumPolicy>::type() const noexcept {
        // Right-shift to align bits 7..5 at the LSB, then mask to 3 bits.
        return static_cast<header_type>((_byte >> 5) & 0x7);
    }

    template<topology Topology, typename ChecksumPolicy>
    constexpr header_options
    packet_header<Topology, ChecksumPolicy>::options() const noexcept {
        // Mask retains only bits 4..2; the result is already at the correct
        // positions matching the header_options enumerator values.
        return static_cast<header_options>(_byte & header_options_mask);
    }

    template<topology Topology, typename ChecksumPolicy>
    constexpr bool
    packet_header<Topology, ChecksumPolicy>::has(header_options opt) const noexcept {
        // Subset test: all bits in `opt` must be present in `options()`.
        return (options() & opt) == opt;
    }

    template<topology Topology, typename ChecksumPolicy>
    constexpr std::uint8_t
    packet_header<Topology, ChecksumPolicy>::version() const noexcept {
        // Version lives in bits 1..0; mask away the upper six bits.
        return static_cast<std::uint8_t>(_byte & 0x3);
    }

    template<topology Topology, typename ChecksumPolicy>
    constexpr std::uint8_t
    packet_header<Topology, ChecksumPolicy>::raw() const noexcept {
        return _byte;
    }

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_PACKET_HEADER_TPP_
