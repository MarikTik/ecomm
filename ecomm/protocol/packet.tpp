// SPDX-License-Identifier: MIT
/**
* @file packet.tpp
*
* @brief Template definitions for ecomm::protocol::packet.
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
* - 2026-05-26 Initial creation.
*/
#ifndef ECOMM_PROTOCOL_PACKET_TPP_
#define ECOMM_PROTOCOL_PACKET_TPP_

#include "packet.hpp"

namespace ecomm::protocol {

    template<std::size_t PacketSize, topology Topology, typename SequencePolicy, typename ChecksumPolicy>
    constexpr packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>::packet(
        header_type    type,
        header_options opts
    ) noexcept
        // Delegate to packet_header's two-parameter constructor; payload is
        // zero-initialized by the member initializer on the declaration.
        : header{type, opts}
    {}

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_PACKET_TPP_
