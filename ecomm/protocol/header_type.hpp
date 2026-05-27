// SPDX-License-Identifier: BSL-1.1
/**
* @file header_type.hpp
*
* @brief `header_type` enumeration — top-level packet classification.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* Defines the 3-bit type field stored in bits 7..5 of the protocol byte.
* Six values are currently assigned; encodings 0x6 and 0x7 are reserved.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-27
*
* @copyright
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2026 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-27 Extracted from packet_header.hpp.
*/
#ifndef ECOMM_PROTOCOL_HEADER_TYPE_HPP_
#define ECOMM_PROTOCOL_HEADER_TYPE_HPP_

#include <cstdint>

namespace ecomm::protocol {

    /**
    * @enum header_type
    *
    * @brief Top-level classification of what a packet carries.
    *
    * Stored in bits 7..5 of the protocol byte (3 bits). Six values are defined;
    * encodings `0x6` and `0x7` are reserved for future packet kinds and must not
    * appear on the wire until assigned.
    *
    * @note These values are wire-stable. Never renumber an existing enumerator —
    *       doing so would silently break backward compatibility with any peer
    *       running an older firmware.
    */
    enum class header_type : std::uint8_t {
        data     = 0x0, ///< Generic application data. Most packets use this type.
        control  = 0x1, ///< Protocol-level commands (reset, sync, configuration).
        auth     = 0x2, ///< Authentication or credential exchange.
        session  = 0x3, ///< Session lifecycle: initiation, teardown, handshake.
        log      = 0x4, ///< Diagnostic log messages or telemetry.
        firmware = 0x5, ///< Firmware image chunks or update-related payloads.
    };

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_HEADER_TYPE_HPP_
