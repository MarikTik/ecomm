// SPDX-License-Identifier: BSL-1.1
/**
* @file basic_packet.hpp
*
* @brief Definition of the basic fixed-size packet structure for etask communication protocol.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* This file defines the minimal packet structure (`basic_packet`) used for encoding and decoding 
* messages exchanged between systems in the etask framework. 
* 
* A basic_packet consists of:
* - A compact packet_header (ecomm::packet_header) which encodes protocol metadata
* - A task identifier (user-defined underlying type)
* - A fixed-size payload region
*
* The structure ensures word-alignment for efficient transmission, parsing, and embedded processing.
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
*      - Added `noexcept` specifier to methods for better exception safety.
* - 2025-07-15 
*      - Updated to use `packet_header` instead of `header_t` following change in `packet_header.hpp`.
*      - Updated to use `header_flags` instead of `flags_t` in base packets.
*      - Updated to use `header_type` instead of `type_t` in base packets.
*      - Renamed `TaskID_UnderlyingType` to `TaskID_t` since an id type can be an enum as well.
* - 2025-07-19
*      - Fixed `ackp`, `errp`, and `hbp` constructors to remove `version` setting.
*/
#ifndef ETASK_COMM_PROTOCOL_BASIC_PACKET_HPP_
#define ETASK_COMM_PROTOCOL_BASIC_PACKET_HPP_

#include <cstdint>
#include <cstddef>
#include "packet_header.hpp"

namespace ecomm::protocol {

    #pragma pack(push, 1) // Ensure 1-byte packing for packet_header and payload alignment
    /**
    * @struct basic_packet
    * @brief Core packet structure for etask communication without checksum framing.
    * 
    * @tparam PacketSize The total size of the packet in bytes (including packet_header, task_id, and payload). Must be word-aligned.
    * @tparam TaskID_t The underlying type used for task identifiers (e.g. uint8_t, uint16_t).
    * 
    * Packet layout:
    * ```
    * +-----------------------+---------------------+-------------------------------+-----------------+
    * |      packet_header    |     status_code     |            task_id            |      payload    |
    * +-----------------------+---------------------+-------------------------------+-----------------+
    * | sizeof(packet_header) |       1 byte        |       sizeof(TaskID_t)        |   payload_size  |
    * +-----------------------+---------------------+-------------------------------+-----------------+
    * ```
    *
    * The payload size is computed automatically to fit the remaining space.
    */
    template<
        std::size_t PacketSize = 32,
        typename TaskID_t = std::uint8_t
    >
    struct basic_packet {
        /// Compile-time alignment enforcement: total packet must be word-aligned
        static_assert(PacketSize % sizeof(size_t) == 0, "Packet must be word-aligned.");
        
        /// Compile-time sanity check: packet size must fit at minimum packet_header and task ID
        static_assert(PacketSize >= sizeof(packet_header) + sizeof(TaskID_t) + 1,
        "Packet size must be at least the size of packet_header and task ID.");
        
        ///@brief Default constructor — zero-initialized packet
        basic_packet() = default; 
        
        /// @brief Compile-time constant representing the total packet size in bytes.
        static constexpr std::size_t packet_size = PacketSize;
        
        /// @brief Compile-time constant representing the payload size in bytes.
        static constexpr std::size_t payload_size = PacketSize - sizeof(packet_header) - sizeof(TaskID_t) - 1; 

        /** 
        * @brief Constructs a basic_packet with specified packet_header and task ID. 
        * @param packet_header The packet packet_header containing protocol metadata.
        * @param task_id The task identifier assigned to this packet.
        * @note The payload is automatically zero-initialized.
        */
        inline basic_packet(packet_header header, TaskID_t task_id, uint8_t status_code = 0) noexcept;
        
        /**
        * @brief Constructs a basic_packet with specified packet_header, task ID, and payload.
        * @param packet_header The packet packet_header containing protocol metadata.
        * @param task_id The task identifier assigned to this packet.
        * @param payload An array of bytes to initialize the payload.
        */
        inline basic_packet(packet_header header, TaskID_t task_id, uint8_t status_code, const std::byte *payload, size_t payload_size) noexcept;
        
        /// @brief Compact packet header containing all protocol metadata.
        packet_header header;
        
        /// @brief Status code for the packet, if applicable (e.g. error codes).
        uint8_t status_code{}; 

        /// @brief Task identifier assigned to this packet.
        TaskID_t task_id;
        
        /// @brief Payload data storage (automatically sized based on packet size).
        std::byte payload[payload_size]{};
    };
    #pragma pack(pop) // Restore previous packing alignment
    
    /// @brief Predefined acknowledgment packet with default packet_header and empty task ID.
    inline basic_packet<16> ackp {
        packet_header{
            header_type::control, // type
            false,                // encrypted
            false,                // fragmented
            0,                    // priority
            header_flags::ack,    // flags
            false                 // validated
        },
        0 // empty task ID for acknowledgment packets
    };
    
    /// @brief Predefined error packet with default packet_header and empty task ID.
    inline basic_packet<16> errp {
        packet_header{
            header_type::control, // type
            false,                // encrypted
            false,                // fragmented
            0,                    // priority
            header_flags::error,  // flags
            false                 // validated
        },
        0 // empty task ID for error packets
    };
    
    /// @brief Predefined heartbeat packet with default packet_header and empty task ID.
    inline basic_packet<16> hbp{
        packet_header{
            header_type::control,    // type
            false,                   // encrypted
            false,                   // fragmented
            0,                       // priority
            header_flags::heartbeat, // flags
            false                    // validated
        },
        0 // empty task ID for heartbeat packets
    };
    
} // namespace ecomm::protocol

#include "basic_packet.tpp"
#endif // ETASK_COMM_PROTOCOL_BASIC_PACKET_HPP_
