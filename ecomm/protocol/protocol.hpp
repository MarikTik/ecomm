// SPDX-License-Identifier: BSL-1.1
/**
* @file protocol.hpp
*
* @brief Aggregator header for the ecomm protocol layer.
*
* @defgroup ecomm_protocol ecomm::protocol
*
* Include this file to pull in every public component of `ecomm::protocol`:
* packet header, checksum policies, the unified packet type, the validator,
* checksum computation, and the error envelope.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2025-07-03
*
* @copyright
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2026 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
*
* @par Changelog
* - 2025-07-03 Initial creation.
* - 2025-07-15 Added config.hpp include for ECOMM_BOARD_ID.
* - 2026-05-25 Added error.hpp (error envelope).
* - 2026-05-26 Replaced basic_packet / framed_packet with unified packet.
* - 2026-05-27 Added header_type.hpp, header_options.hpp, node_ids.hpp,
*              header_layout.hpp (standard-layout refactor).
* - 2026-05-27 Added sequence.hpp (SequencePolicy parameter).
*/
#ifndef ECOMM_PROTOCOL_HPP_
#define ECOMM_PROTOCOL_HPP_

#include "config.hpp"
#include "topology.hpp"
#include "sequence.hpp"
#include "checksum.hpp"
#include "header_type.hpp"
#include "header_options.hpp"
#include "node_ids.hpp"
#include "header_layout.hpp"
#include "packet_header.hpp"
#include "packet.hpp"
#include "validator.hpp"
#include "compute.hpp"
#include "error.hpp"

#endif // ECOMM_PROTOCOL_HPP_
