// SPDX-License-Identifier: BSL-1.1
/**
* @file framed_packet.hpp
*
* @brief Definition of the framed packet structure for etask communication protocol with checksum support.
* 
* @ingroup ecomm_protocol ecomm::protocol
*
* This file defines the `framed_packet` structure, which extends the basic packet format by 
* including an optional checksum field (FCS - Frame Check Sequence). The checksum type is fully 
* configurable via policy-based template parameterization.
* 
* The framed_packet ensures:
* - Word-aligned total packet size.
* - Precise memory layout suitable for embedded serialization.
* - Flexible integration of multiple checksum algorithms via ChecksumPolicy.
* 
* The structure is designed for safe transmission over unreliable links 
* such as serial, radio, or low-level networking layers.
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
* - 2025-07-03 
*      - Initial creation.
* - 2025-07-14 
*      - Added `noexcept` constructor support similar to that in `basic_packet`.
* - 2025-07-15
*      - Updated to use `packet_header` instead of `header_t` following change in `packet_header.hpp`.
*      - Renamed `TaskID_UnderlyingType` to `TaskID_t` since an id type can be an enum as well.
*/
#ifndef ECOMM_PROTOCOL_FRAMED_PACKET_HPP_
#define ECOMM_PROTOCOL_FRAMED_PACKET_HPP_

#include <cstddef>
#include <cstdint>
#include "packet_header.hpp"
#include "checksum.hpp"

namespace ecomm::protocol {

    #pragma pack(push, 1) // Ensure 1-byte packing for header and payload alignment
    /**
    * @struct framed_packet
    * @brief Full protocol packet structure with checksum protection.
    * 
    * The framed_packet extends the basic_packet by adding a final checksum field,
    * which provides data integrity verification for the header, task_id, and payload.
    * 
    * @tparam PacketSize The total size of the packet in bytes (including header, task ID, payload, and checksum). Must be word-aligned.
    * @tparam TaskID_t The underlying type used for task identifiers (e.g. uint8_t, uint16_t).
    * @tparam ChecksumPolicy The checksum algorithm to use (e.g. checksum::crc32, checksum::sum16, checksum::none).
    * 
    * Packet layout:
    * ```
    * +-----------------------+-------------------------+-----------------------------------+--------------+----------------------+
    * |      header           |       status_code       |           task_id                 |    payload   |    checksum (FCS)    |
    * +-----------------------+-------------------------+-----------------------------------+--------------+----------------------+         
    * |   sizeof(header)      |            1            |          sizeof(TaskID_t)         | payload_size | ChecksumPolicy::size |
    * +-----------------------+-------------------------+-----------------------------------+--------------+----------------------+
    * ```
    */
    template<
        std::size_t PacketSize = 32,
        typename TaskID_t = std::uint8_t,
        typename ChecksumPolicy = protocol::crc32
    >
    struct framed_packet {
        /// Compile-time alignment enforcement: total packet must be word-aligned
        static_assert(PacketSize % sizeof(size_t) == 0, "Packet must be word-aligned.");
        
        /// Compile-time sanity check: packet size must fit at minimum header, task ID, and checksum
        static_assert(PacketSize >= sizeof(packet_header) + sizeof(TaskID_t) + ChecksumPolicy::size + 1,
        "Packet size must be at least the size of header, status_code, task ID, and checksum.");
        
        /// @brief Compile-time constant representing the total packet size in bytes.
        static constexpr std::size_t packet_size = PacketSize; 

        /// @brief Compile-time constant representing the payload size in bytes.
        static constexpr std::size_t payload_size = PacketSize - sizeof(packet_header) - sizeof(TaskID_t) - ChecksumPolicy::size - 1;
        
        /// @brief Type alias for the checksum value type defined by the ChecksumPolicy.
        using checksum_policy_t = ChecksumPolicy; 
        
        /** 
        * @brief Constructs a basic_packet with specified header and task ID. 
        * @param header The packet header containing protocol metadata.
        * @param task_id The task identifier assigned to this packet.
        * @note The payload is automatically zero-initialized.
        */
        inline framed_packet(packet_header header, TaskID_t task_id, uint8_t status_code = 0) noexcept;
        
        /**
        * @brief Constructs a basic_packet with specified header, task ID, and payload.
        * @param header The packet header containing protocol metadata.
        * @param task_id The task identifier assigned to this packet.
        * @param payload An array of bytes to initialize the payload.
        */
        inline framed_packet(packet_header header, TaskID_t task_id, uint8_t status_code, const std::byte *payload, size_t payload_size) noexcept;

        /// @brief Compact packet header containing all protocol metadata.
        packet_header header;

        /// @brief Status code for the packet, if applicable (e.g. error codes).
        uint8_t status_code{}; 
        
        /// @brief Task identifier assigned to this packet.
        TaskID_t task_id;
        
        /// @brief Payload data storage (automatically sized based on packet size and checksum).
        std::byte payload[payload_size]{};
        
        /// @brief Frame Check Sequence field for checksum integrity protection.
        typename ChecksumPolicy::value_type fcs;
    };
    #pragma pack(pop) // Restore previous packing alignment

} // namespace ecomm::protocol

#include "framed_packet.tpp"
#endif // ECOMM_PROTOCOL_FRAMED_PACKET_HPP_
