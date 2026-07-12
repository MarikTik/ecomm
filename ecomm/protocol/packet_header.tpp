// SPDX-License-Identifier: MIT
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
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-26 Initial creation alongside the packet_header rewrite.
* - 2026-05-27 Standard-layout refactor: _byte inherited from header_layout;
*              constructor delegates to the base.
* - 2026-05-27 Private inheritance refactor: one definition block per
*              specialisation; primary template is undefined.
* - 2026-05-27 Added SequencePolicy parameter; eight definition blocks total.
*/
#ifndef ECOMM_PROTOCOL_PACKET_HEADER_TPP_
#define ECOMM_PROTOCOL_PACKET_HEADER_TPP_

#include "packet_header.hpp"

namespace ecomm::protocol {

// =============================================================================
// Common packing note (applies to every constructor below)
// =============================================================================
//
// The protocol byte is packed using explicit shifts and masks  --  never C++
// bitfields, whose layout is implementation-defined and therefore not safe
// for wire protocols.
//
//   Bits 7..5  --  type    : shift the 3-bit enumerator to the top position.
//   Bits 4..2  --  options : enumerator values are pre-shifted; stray bits masked.
//   Bits 1..0  --  version : 2-bit constant ECOMM_PROTOCOL_VERSION.
//

// =============================================================================
// Specialisation 1  --  point-to-point, no sequence, no checksum
// =============================================================================

constexpr
packet_header<topology::point_to_point, no_sequence, none>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)         |
          (static_cast<std::uint8_t>(opts) & header_options_mask)  |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

constexpr header_type
packet_header<topology::point_to_point, no_sequence, none>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

constexpr header_options
packet_header<topology::point_to_point, no_sequence, none>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

constexpr bool
packet_header<topology::point_to_point, no_sequence, none>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

constexpr std::uint8_t
packet_header<topology::point_to_point, no_sequence, none>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

constexpr std::uint8_t
packet_header<topology::point_to_point, no_sequence, none>::raw() const noexcept {
    return this->_byte;
}

// =============================================================================
// Specialisation 2  --  point-to-point, no sequence, with checksum
// =============================================================================

template<typename ChecksumPolicy>
constexpr
packet_header<topology::point_to_point, no_sequence, ChecksumPolicy>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)         |
          (static_cast<std::uint8_t>(opts) & header_options_mask)  |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

template<typename ChecksumPolicy>
constexpr header_type
packet_header<topology::point_to_point, no_sequence, ChecksumPolicy>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

template<typename ChecksumPolicy>
constexpr header_options
packet_header<topology::point_to_point, no_sequence, ChecksumPolicy>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

template<typename ChecksumPolicy>
constexpr bool
packet_header<topology::point_to_point, no_sequence, ChecksumPolicy>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::point_to_point, no_sequence, ChecksumPolicy>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::point_to_point, no_sequence, ChecksumPolicy>::raw() const noexcept {
    return this->_byte;
}

// =============================================================================
// Specialisation 3  --  point-to-point, sequenced, no checksum
// =============================================================================

constexpr
packet_header<topology::point_to_point, sequenced, none>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)         |
          (static_cast<std::uint8_t>(opts) & header_options_mask)  |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

constexpr header_type
packet_header<topology::point_to_point, sequenced, none>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

constexpr header_options
packet_header<topology::point_to_point, sequenced, none>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

constexpr bool
packet_header<topology::point_to_point, sequenced, none>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

constexpr std::uint8_t
packet_header<topology::point_to_point, sequenced, none>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

constexpr std::uint8_t
packet_header<topology::point_to_point, sequenced, none>::raw() const noexcept {
    return this->_byte;
}

// =============================================================================
// Specialisation 4  --  point-to-point, sequenced, with checksum
// =============================================================================

template<typename ChecksumPolicy>
constexpr
packet_header<topology::point_to_point, sequenced, ChecksumPolicy>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)         |
          (static_cast<std::uint8_t>(opts) & header_options_mask)  |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

template<typename ChecksumPolicy>
constexpr header_type
packet_header<topology::point_to_point, sequenced, ChecksumPolicy>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

template<typename ChecksumPolicy>
constexpr header_options
packet_header<topology::point_to_point, sequenced, ChecksumPolicy>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

