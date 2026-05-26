// SPDX-License-Identifier: BSL-1.1
/**
* @file config.hpp
*
* @brief Internal protocol configuration definitions for the ecomm communication framework.
*
* This header provides internal compile-time configuration options used by the protocol layer.
* Users are not expected to modify protocol metadata directly in code. Instead, static definitions
* like the unique board/device ID (`ECOMM_BOARD_ID`) can be overridden via compiler flags or 
* global macro definitions.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2025-07-18
*
* @copyright
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2025 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
* @par Changelog
* - 2025-07-18
*      - Initial creation.
* - 2025-07-19
*      - Added `ECOMM_DEVICE_N` to specifiy the number of devices in the system.
*      - Ensured `ECOMM_DEVICE_N` is in range [1, 255] via `static_assert`.
*      - Added `ECOMM_PROTOCOL_VERSION` to specify the protocol version.
*      - Ensured `ECOMM_PROTOCOL_VERSION` is in range [0, 3] via `static_assert`.
* - 2026-05-25
*      - Added `ECOMM_MAX_ERROR_MESSAGE_LENGTH` to cap the size of the human-readable
*        message in an error envelope. Default is 65535 (uint16_t max).
*      - Added `ECOMM_DEFAULT_TOPOLOGY` to select the default network topology used as
*        the template default for `packet_header` and `packet`. Default is point-to-point.
*/
#ifndef ECOMM_PROTOCOL_CONFIG_HPP_
#define ECOMM_PROTOCOL_CONFIG_HPP_

#define ECOMM_PROTOCOL_VERSION 0 ///< Protocol Verson. Currently set to 0.
static_assert(ECOMM_PROTOCOL_VERSION >= 0 and ECOMM_PROTOCOL_VERSION < 4, "ECOMM_PROTOCOL_VERSION must be in range [0, 3]");

#ifndef ECOMM_BOARD_ID
#define ECOMM_BOARD_ID 0 ///< Default board ID, can be overridden by customizing the value before header inclusion.
#endif // ECOMM_BOARD_ID

#ifndef ECOMM_DEVICE_N
#define ECOMM_DEVICE_N 2 ///< Default number of devices in the system (set to 2), can be overridden by customizing the value before header inclusion.
#endif // ECOMM_DEVICE_N
static_assert(ECOMM_DEVICE_N > 0 and ECOMM_DEVICE_N < 256, "ECOMM_DEVICE_N must be in range [1, 255]");

#ifndef ECOMM_MAX_ERROR_MESSAGE_LENGTH
#define ECOMM_MAX_ERROR_MESSAGE_LENGTH 65535 ///< Maximum length of an error-envelope message in bytes. Defaults to 65535 (equivalent to std::numeric_limits<uint16_t>::max()). Override before including ecomm headers to shrink the on-wire length field (e.g. setting it to 255 makes the length field a single uint8_t).
#endif // ECOMM_MAX_ERROR_MESSAGE_LENGTH

// Layer 1 guard: validate the macro value itself at preprocess time.
// Layer 2 guard (vs. per-packet capacity) lives in error.hpp at the template
// instantiation point, since it depends on user-chosen PacketSize.
#if ECOMM_MAX_ERROR_MESSAGE_LENGTH <= 0
#  error "ECOMM_MAX_ERROR_MESSAGE_LENGTH must be strictly positive."
#endif
#if ECOMM_MAX_ERROR_MESSAGE_LENGTH > 4294967295
#  error "ECOMM_MAX_ERROR_MESSAGE_LENGTH must fit in a uint32_t (max 4294967295); reduce the value or extend smallest_uint_t coverage."
#endif

// Topology selector values. These are stable wire/api constants; do not renumber.
// Higher-level code translates these into the `ecomm::protocol::topology` enum value.
#define ECOMM_TOPOLOGY_POINT_TO_POINT 0 ///< No sender/receiver id fields in the header. Saves 2 bytes per packet.
#define ECOMM_TOPOLOGY_NETWORK        1 ///< Sender and receiver id fields present in the header.

#ifndef ECOMM_DEFAULT_TOPOLOGY
#define ECOMM_DEFAULT_TOPOLOGY ECOMM_TOPOLOGY_POINT_TO_POINT ///< Default topology used as the template default for `packet_header` and `packet`. Per-instantiation override is supported.
#endif // ECOMM_DEFAULT_TOPOLOGY

#if ECOMM_DEFAULT_TOPOLOGY != ECOMM_TOPOLOGY_POINT_TO_POINT && \
    ECOMM_DEFAULT_TOPOLOGY != ECOMM_TOPOLOGY_NETWORK
#  error "ECOMM_DEFAULT_TOPOLOGY must be ECOMM_TOPOLOGY_POINT_TO_POINT or ECOMM_TOPOLOGY_NETWORK."
#endif


#endif // ECOMM_PROTOCOL_CONFIG_HPP_