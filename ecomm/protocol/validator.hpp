// SPDX-License-Identifier: MIT
/**
* @file validator.hpp
*
* @brief Packet validator for the ecomm communication protocol.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* Defines `validator<Packet>`, a stateless policy struct with two operations:
*
* - `is_valid(packet)`  --  checks whether a received packet is structurally sound.
*   For packets with `ChecksumPolicy == none` this is always true (no FCS to check).
*   For all other policies it recomputes the FCS over the canonical byte region and
*   compares it against `packet.header.fcs`.
*
* - `seal(packet)`  --  finalizes a packet before transmission by computing and writing
*   the FCS into `packet.header.fcs`. A no-op when `ChecksumPolicy == none`.
*
* **Checksum region** (what bytes are hashed):
*   1. Zero `packet.header.fcs` in place.
*   2. Hash all `PacketSize` bytes of the packet.
*   3. Write the result back into `packet.header.fcs`.
*
* `is_valid` follows the same zeroing step on a local copy before recomputing, so
* the comparison is deterministic regardless of what value `fcs` currently holds.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2025-07-03
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2025-07-03 Initial creation.
* - 2025-07-14 Added noexcept specifiers.
* - 2026-05-26 Removed basic_packet / framed_packet specializations.
* - 2026-05-26 Rewrote for unified packet<>: none (no-op) and general checksum
*              specializations. Sealing contract documented above.
*/
#ifndef ECOMM_PROTOCOL_VALIDATOR_HPP_
#define ECOMM_PROTOCOL_VALIDATOR_HPP_

#include "packet.hpp"

namespace ecomm::protocol {

    /**
    * @class validator
    *
    * @brief Primary validator template  --  unspecialized, intentionally incomplete.
    *
    * Instantiating this directly is a compile error. Use one of the two provided
    * partial specializations below.
    *
    * @tparam PacketType The concrete packet type to validate.
    */
    template<typename PacketType>
    struct validator;

    // -------------------------------------------------------------------------

    /**
    * @class validator<packet<PacketSize, Topology, SequencePolicy, none>>
    *
    * @brief Specialization for packets with no checksum policy.
    *
    * When `ChecksumPolicy == none` the header carries no FCS field, so there is
    * nothing to compute or verify. Both operations are no-ops provided for API
    * consistency with the checksum-carrying specialization.
    *
    * @tparam PacketSize     Total wire size of the packet in bytes.
    * @tparam Topology       Topology policy (`point_to_point` or `network`).
    * @tparam SequencePolicy Sequence policy (`no_sequence` or `sequenced`).
    */
    template<std::size_t PacketSize, topology Topology, typename SequencePolicy>
    struct validator<packet<PacketSize, Topology, SequencePolicy, none>> {

        using packet_t = packet<PacketSize, Topology, SequencePolicy, none>;

        /**
        * @brief Always returns `true`  --  no FCS to verify.
        *
        * @param[in] packet The packet to validate.
        * @return `true` unconditionally.
        */
        [[nodiscard]] bool is_valid(const packet_t& packet) const noexcept;

        /**
        * @brief No-op  --  no FCS field to write.
        *
        * @param[in,out] packet The packet to seal.
        */
        void seal(packet_t& packet) const noexcept;
    };

    // -------------------------------------------------------------------------

    /**
    * @class validator<packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>>
    *
    * @brief Specialization for packets carrying a checksum (any policy except `none`).
    *
    * **Sealing contract** (`seal`):
    *   1. Zero `packet.header.fcs`.
    *   2. Compute `ChecksumPolicy` over all `PacketSize` bytes of the packet.
    *   3. Write the result into `packet.header.fcs`.
    *
    * **Validation contract** (`is_valid`):
    *   1. Save the received `packet.header.fcs`.
    *   2. Make a local copy of the packet and zero its `header.fcs`.
    *   3. Compute `ChecksumPolicy` over all `PacketSize` bytes of the copy.
    *   4. Return `true` iff the recomputed value equals the saved value.
    *
    * @tparam PacketSize     Total wire size of the packet in bytes.
    * @tparam Topology       Topology policy (`point_to_point` or `network`).
    * @tparam SequencePolicy Sequence policy (`no_sequence` or `sequenced`).
    * @tparam ChecksumPolicy Checksum algorithm tag (any policy from `checksum.hpp`
    *                        except `none`).
    */
    template<std::size_t PacketSize, topology Topology, typename SequencePolicy, typename ChecksumPolicy>
    struct validator<packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>> {

        using packet_t = packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>;
        using fcs_t    = typename ChecksumPolicy::value_type;

        /**
        * @brief Verify the packet's FCS against a freshly recomputed value.
        *
        * Works on a local copy for the zeroing step  --  the caller's buffer is
        * never modified.
        *
        * @param[in] packet The received packet to validate.
        * @return `true` if the recomputed FCS matches `packet.header.fcs`;
        *         `false` on any mismatch (corruption, truncation, wrong policy).
        */
        [[nodiscard]] bool is_valid(const packet_t& packet) const noexcept;

        /**
        * @brief Compute and write the FCS into `packet.header.fcs`.
        *
        * Must be called on every outgoing packet before handing it to the transport.
        *
        * @param[in,out] packet The packet to seal. `packet.header.fcs` is overwritten.
        *
        * @pre  `packet.header.fcs == 0`. Calling `seal` on an already-sealed packet
        *       produces a wrong FCS because the first FCS value is non-zero during
        *       the second hash pass.
        * @post `packet.header.fcs` holds the checksum of all `PacketSize` bytes,
        *       computed with `packet.header.fcs` treated as zero.
        */
        void seal(packet_t& packet) const noexcept;
    };

} // namespace ecomm::protocol

#include "validator.tpp"
#endif // ECOMM_PROTOCOL_VALIDATOR_HPP_
