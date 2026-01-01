// SPDX-License-Identifier: BSL-1.1
/**
* @file validator.hpp
*
* @brief Packet validator system for etask communication protocol framework.
*
* @ingroup etask_comm_protocol etask::comm::protocol
*
* This file defines the core validator mechanism that allows compile-time specialization
* of packet validation logic for different packet types within the protocol stack.
*
* The validator system supports:
* - Both basic_packet (without checksums) and framed_packet (with checksums).
* - Policy-based checksum computation via compute<> metafunction.
* - Separation of validation and sealing operations.
*
* @note 
* The validator system is fully extensible to support future packet types
* without modifying existing code.
*
* Platform-specific optimizations may be added to checksum calculations inside compute.hpp.
* This system guarantees cross-platform compatibility for protocol logic itself.
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
#ifndef ETASK_PROTOCOL_VALIDATOR_HPP_
#define ETASK_PROTOCOL_VALIDATOR_HPP_
#include "basic_packet.hpp"
#include "framed_packet.hpp"

namespace etask::comm::protocol {
    
    /**
    * @class validator
    * @brief Primary validator template (unspecialized).
    *
    * This primary template serves as a generic interface for packet validation.
    * Specializations for specific packet types provide the actual logic.
    *
    * @tparam PacketType Type of packet to validate.
    */
    template<typename PacketType>
    struct validator;
    
    
    /**
    * @class validator<basic_packet<>>
    * @brief Specialization for validating basic packets (no checksum).
    *
    * Since basic_packet has no checksum, validation always returns true.
    *
    * @tparam PacketSize Total packet size.
    * @tparam TaskID_UnderlyingType Underlying type used for task identifiers.
    */
    template<std::size_t PacketSize, typename TaskID_UnderlyingType>
    struct validator<basic_packet<PacketSize, TaskID_UnderlyingType>> {
        using packet_t = basic_packet<PacketSize, TaskID_UnderlyingType>;
        
        /**
        * @brief Validate a basic packet.
        * 
        * Always returns true as no checksum validation is required.
        * 
        * @param packet The basic_packet instance to validate.
        * @return Always true.
        */
        [[nodiscard]] inline bool is_valid(const packet_t &packet) const noexcept;
        
        /**
        * @brief Seal (finalize) a basic packet before transmission.
        * 
        * No action required for basic_packet; provided for API consistency.
        * 
        * @param packet The basic_packet instance to seal.
        */
        inline void seal(packet_t& packet) const noexcept;
    };


    /**
    * @class validator<framed_packet<>>
    * @brief Specialization for validating framed packets (with checksum support).
    *
    * Computes checksum dynamically using the checksum policy provided at compile time.
    *
    * @tparam PacketSize Total packet size.
    * @tparam TaskID_UnderlyingType Underlying type used for task identifiers.
    * @tparam ChecksumPolicy Checksum algorithm policy type (must match compute<> specialization).
    */
    template<std::size_t PacketSize, typename TaskID_UnderlyingType, typename ChecksumPolicy>
    struct validator<framed_packet<PacketSize, TaskID_UnderlyingType, ChecksumPolicy>> {
        using packet_t = framed_packet<PacketSize, TaskID_UnderlyingType, ChecksumPolicy>;
        
        /**
        * @brief Validate a framed packet by recomputing its checksum.
        *
        * @param packet The framed_packet instance to validate.
        * @return true if checksum matches, false otherwise.
        */
        [[nodiscard]] inline bool is_valid(const packet_t& packet) const noexcept;
        
        /**
        * @brief Seal (finalize) a framed packet before transmission by writing checksum.
        *
        * @param packet The framed_packet instance to seal.
        */
        inline void seal(packet_t& packet) const noexcept;
    };
    
} // namespace etask::comm::protocol

#include "validator.tpp"
#endif // ETASK_PROTOCOL_VALIDATOR_HPP_