// SPDX-License-Identifier: MIT
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
* MIT License
* Copyright (c) 2025 Mark Tikhonov
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
* - 2026-05-28
*      - `ECOMM_BOARD_ID` default changed from 0 to 1. 0 is now reserved for
*        "unassigned / no sender". 255 is the broadcast address.
*/
#ifndef ECOMM_PROTOCOL_CONFIG_HPP_
#define ECOMM_PROTOCOL_CONFIG_HPP_

#define ECOMM_PROTOCOL_VERSION 0 ///< Protocol version. Currently set to 0.
static_assert(ECOMM_PROTOCOL_VERSION >= 0 and ECOMM_PROTOCOL_VERSION < 4, "ECOMM_PROTOCOL_VERSION must be in range [0, 3]");

// Address convention (network topology):
//   0        -- reserved / unassigned ("no sender").
//   1..254   -- unicast board identities. ECOMM_BOARD_ID must be in this range.
//   255      -- broadcast address; a hub or sender uses this as receiver_id to
//               address all nodes simultaneously.
#ifndef ECOMM_BOARD_ID
#define ECOMM_BOARD_ID 1 ///< Unique ID of this board. Override via compiler flag (-DECOMM_BOARD_ID=N). Valid range: 1-254. 0 = unassigned, 255 = broadcast.
#endif // ECOMM_BOARD_ID
static_assert(ECOMM_BOARD_ID >= 1 and ECOMM_BOARD_ID <= 254,
    "ECOMM_BOARD_ID must be in range [1, 254]. "
    "0 is reserved (unassigned) and 255 is the broadcast address.");

#ifndef ECOMM_DEVICE_N
#define ECOMM_DEVICE_N 2 ///< Number of unicast devices in the system (excludes the broadcast address). Default 2. Valid range: 1-254.
#endif // ECOMM_DEVICE_N
static_assert(ECOMM_DEVICE_N > 0 and ECOMM_DEVICE_N < 255,
    "ECOMM_DEVICE_N must be in range [1, 254] (255 is the broadcast address).");

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