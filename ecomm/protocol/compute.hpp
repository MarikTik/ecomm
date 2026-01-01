// SPDX-License-Identifier: BSL-1.1
/**
* @file compute.hpp
*
* @brief Checksum computation engines for ecomm protocol framework.
*
* @ingroup ecomm_protocol ecomm::protocol
* 
* This file declares the platform-independent checksum metafunctions 
* responsible for computing various checksum algorithms.
* 
* The implementations may later be specialized for specific embedded platforms
* (ESP32, ARM Cortex, etc) to leverage hardware acceleration where available.
* Otherwise, portable reference implementations will be provided.
* 
* @note These algorithms assume fixed endianness behavior defined by the protocol.
* @note Payload alignment or packing issues must be handled by caller.
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
#ifndef ECOMM_PROTOCOL_COMPUTE_HPP_
#define ECOMM_PROTOCOL_COMPUTE_HPP_
#include <cstdint>
#include "checksum.hpp"

namespace ecomm::protocol {
    /**
    * @struct compute
    * @brief Primary template (unspecialized) — intentionally undefined.
    * 
    * The compute struct template must be explicitly specialized for each supported 
    * checksum policy defined in `checksum.hpp`. It exposes a single `operator()` 
    * function that computes the checksum for the given data buffer.
    * 
    * @tparam ChecksumPolicy The checksum policy type (crc32, sum8, fletcher16, etc).
    */
    template<typename ChecksumPolicy> 
    struct compute;
    
    /**
    * @struct compute<sum8>
    * @brief Computes sum8 checksum for the given data.
    */
    template<>
    struct compute<sum8> {
        [[nodiscard]] inline sum8::value_type operator()(const std::byte* data, size_t size) const noexcept;
    };
    
    /**
    * @struct compute<sum16>
    * @brief Computes sum16 checksum for the given data.
    */
    template<>
    struct compute<sum16> {
        [[nodiscard]] inline sum16::value_type operator()(const std::byte* data, size_t size) const noexcept;
    };
    
    /**
    * @struct compute<sum32>
    * @brief Computes sum32 checksum for the given data.
    */
    template<>
    struct compute<sum32> {
        [[nodiscard]] inline sum32::value_type operator()(const std::byte* data, size_t size) const noexcept;
    };
    
    /**
    * @struct compute<crc8>
    * @brief Computes crc8 checksum for the given data.
    */
    template<>
    struct compute<crc8> {
        [[nodiscard]] inline crc8::value_type operator()(const std::byte* data, size_t size) const noexcept;
    };
    
    /**
    * @struct compute<crc16>
    * @brief Computes crc16 checksum for the given data.
    */
    template<>
    struct compute<crc16> {
        [[nodiscard]] inline crc16::value_type operator()(const std::byte* data, size_t size) const noexcept;
    };
    
    /**
    * @struct compute<crc32>
    * @brief Computes crc32 checksum for the given data.
    */
    template<>
    struct compute<crc32> {
        [[nodiscard]] inline crc32::value_type operator()(const std::byte* data, size_t size) const noexcept;
    };
    
    /**
    * @struct compute<crc64>
    * @brief Computes crc64 checksum for the given data.
    */
    template<>
    struct compute<crc64> {
        [[nodiscard]] inline crc64::value_type operator()(const std::byte* data, size_t size) const noexcept;
    };

    /**
    * @struct compute<fletcher16>
    * @brief Computes fletcher16 checksum for the given data.
    */
    template<>
    struct compute<fletcher16> {
        [[nodiscard]] inline fletcher16::value_type operator()(const std::byte* data, size_t size) const noexcept;
    };
    
    /**
    * @struct compute<fletcher32>
    * @brief Computes fletcher32 checksum for the given data.
    */
    template<>
    struct compute<fletcher32> {
        [[nodiscard]] inline fletcher32::value_type operator()(const std::byte* data, size_t size) const noexcept;
    };
    
    /**
    * @struct compute<adler32>
    * @brief Computes adler32 checksum for the given data.
    */
    template<>
    struct compute<adler32> {
        [[nodiscard]] inline adler32::value_type operator()(const std::byte* data, size_t size) const noexcept;
    };
    
    /**
    * @struct compute<internet16>
    * @brief Computes internet16 (one's complement sum) checksum.
    */
    template<>
    struct compute<internet16> {
        [[nodiscard]] inline internet16::value_type operator()(const std::byte* data, size_t size) const noexcept;
    };
    
} // namespace ecomm::protocol

#include "compute.tpp"
#endif // ECOMM_PROTOCOL_COMPUTE_HPP_
