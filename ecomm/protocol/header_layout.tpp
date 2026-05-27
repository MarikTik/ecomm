// SPDX-License-Identifier: BSL-1.1
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
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2026 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-27 Initial creation alongside header_layout.hpp.
* - 2026-05-27 Updated specialisation keys: bool HasIds replaced by topology.
*/
#ifndef ECOMM_PROTOCOL_HEADER_LAYOUT_TPP_
#define ECOMM_PROTOCOL_HEADER_LAYOUT_TPP_

#include "header_layout.hpp"

namespace ecomm::protocol::details {

    // Specialisation 1: <topology::point_to_point, none>
    constexpr header_layout<topology::point_to_point, none>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

    // Specialisation 2: <topology::point_to_point, ChecksumPolicy>
    template<typename ChecksumPolicy>
    constexpr header_layout<topology::point_to_point, ChecksumPolicy>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

    // Specialisation 3: <topology::network, none>
    constexpr header_layout<topology::network, none>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

    // Specialisation 4: <topology::network, ChecksumPolicy>
    template<typename ChecksumPolicy>
    constexpr header_layout<topology::network, ChecksumPolicy>::header_layout(
        std::uint8_t b
    ) noexcept
        : _byte{b} {}

} // namespace ecomm::protocol::details

#endif // ECOMM_PROTOCOL_HEADER_LAYOUT_TPP_
