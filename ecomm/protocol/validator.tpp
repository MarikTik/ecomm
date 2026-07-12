// SPDX-License-Identifier: MIT
/**
* @file validator.tpp
*
* @brief Template definitions for ecomm::protocol::validator.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2025-07-03
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2025-07-03 Initial creation.
* - 2025-07-14 Added noexcept specifiers.
* - 2026-05-26 Rewrote for unified packet<>. Implementations left as stubs  --  fill in.
*/
#ifndef ECOMM_PROTOCOL_VALIDATOR_TPP_
#define ECOMM_PROTOCOL_VALIDATOR_TPP_

#include "validator.hpp"
#include "compute.hpp"

namespace ecomm::protocol {

    // -------------------------------------------------------------------------
    // validator<packet<PacketSize, Topology, none>>  --  no-op specialization
    // -------------------------------------------------------------------------

    template<std::size_t PacketSize, topology Topology, typename SequencePolicy>
    bool validator<packet<PacketSize, Topology, SequencePolicy, none>>::is_valid(
        [[maybe_unused]] const packet_t& packet
    ) const noexcept {
        return true; // no checksum to verify
    }

    template<std::size_t PacketSize, topology Topology, typename SequencePolicy>
    void validator<packet<PacketSize, Topology, SequencePolicy, none>>::seal(
        [[maybe_unused]] packet_t& packet
    ) const noexcept {
        // nothing to do  --  no fcs field
    }

    // -------------------------------------------------------------------------
    // validator<packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>>  --  checksum specialization
    // -------------------------------------------------------------------------

    template<std::size_t PacketSize, topology Topology, typename SequencePolicy, typename ChecksumPolicy>
    bool validator<packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>>::is_valid(
        const packet_t& packet
    ) const noexcept {
        // const_cast is safe here: the packet lives in a receive buffer that was
        // never declared const at the object level  --  the const is our own API
        // promise to the caller. We restore fcs before returning, so the packet
        // is observationally unchanged on exit.
        packet_t& mut = const_cast<packet_t&>(packet);

        const fcs_t received_fcs = mut.header.fcs;
        mut.header.fcs = {};

        const bool valid = received_fcs == compute<ChecksumPolicy>{}(
            reinterpret_cast<const std::byte*>(&mut), packet_t::packet_size);

        mut.header.fcs = received_fcs; // restore
        return valid;
    }

    template<std::size_t PacketSize, topology Topology, typename SequencePolicy, typename ChecksumPolicy>
    void validator<packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>>::seal(
        packet_t& packet
    ) const noexcept {
        packet.header.fcs = {};
        packet.header.fcs = compute<ChecksumPolicy>{}(
            reinterpret_cast<const std::byte*>(&packet), packet_t::packet_size);
    }

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_VALIDATOR_TPP_