template<typename ChecksumPolicy>
constexpr bool
packet_header<topology::point_to_point, sequenced, ChecksumPolicy>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::point_to_point, sequenced, ChecksumPolicy>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::point_to_point, sequenced, ChecksumPolicy>::raw() const noexcept {
    return this->_byte;
}

// =============================================================================
// Specialisation 5  --  network topology, no sequence, no checksum
// =============================================================================

constexpr
packet_header<topology::network, no_sequence, none>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)         |
          (static_cast<std::uint8_t>(opts) & header_options_mask)  |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

constexpr header_type
packet_header<topology::network, no_sequence, none>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

constexpr header_options
packet_header<topology::network, no_sequence, none>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

constexpr bool
packet_header<topology::network, no_sequence, none>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

constexpr std::uint8_t
packet_header<topology::network, no_sequence, none>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

constexpr std::uint8_t
packet_header<topology::network, no_sequence, none>::raw() const noexcept {
    return this->_byte;
}

// =============================================================================
// Specialisation 6  --  network topology, no sequence, with checksum
// =============================================================================

template<typename ChecksumPolicy>
constexpr
packet_header<topology::network, no_sequence, ChecksumPolicy>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)         |
          (static_cast<std::uint8_t>(opts) & header_options_mask)  |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

template<typename ChecksumPolicy>
constexpr header_type
packet_header<topology::network, no_sequence, ChecksumPolicy>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

template<typename ChecksumPolicy>
constexpr header_options
packet_header<topology::network, no_sequence, ChecksumPolicy>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

template<typename ChecksumPolicy>
constexpr bool
packet_header<topology::network, no_sequence, ChecksumPolicy>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::network, no_sequence, ChecksumPolicy>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::network, no_sequence, ChecksumPolicy>::raw() const noexcept {
    return this->_byte;
}

// =============================================================================
// Specialisation 7  --  network topology, sequenced, no checksum
// =============================================================================

constexpr
packet_header<topology::network, sequenced, none>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)         |
          (static_cast<std::uint8_t>(opts) & header_options_mask)  |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

constexpr header_type
packet_header<topology::network, sequenced, none>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

constexpr header_options
packet_header<topology::network, sequenced, none>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

constexpr bool
packet_header<topology::network, sequenced, none>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

constexpr std::uint8_t
packet_header<topology::network, sequenced, none>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

constexpr std::uint8_t
packet_header<topology::network, sequenced, none>::raw() const noexcept {
    return this->_byte;
}

// =============================================================================
// Specialisation 8  --  network topology, sequenced, with checksum
// =============================================================================

template<typename ChecksumPolicy>
constexpr
packet_header<topology::network, sequenced, ChecksumPolicy>::packet_header(
    header_type    type,
    header_options opts
) noexcept
    : layout{static_cast<std::uint8_t>(
          ((static_cast<std::uint8_t>(type) & 0x7u) << 5)         |
          (static_cast<std::uint8_t>(opts) & header_options_mask)  |
          (static_cast<std::uint8_t>(ECOMM_PROTOCOL_VERSION) & 0x3u)
      )}
{}

template<typename ChecksumPolicy>
constexpr header_type
packet_header<topology::network, sequenced, ChecksumPolicy>::type() const noexcept {
    return static_cast<header_type>((this->_byte >> 5) & 0x7u);
}

template<typename ChecksumPolicy>
constexpr header_options
packet_header<topology::network, sequenced, ChecksumPolicy>::options() const noexcept {
    return static_cast<header_options>(this->_byte & header_options_mask);
}

template<typename ChecksumPolicy>
constexpr bool
packet_header<topology::network, sequenced, ChecksumPolicy>::has(header_options opt) const noexcept {
    return (options() & opt) == opt;
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::network, sequenced, ChecksumPolicy>::version() const noexcept {
    return static_cast<std::uint8_t>(this->_byte & 0x3u);
}

template<typename ChecksumPolicy>
constexpr std::uint8_t
packet_header<topology::network, sequenced, ChecksumPolicy>::raw() const noexcept {
    return this->_byte;
}

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_PACKET_HEADER_TPP_
