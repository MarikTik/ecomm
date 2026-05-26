// SPDX-License-Identifier: BSL-1.1
/**
* @file topology.hpp
*
* @brief Per-instantiation topology selector for `packet_header` and `packet`.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* A device may participate in multiple links simultaneously, each with its own
* communication shape — e.g. a UART leaf link that is strictly point-to-point and a
* WiFi link that is part of a multi-node mesh. Topology is therefore a per-instantiation
* template parameter, not a build-wide flag: the same firmware can instantiate
* `packet_header<topology::point_to_point, ...>` for the UART and
* `packet_header<topology::network, ...>` for the WiFi link.
*
* The default value of the template parameter is selected by the `ECOMM_DEFAULT_TOPOLOGY`
* config macro (see `config.hpp`).
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-25
*
* @copyright
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2026 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-25 Initial creation.
*/
#ifndef ECOMM_PROTOCOL_TOPOLOGY_HPP_
#define ECOMM_PROTOCOL_TOPOLOGY_HPP_

#include <cstdint>

#include "config.hpp"

namespace ecomm::protocol {

    /**
    * @enum topology
    *
    * @brief Communication shape selector applied per `packet_header` instantiation.
    *
    * Affects only header layout (presence of `sender_id` / `receiver_id`). Does not
    * imply anything about routing logic; routing belongs to the `hub/` layer.
    */
    enum class topology : std::uint8_t {
        point_to_point = ECOMM_TOPOLOGY_POINT_TO_POINT, ///< No sender/receiver id fields.
        network        = ECOMM_TOPOLOGY_NETWORK,        ///< Sender and receiver id fields present.
    };

    /// @brief Default topology, taken from `ECOMM_DEFAULT_TOPOLOGY` in `config.hpp`.
    inline constexpr topology default_topology = static_cast<topology>(ECOMM_DEFAULT_TOPOLOGY);

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_TOPOLOGY_HPP_
