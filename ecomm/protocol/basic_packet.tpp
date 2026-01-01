// SPDX-License-Identifier: BSL-1.1
/**
* @file basic_packet.tpp
*
* @brief implementation of basic_packet.tpp methods.
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
*/
#ifndef ECOMM_PROTOCOL_BASIC_PACKET_TPP_
#define ECOMM_PROTOCOL_BASIC_PACKET_TPP_
#include "basic_packet.hpp"
#include <cassert>
#include <cstring>

namespace ecomm::protocol {

    template <std::size_t PacketSize, typename TaskID_t>
    inline basic_packet<PacketSize, TaskID_t>::basic_packet(packet_header header_param, TaskID_t task_id_param, uint8_t status_code_param) noexcept
        : header{header_param}, status_code{status_code_param}, task_id{task_id_param}
    {
    }
    
    template <std::size_t PacketSize, typename TaskID_t>
    inline basic_packet<PacketSize, TaskID_t>::basic_packet(packet_header header_param, TaskID_t task_id_param, uint8_t status_code_param, const std::byte *payload_param, size_t payload_size_param) noexcept
        : basic_packet(header_param, task_id_param, status_code_param)
    {
        assert(payload_size_param <= payload_size && "Payload size exceeds packet capacity");
        if (payload_param and payload_size_param > 0) std::memcpy(payload, payload_param, payload_size_param);
    }
} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_BASIC_PACKET_TPP_