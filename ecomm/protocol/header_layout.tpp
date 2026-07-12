// SPDX-License-Identifier: MIT
/**
* @file header_layout.tpp
*
* @brief Constructor definitions for `details::header_layout` specialisations.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-27
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-27 Initial creation alongside header_layout.hpp.
* - 2026-05-27 Updated specialisation keys: bool HasIds replaced by topology.
* - 2026-05-27 Added SequencePolicy parameter; eight specialisations total.
*/
#ifndef ECOMM_PROTOCOL_HEADER_LAYOUT_TPP_
#define ECOMM_PROTOCOL_HEADER_LAYOUT_TPP_

#include "header_layout.hpp"

namespace ecomm::protocol::details {

    // Specialisation 1: <topology::point_to_point, no_sequence, none>
    constexpr header_layout<topology::point_to_point, no_sequence, none>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

    // Specialisation 2: <topology::point_to_point, no_sequence, ChecksumPolicy>
    template<typename ChecksumPolicy>
    constexpr header_layout<topology::point_to_point, no_sequence, ChecksumPolicy>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

    // Specialisation 3: <topology::point_to_point, sequenced, none>
    constexpr header_layout<topology::point_to_point, sequenced, none>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

    // Specialisation 4: <topology::point_to_point, sequenced, ChecksumPolicy>
    template<typename ChecksumPolicy>
    constexpr header_layout<topology::point_to_point, sequenced, ChecksumPolicy>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

    // Specialisation 5: <topology::network, no_sequence, none>
    constexpr header_layout<topology::network, no_sequence, none>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

    // Specialisation 6: <topology::network, no_sequence, ChecksumPolicy>
    template<typename ChecksumPolicy>
    constexpr header_layout<topology::network, no_sequence, ChecksumPolicy>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

    // Specialisation 7: <topology::network, sequenced, none>
    constexpr header_layout<topology::network, sequenced, none>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

    // Specialisation 8: <topology::network, sequenced, ChecksumPolicy>
    template<typename ChecksumPolicy>
    constexpr header_layout<topology::network, sequenced, ChecksumPolicy>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

} // namespace ecomm::protocol::details

#endif // ECOMM_PROTOCOL_HEADER_LAYOUT_TPP_
