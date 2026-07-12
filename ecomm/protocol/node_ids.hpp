// SPDX-License-Identifier: MIT
/**
* @file node_ids.hpp
*
* @brief `node_ids`  --  sender and receiver node identifiers for network topology.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* Defines `details::node_ids`, the two-byte struct that is embedded in
* `packet_header` when `Topology == topology::network`. Also defines
* `details::ids_storage_t`, the alias that selects `node_ids` or an empty
* placeholder depending on the topology policy.
*
* @note Renamed from `network_ids` to `node_ids` to better reflect that these
*       are identifiers for individual nodes, not a description of the network
*       itself. The field names `sender_id` / `receiver_id` are unchanged.
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
* - 2026-05-27 Extracted from packet_header.hpp; renamed network_ids -> node_ids.
*/
#ifndef ECOMM_PROTOCOL_NODE_IDS_HPP_
#define ECOMM_PROTOCOL_NODE_IDS_HPP_

#include <cstdint>
#include <type_traits>

#include "config.hpp"
#include "topology.hpp"

namespace ecomm::protocol::details {

    /**
    * @struct node_ids
    *
    * @brief Wire storage for the sender and receiver node identifiers.
    *
    * Present in `packet_header` only when `Topology == topology::network`.
    * Both fields are one byte wide, matching the `ECOMM_BOARD_ID` granularity
    * (up to 256 distinct nodes on one network segment).
    *
    * @note `sender_id` is default-initialised to `ECOMM_BOARD_ID` (the local
    *       node identifier defined in `config.hpp`). `receiver_id` is
    *       zero-initialised and must be written explicitly before transmission.
    */
    struct node_ids {
        std::uint8_t sender_id{ECOMM_BOARD_ID}; ///< Identity of the sending node.
        std::uint8_t receiver_id{};              ///< Identity of the intended recipient.
    };

    /**
    * @brief Empty placeholder used when no optional field is needed.
    *
    * Has no data members, so `packet_layout` can inherit from it without
    * contributing bytes to the wire layout.
    */
    struct empty_field {};

    /**
    * @brief Select the id-storage type based on the topology policy.
    *
    * Resolves to `node_ids` (2 bytes on the wire) when `Topology` is
    * `topology::network`, and to `empty_field` (zero bytes) for every
    * other topology.
    *
    * @tparam Topology The topology policy tag from `topology.hpp`.
    */
    template<topology Topology>
    using ids_storage_t =
        std::conditional_t<Topology == topology::network, node_ids, empty_field>;

} // namespace ecomm::protocol::details

#endif // ECOMM_PROTOCOL_NODE_IDS_HPP_
