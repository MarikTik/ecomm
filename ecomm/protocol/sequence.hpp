// SPDX-License-Identifier: MIT
/**
* @file sequence.hpp
*
* @brief Sequence-number policy tags for `packet_header` and `packet`.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* A sequence policy is a pure compile-time tag that controls whether
* `packet_header` carries a one-byte sequence number field (`seq_num`)
* immediately after the protocol byte.  The field is used by
* `reliable_channel` to match outbound packets with their acknowledgements;
* it is absent when the channel layer does not need reliability.
*
* Two policies are defined:
*
* | Policy        | seq_num field | Wire bytes added |
* |---------------|---------------|-----------------|
* | `no_sequence` | absent        | 0               |
* | `sequenced`   | present       | 1               |
*
* Like `ChecksumPolicy`, the sequence policy is a template parameter of
* `packet_header` and `packet`.  The default is `no_sequence` so that
* existing instantiations are unaffected.
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
* - 2026-05-27 Initial creation.
*/
#ifndef ECOMM_PROTOCOL_SEQUENCE_HPP_
#define ECOMM_PROTOCOL_SEQUENCE_HPP_

#include <cstddef>
#include <cstdint>

namespace ecomm::protocol {

    /**
    * @struct no_sequence
    *
    * @brief Sequence policy tag -- no sequence number field.
    *
    * Selecting this policy (the default) leaves the wire layout unchanged
    * relative to the pre-sequence era.  `reliable_channel` requires
    * `sequenced` instead.
    */
    struct no_sequence {
        /// @brief Number of bytes this policy adds to the header. Always 0.
        static constexpr std::size_t size = 0;
    };

    /**
    * @struct sequenced
    *
    * @brief Sequence policy tag -- adds a one-byte sequence number.
    *
    * When selected, `packet_header` gains a `seq_num` field of type
    * `std::uint8_t` placed immediately after the protocol byte (`_byte`)
    * and before any node id or FCS fields.  The field wraps at 255.
    *
    * `reliable_channel` static-asserts that its packet type uses this policy.
    */
    struct sequenced {
        /// @brief Type of the sequence number field.
        using value_type = std::uint8_t;

        /// @brief Number of bytes this policy adds to the header. Always 1.
        static constexpr std::size_t size = 1;
    };

} // namespace ecomm::protocol

#endif // ECOMM_PROTOCOL_SEQUENCE_HPP_
