// SPDX-License-Identifier: MIT
/**
* @file header_options.hpp
*
* @brief `header_options` flags, `header_options_mask`, and bitwise operator opt-in.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* Defines the single-bit option flags stored in bits 4..2 of the protocol byte,
* the bitmask that spans all option positions, and the `etools::meta::enable_flags`
* specialisation that gives `header_options` its `|`, `&`, `^`, `~` operators.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-27
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-27 Extracted from packet_header.hpp.
*/
#ifndef ECOMM_PROTOCOL_HEADER_OPTIONS_HPP_
#define ECOMM_PROTOCOL_HEADER_OPTIONS_HPP_

#include <cstdint>
#include <type_traits>

#include <etools/meta/flags.hpp>

namespace ecomm::protocol {

    /**
    * @enum header_options
    *
    * @brief Independent single-bit flags that modify how a packet is interpreted.
    *
    * Each enumerator's numeric value is already shifted to its final bit position
    * within the protocol byte, so `packet_header` can OR the parameter directly
    * into storage without additional shifting.
    *
    * Opted-in to bitwise operators (`|`, `&`, `^`, `~`) via
    * `etools::meta::enable_flags` (specialisation appears after this namespace).
    *
    * Flags may be combined with `|`:
    * ```cpp
    * auto opts = header_options::error | header_options::encrypted;
    * ```
    *
    * @note These values are wire-stable. Never renumber or move an existing
    *       enumerator  --  doing so would silently break backward compatibility
    *       with any peer running an older firmware.
    *
    * @see header_options_mask for the bitmask that covers all option bits.
    * @see error.hpp for the error-envelope layout that `error` implies.
    */
    enum class header_options : std::uint8_t {
        none      = 0,        ///< No flags set. Use as a neutral constructor argument.
        error     = 1u << 4,  ///< Payload is an error envelope (see error.hpp).
        ack       = 1u << 3,  ///< Reliability acknowledgement (see reliable_channel).
        encrypted = 1u << 2,  ///< Payload is encrypted; decryption is caller's responsibility.
    };

    /**
    * @brief Bitmask covering every bit position that `header_options` may occupy.
    *
    * Spans bits 4..2 of the protocol byte: `0b0001'1100`.
    *
    * The `packet_header` constructor masks caller-supplied `opts` with this
    * constant before storing, preventing any spurious bits from corrupting
    * the type or version fields.
    */
    inline constexpr std::uint8_t header_options_mask = 0b0001'1100;

} // namespace ecomm::protocol

// Opt header_options in to the etools bitwise operators. Must be at namespace
// scope and visible wherever the operators are used.
template<>
struct etools::meta::enable_flags<ecomm::protocol::header_options> : std::true_type {};

namespace ecomm::protocol {
    // Pull the flag operators from etools::meta into ecomm::protocol so ordinary
    // unqualified lookup resolves `a | b` for header_options at call sites.
    using namespace etools::meta;
} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_HEADER_OPTIONS_HPP_
