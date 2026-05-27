// SPDX-License-Identifier: BSL-1.1
/**
* @file packet_header.tpp
*
* @brief Constructor and accessor definitions for all `packet_header`
*        partial specialisations.
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
* - 2026-05-27 Standard-layout refactor: _byte inherited from header_layout;
*              constructor delegates to the base.
* - 2026-05-27 Private inheritance refactor: one definition block per
*              specialisation; primary template is undefined.
*/
#ifndef ECOMM_PROTOCOL_PACKET_HEADER_TPP_
#define ECOMM_PROTOCOL_PACKET_HEADER_TPP_

#include "packet_header.hpp"

namespace ecomm::protocol {

// =============================================================================
// Common packing note (applies to every constructor below)
// =============================================================================
//
// The protocol byte is packed using explicit shifts and masks — never C++
// bitfields, whose layout is implementation-defined and therefore not safe
// for wire protocols.
//
//   Bits 7..5 — type    : shift the 3-bit enumerator to the top position.
//   Bits 4..2 — options : enumerator values are pre-shifted; stray bits masked.
//   Bits 1..0 — version : 2-bit constant ECOMM_PROTOCOL_VERSION.
//

// =============================================================================
// Specialisation 1 — point-to-point, no checksum
// =============================================================================

constexpr
packet_header<topology::point_to_point, none>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)    |
          (static_cast<std::uint8_t>(opts) & header_options_mask) |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

constexpr header_type
packet_header<topology::point_to_point, none>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

constexpr header_options
packet_header<topology::point_to_point, none>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

constexpr bool
packet_header<topology::point_to_point, none>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

constexpr std::uint8_t
packet_header<topology::point_to_point, none>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

constexpr std::uint8_t
packet_header<topology::point_to_point, none>::raw() const noexcept {
    return this->_byte;
}

// =============================================================================
// Specialisation 2 — point-to-point, with checksum
// =============================================================================

template<typename ChecksumPolicy>
constexpr
packet_header<topology::point_to_point, ChecksumPolicy>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)    |
          (static_cast<std::uint8_t>(opts) & header_options_mask) |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

template<typename ChecksumPolicy>
constexpr header_type
packet_header<topology::point_to_point, ChecksumPolicy>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

template<typename ChecksumPolicy>
constexpr header_options
packet_header<topology::point_to_point, ChecksumPolicy>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

template<typename ChecksumPolicy>
constexpr bool
packet_header<topology::point_to_point, ChecksumPolicy>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::point_to_point, ChecksumPolicy>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::point_to_point, ChecksumPolicy>::raw() const noexcept {
    return this->_byte;
}

// =============================================================================
// Specialisation 3 — network topology, no checksum
// =============================================================================

constexpr
packet_header<topology::network, none>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)    |
          (static_cast<std::uint8_t>(opts) & header_options_mask) |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

constexpr header_type
packet_header<topology::network, none>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

constexpr header_options
packet_header<topology::network, none>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

constexpr bool
packet_header<topology::network, none>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

constexpr std::uint8_t
packet_header<topology::network, none>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

constexpr std::uint8_t
packet_header<topology::network, none>::raw() const noexcept {
    return this->_byte;
}

// =============================================================================
// Specialisation 4 — network topology, with checksum
// =============================================================================

template<typename ChecksumPolicy>
constexpr
packet_header<topology::network, ChecksumPolicy>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)    |
          (static_cast<std::uint8_t>(opts) & header_options_mask) |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

template<typename ChecksumPolicy>
constexpr header_type
packet_header<topology::network, ChecksumPolicy>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

template<typename ChecksumPolicy>
constexpr header_options
packet_header<topology::network, ChecksumPolicy>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

template<typename ChecksumPolicy>
constexpr bool
packet_header<topology::network, ChecksumPolicy>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::network, ChecksumPolicy>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::network, ChecksumPolicy>::raw() const noexcept {
    return this->_byte;
}

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_PACKET_HEADER_TPP_
