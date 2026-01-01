// SPDX-License-Identifier: BSL-1.1
/**
* @file framed_packet.tpp
*
* @brief implementation of framed_packet.tpp methods.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2025-07-15
*
* @copyright
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2025 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
* @par Changelog
* - 2025-07-03 
*      - Initial creation.
* - 2025-07-14
*      - Added `noexcept` constructor support similar to that in `basic_packet`.
* - 2025-07-15
*      - Updated to use `packet_header` instead of `header_t` following change in `packet_header.hpp`.
*      - Renamed `TaskID_UnderlyingType` to `TaskID_t` since an id type can be an enum as well.
*/
#ifndef ECOMM_PROTOCOL_FRAMED_PACKET_TPP_
#define ECOMM_PROTOCOL_FRAMED_PACKET_TPP_
#include "framed_packet.hpp"
#include <cassert>
namespace ecomm::protocol {

    template <std::size_t PacketSize, typename TaskID_t, typename ChecksumPolicy>
    inline framed_packet<PacketSize, TaskID_t, ChecksumPolicy>::framed_packet(packet_header header_param, TaskID_t task_id_param, uint8_t status_code_param) noexcept
        : header{header_param}, status_code{status_code_param}, task_id{task_id_param}
    {
    }

    template <std::size_t PacketSize, typename TaskID_t, typename ChecksumPolicy>
    inline framed_packet<PacketSize, TaskID_t, ChecksumPolicy>::framed_packet(
        packet_header header_param, TaskID_t task_id_param, uint8_t status_code_param, const std::byte *payload_param, size_t payload_size_param) noexcept
        : header{header_param}, status_code{status_code_param}, task_id{task_id_param}
    {
        assert(payload_size_param <= payload_size && "Payload size exceeds packet capacity");
        if (payload_param && payload_size_param > 0) std::memcpy(payload, payload_param, payload_size_param);
    }

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_FRAMED_PACKET_TPP_