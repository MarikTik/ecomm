// SPDX-License-Identifier: BSL-1.1
/**
* @file packet.hpp
*
* @brief Fixed-size protocol packet for the ecomm communication library.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* A `packet` is the fundamental unit of data on the wire: a `packet_header` followed
* immediately by a raw payload region. Nothing else. Application-layer concepts such as
* handler ids, task ids, and status codes are not part of the packet  --  they live in the
* first bytes of the payload and are interpreted by the layer above.
*
* Wire layout:
* ```
* +----------------------------------------------+-------------------------------+
* |              packet_header                   |    payload (PacketSize        |
* |  [proto(1B)] [seq(*s)] [ids(*n)] [fcs($)]    |       - sizeof(header) bytes) |
* +----------------------------------------------+-------------------------------+
* (*s) seq_num present only when SequencePolicy == sequenced (1 byte)
* (*n) sender_id + receiver_id present only when Topology == network (2 bytes)
* ($)  fcs field present only when ChecksumPolicy != none (ChecksumPolicy::size bytes)
* ```
*
* The `packet_header` is templated on the same `<Topology, SequencePolicy,
* ChecksumPolicy>` policies, so `sizeof(header)`  --  and therefore `payload_size`  --
* is fully known at compile time.
*
* Three compile-time policies parameterize the layout (identical to `packet_header`):
* - `Topology`        --  `point_to_point` or `network`; drives whether `sender_id` /
*                      `receiver_id` occupy bytes in the header.
* - `SequencePolicy`  --  sequence tag from `sequence.hpp`; `sequenced` adds a one-byte
*                      `seq_num` immediately after the protocol byte. Use `no_sequence`
*                      for unreliable channels.
* - `ChecksumPolicy`  --  checksum algorithm tag from `checksum.hpp`; drives the width of
*                      the FCS field inside the header. Use `none` for no checksum.
*
* @note `PacketSize` must be strictly greater than `sizeof(packet_header<Topology,
*       SequencePolicy, ChecksumPolicy>)` so that at least one payload byte exists.
*       A `static_assert` enforces this at instantiation.
*
* @note Word-alignment of `PacketSize` is enforced via `static_assert`. On the
*       Arduino/ESP target, DMA and serial drivers typically require word-aligned buffers;
*       a misaligned `PacketSize` is always a mistake.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-26
*
* @copyright
* Business Source License 1.1 (BSL 1.1)
* Copyright (c) 2026 Mark Tikhonov
* Free for non-commercial use. Commercial use requires a separate license.
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-26 Initial creation. Replaces basic_packet and framed_packet.
*/
#ifndef ECOMM_PROTOCOL_PACKET_HPP_
#define ECOMM_PROTOCOL_PACKET_HPP_

#include <cstddef>
#include <cstdint>

#include "packet_header.hpp"

namespace ecomm::protocol {

    #pragma pack(push, 1)
    /**
    * @class packet
    *
    * @brief Fixed-size wire packet: a typed header followed by a raw payload.
    *
    * `packet` is a data-only POD aggregate. It owns no heap memory; every field is
    * a plain value or array member. The compiler sees its complete layout at
    * instantiation, making it suitable for direct `memcpy` to/from serial or network
    * buffers.
    *
    * @tparam PacketSize     Total size of the packet in bytes, header included.
    *                        Must be word-aligned (`PacketSize % sizeof(std::size_t) == 0`)
    *                        and large enough to hold the header plus at least one payload
    *                        byte (`PacketSize > sizeof(header_t)`).
    *                        Defaults to 32.
    * @tparam Topology       Wire shape passed through to `packet_header`. Selects whether
    *                        `sender_id` / `receiver_id` bytes are present in the header.
    *                        Defaults to `default_topology` (from `config.hpp`).
    * @tparam SequencePolicy Sequence tag passed through to `packet_header`. `sequenced`
    *                        adds a one-byte `seq_num` field after the protocol byte;
    *                        `no_sequence` adds nothing. Defaults to `no_sequence`.
    * @tparam ChecksumPolicy Checksum algorithm tag passed through to `packet_header`.
    *                        Controls the FCS field width inside the header. Use `none`
    *                        for no checksum.  Defaults to `none`.
    */
    template<
        std::size_t  PacketSize     = 32,
        topology     Topology       = default_topology,
        typename     SequencePolicy = no_sequence,
        typename     ChecksumPolicy = none
    >
    struct packet {

