// SPDX-License-Identifier: BSL-1.1
/**
* @file validator.tpp
*
* @brief implementation of validator.tpp methods.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2025-07-03
*
* @copyright
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2025 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
*
* @par Changelog
* - 2025-07-03 Initial creation.
* - 2025-07-14 Added `noexcept` specifier to methods for better exception safety.
*/
#ifndef ECOMM_PROTOCOL_VALIDATOR_TPP_
#define ECOMM_PROTOCOL_VALIDATOR_TPP_
#include "validator.hpp"
#include "compute.hpp"

namespace ecomm::protocol {

    template <std::size_t PacketSize, typename TaskID_UnderlyingType>
    inline bool validator<basic_packet<PacketSize, TaskID_UnderlyingType>>::is_valid([[maybe_unused]] const packet_t &packet) const noexcept
    {
        return true; // Basic packet validation always returns true, as it has no checksum
    }
    template <std::size_t PacketSize, typename TaskID_UnderlyingType>
    inline void validator<basic_packet<PacketSize, TaskID_UnderlyingType>>::seal([[maybe_unused]] packet_t &packet) const noexcept
    {
        // No sealing required for basic packets, as they do not have a checksum
        // This function is provided for interface consistency
    }


    template <std::size_t PacketSize, typename TaskID_UnderlyingType, typename ChecksumPolicy>
    inline void validator<framed_packet<PacketSize, TaskID_UnderlyingType, ChecksumPolicy>>::seal(packet_t &packet) const noexcept
    {
        // Compute the checksum for the header, task_id, and payload
        packet.fcs = compute<ChecksumPolicy>(
            reinterpret_cast<const std::byte*>(&packet),
            packet.packet_size - ChecksumPolicy::size
        );
    }

    template <std::size_t PacketSize, typename TaskID_UnderlyingType, typename ChecksumPolicy>
    inline bool validator<framed_packet<PacketSize, TaskID_UnderlyingType, ChecksumPolicy>>::is_valid(const packet_t &packet) const noexcept
    {
        // Compute the expected checksum for the header, task_id, and payload and compare with the stored fcs
        return packet.fcs == compute<ChecksumPolicy>(
            reinterpret_cast<const std::byte*>(&packet),
            packet.packet_size - ChecksumPolicy::size
        );
    }

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_VALIDATOR_TPP_