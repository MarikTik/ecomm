// SPDX-License-Identifier: BSL-1.1
/**
* @file packet_header.inl
*
* @brief implementation of packet_header methods.
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
*      - Renamed `header_t` to `packet_header` for clarity.
*      - Renamed `flags_t` to `header_flags` for clarity.
*      - Added enum `header_type` to properly represent packet types. 
* - 2025-07-16
*      - Included a `receiver_id` byte field for devices to know intended packet recepient.
* - 2025-07-18
*      - Privatized `sender_id` field by setting it to default value of `ECOMM_BOARD_ID` to ensure consistent sender ID usage across the protocol.
* - 2025-07-19
*      - Locked `version` subfield in `_space` to be always set to `ECOMM_PROTOCOL_VERSION` to ensure protocol version consistency.
*/
#ifndef ECOMM_PROTOCOL_PACKET_HEADER_INL_
#define ECOMM_PROTOCOL_PACKET_HEADER_INL_
#include "packet_header.hpp"

namespace ecomm::protocol{

    inline packet_header::packet_header(
        uint16_t raw_value,
        uint8_t receiver_id
    ) noexcept
        : _space{
                static_cast<uint16_t>(
                (raw_value & ~(0x3 << 11)) |
                ((ECOMM_PROTOCOL_VERSION & 0x3) << 11)
            )
        },
        receiver_id{receiver_id}
    {
    }

    inline packet_header::packet_header(
        header_type type,
        bool encrypted,
        bool fragmented,
        uint8_t priority,
        header_flags flags,
        bool validated,
        bool reserved,
        uint8_t receiver_id
    ) noexcept
        : _space{static_cast<uint16_t>(
              (static_cast<uint16_t>(static_cast<uint8_t>(type) & 0xF) << 12) |
               (static_cast<uint16_t>(ECOMM_PROTOCOL_VERSION & 0x3) << 10) | 
              ((static_cast<uint16_t>(encrypted) & 0x1) << 9) |
              ((static_cast<uint16_t>(fragmented) & 0x1) << 8) |
              (static_cast<uint16_t>(priority & 0x7) << 5) |
              (static_cast<uint16_t>(static_cast<uint8_t>(flags) & 0x7) << 2) |
              ((static_cast<uint16_t>(validated) & 0x1) << 1) |
              ((static_cast<uint16_t>(reserved) & 0x1)))},
          receiver_id{receiver_id}
    {
    }
    
    inline header_type packet_header::type() const noexcept{
        return static_cast<header_type>((_space >> 12) & 0xF);
    }
    
    inline uint8_t packet_header::version() const noexcept{
        return (_space >> 10) & 0x3; 
    }
    
    inline bool packet_header::encrypted() const noexcept{
        return (_space & 0x0200) != 0; 
    }
    
    inline bool packet_header::fragmented() const noexcept{ 
        return (_space & 0x0100) != 0; 
    }
    
    inline uint8_t packet_header::priority() const noexcept{ 
        return (_space >> 5) & 0x7; 
    }
    
    inline header_flags packet_header::flags() const noexcept {
        return static_cast<header_flags>((_space >> 2) & 0x7);
    }
    
    inline bool packet_header::validated() const noexcept
    {
        return (_space & 0x0002) != 0;
    }
    
    inline bool packet_header::reserved() const noexcept
    {
        return (_space & 0x0001) != 0;
    }
    inline uint8_t packet_header::sender_id() const noexcept
    {
        return _sender_id;
    }
}

#endif // ECOMM_PROTOCOL_PACKET_HEADER_INL_