        // -----------------------------------------------------------------
        // Type aliases
        // -----------------------------------------------------------------

        /// @brief The concrete `packet_header` type for this packet instantiation.
        using header_t = packet_header<Topology, SequencePolicy, ChecksumPolicy>;

        // -----------------------------------------------------------------
        // Compile-time constants
        // -----------------------------------------------------------------

        /// @brief Total size of this packet on the wire, in bytes. Equals `PacketSize`.
        static constexpr std::size_t packet_size = PacketSize;

        /// @brief Number of bytes in the payload region.
        ///
        /// Computed as `PacketSize - sizeof(header_t)`. This is exact: the header
        /// size already accounts for topology-conditional id bytes and
        /// checksum-conditional FCS bytes via the EBO base classes of `packet_header`.
        static constexpr std::size_t payload_size = PacketSize - sizeof(header_t);

        // -----------------------------------------------------------------
        // Compile-time invariants
        // -----------------------------------------------------------------

        static_assert(
            PacketSize % sizeof(std::size_t) == 0,
            "packet: PacketSize must be word-aligned "
            "(PacketSize % sizeof(std::size_t) == 0). "
            "Choose the next larger multiple of sizeof(std::size_t)."
        );
        static_assert(
            PacketSize > sizeof(header_t),
            "packet: PacketSize must be strictly greater than sizeof(packet_header) "
            "so that at least one payload byte exists. "
            "Increase PacketSize or choose a smaller header configuration "
            "(fewer fields: no SequencePolicy, no ChecksumPolicy, or no node ids)."
        );

        // -----------------------------------------------------------------
        // Constructors
        // -----------------------------------------------------------------

        /**
        * @brief Default constructor. Zero-initializes the entire packet.
        *
        * `header` is default-constructed (protocol byte = 0x00, version bits = 0,
        * no options, type = data). `payload` is zero-filled.
        *
        * @post `sizeof(*this) == PacketSize`. All bytes are zero.
        */
        constexpr packet() noexcept = default;

        /**
        * @brief Construct a packet with a given header type and option flags.
        *
        * Delegates header construction to
        * `packet_header(header_type, header_options)`, which packs type, options,
        * and `ECOMM_PROTOCOL_VERSION` into the protocol byte. The payload is
        * zero-initialized.
        *
        * @param[in] type  Top-level packet classification (see `header_type`).
        * @param[in] opts  OR-combined `header_options` flags; pass
        *                  `header_options::none` when no flags are needed.
        *
        * @post `header.type() == type`. `header.has(opts)` is `true`.
        *       `header.version() == ECOMM_PROTOCOL_VERSION`. All `payload` bytes are 0.
        */
        constexpr packet(header_type type, header_options opts) noexcept;

        // -----------------------------------------------------------------
        // Data members (wire order)
        // -----------------------------------------------------------------

        /// @brief Protocol header. Occupies the first `sizeof(header_t)` bytes on the wire.
        header_t header{};

        /// @brief Raw payload bytes. Interpreted entirely by the layer above.
        ///
        /// The application may overlay any structure on this region (handler id,
        /// task id, error envelope, firmware chunk, ...) by reading/writing through
        /// a pointer cast. The packet itself imposes no schema.
        std::byte payload[payload_size]{};
    };
    #pragma pack(pop)

} // namespace ecomm::protocol

#include "packet.tpp"
#endif // ECOMM_PROTOCOL_PACKET_HPP_
