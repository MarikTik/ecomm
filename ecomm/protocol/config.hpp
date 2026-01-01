// SPDX-License-Identifier: BSL-1.1
/**
* @file config.hpp
*
* @brief Internal protocol configuration definitions for the etask communication framework.
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


#endif // ECOMM_PROTOCOL_CONFIG_HPP_