// SPDX-License-Identifier: BSL-1.1
/**
* @file checksum.hpp
*
* @brief Checksum policy definitions for ecomm protocol framework.
* 
* @ingroup ecomm_protocol ecomm::protocol
*
* This file defines available checksum policy types that can be used within 
* framed packets in the etask communication framework. 
* 
* Each policy defines:
*  - value_type: the type of checksum result (e.g., uint8_t, uint16_t, etc.)
*  - size: compile-time constant for size of checksum field.
* 
* These policies are strictly layout-only and do not implement any actual checksum algorithms.
* Computation logic may be implemented separately as needed.
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
*/
#ifndef ECOMM_PROTOCOL_CHECKSUM_HPP_
#define ECOMM_PROTOCOL_CHECKSUM_HPP_
#include <cstddef>
#include <cstdint>

namespace ecomm::protocol
{
    
    /**
    * @struct none
    * @brief No checksum used.
    * 
    * This policy disables checksumming entirely and occupies no storage.
    * Can be used for fully trusted links or minimal data overhead.
    */
    struct none {
        using value_type = struct empty {};  ///< Placeholder type for no checksum.
        static constexpr size_t size = 0;    ///< No storage required (note that this field will be ignored in `framed_packet`).
    };
    
    // -------------------------------------------------------
    // Simple sum checksums
    // -------------------------------------------------------
    
    /**
    * @struct sum8
    * @brief 8-bit additive sum checksum.
    * 
    * Useful for extremely simple error detection on very short frames.
    */
    struct sum8 {
        using value_type = uint8_t;
        static constexpr size_t size = sizeof(value_type);
    };
    
    /**
    * @struct sum16
    * @brief 16-bit additive sum checksum.
    * 
    * Common in embedded protocols where lightweight integrity is sufficient.
    */
    struct sum16 {
        using value_type = uint16_t;
        static constexpr size_t size = sizeof(value_type);
    };
    
    /**
    * @struct sum32
    * @brief 32-bit additive sum checksum.
    * 
    * Larger additive checksum for slightly stronger detection capability.
    */
    struct sum32 {
        using value_type = uint32_t;
        static constexpr size_t size = sizeof(value_type);
    };
    
    // -------------------------------------------------------
    // CRC family (Cyclic Redundancy Check)
    // -------------------------------------------------------
    
    /**
    * @struct crc8
    * @brief 8-bit CRC checksum.
    * 
    * Provides better burst error detection than simple sums for small frames.
    */
    struct crc8 {
        using value_type = uint8_t;
        static constexpr size_t size = sizeof(value_type);
    };
    
    /**
    * @struct crc16
    * @brief 16-bit CRC checksum.
    * 
    * Very commonly used in serial protocols (e.g. MODBUS, USB-PD, CAN).
    */
    struct crc16 {
        using value_type = uint16_t;
        static constexpr size_t size = sizeof(value_type);
    };
    
    /**
    * @struct crc32
    * @brief 32-bit CRC checksum.
    * 
    * Industry standard for strong integrity detection (Ethernet, WiFi, storage, file formats).
    */
    struct crc32 {
        using value_type = uint32_t;
        static constexpr size_t size = sizeof(value_type);
    };
    
    /**
    * @struct crc64
    * @brief 64-bit CRC checksum.
    * 
    * Used in extremely high-integrity protocols or large data transfers.
    */
    struct crc64 {
        using value_type = uint64_t;
        static constexpr size_t size = sizeof(value_type);
    };
    
    // -------------------------------------------------------
    // Fletcher family
    // -------------------------------------------------------
    
    /**
    * @struct fletcher16
    * @brief 16-bit Fletcher checksum.
    * 
    * Faster than CRC for small frames while providing decent integrity.
    */
    struct fletcher16 {
        using value_type = uint16_t;
        static constexpr size_t size = sizeof(value_type);
    };
    
    /**
    * @struct fletcher32
    * @brief 32-bit Fletcher checksum.
    * 
    * Extended Fletcher checksum for stronger coverage.
    */
    struct fletcher32 {
        using value_type = uint32_t;
        static constexpr size_t size = sizeof(value_type);
    };
    
    // -------------------------------------------------------
    // Adler checksum
    // -------------------------------------------------------
    
    /**
    * @struct adler32
    * @brief 32-bit Adler checksum.
    * 
    * A modified Fletcher checksum widely used in compression libraries (e.g. zlib).
    */
    struct adler32 {
        using value_type = uint32_t;
        static constexpr size_t size = sizeof(value_type);
    };
    
    // -------------------------------------------------------
    // Internet checksum (One's complement sum)
    // -------------------------------------------------------
    
    /**
    * @struct internet16
    * @brief 16-bit Internet checksum (RFC 1071).
    * 
    * Used in IP, TCP, UDP headers and many transport protocols.
    */
    struct internet16 {
        using value_type = uint16_t;
        static constexpr size_t size = sizeof(value_type);
    };
    
}  // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_CHECKSUM_HPP_
