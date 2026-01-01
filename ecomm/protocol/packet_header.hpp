// SPDX-License-Identifier: BSL-1.1
/**
* @file packet_header.hpp
*
* @brief Immutable packet packet_header definition for etask communication protocol.
*
* @ingroup ecomm_protocol ecomm::protocol
*
* The packet packet_header occupies 24 bits and encodes metadata 
* required for packet routing, processing, and control.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2025-07-03
*
* @date 2025-07-14 Added `noexcept` specifier to methods for better exception safety.
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
*      - Removed default constructor to ensure all fields are properly initialized.
*/
#ifndef ECOMM_PROTOCOL_PACKET_HEADER_HPP_
#define ECOMM_PROTOCOL_PACKET_HEADER_HPP_
#include <cstdint>
#include "config.hpp"

namespace ecomm::protocol{
    
    /**
    * @enum header_type
    * 
    * @brief Enumerates the different types of packet headers used in the protocol.
    */
    enum class header_type : uint8_t {
        data       = 0x0, ///< Generic application data packet
        config     = 0x1, ///< Configuration or parameter change
        control    = 0x2, ///< Protocol-level commands
        routing    = 0x3, ///< Routing or discovery
        time_sync  = 0x4, ///< Time synchronization message
        auth       = 0x5, ///< Authentication or login data
        session    = 0x6, ///< Session initiation/teardown
        status     = 0x7, ///< Device status or health info
        log        = 0x8, ///< Log or diagnostic data
        debug      = 0x9, ///< Debug-specific packets
        firmware   = 0xA, ///< Firmware updates or related payloads
        reserved_b = 0xB, ///< Reserved for future use
        reserved_c = 0xC, ///< Reserved for future use
        reserved_d = 0xD, ///< Reserved for future use
        reserved_e = 0xE, ///< Reserved for future use
        reserved_f = 0xF  ///< Reserved for future use
    };

    /**
    * @enum header_flags
    * 
    * @brief Control flags that may be embedded inside the packet packet_header.
    */
    enum class header_flags : uint8_t
    {
        none       = 0,      ///< No flags
        ack        = 1 << 0, ///< Acknowledgment packet
        error      = 1 << 1, ///< Error indication
        heartbeat  = 1 << 2, ///< Heartbeat signal
        abort      = 1 << 3, ///< Abort signal
        pause      = 1 << 4, ///< Pause signal
        resume     = 1 << 5, ///< Resume signal
        reserved_a = 1 << 6, ///< Reserved for future use
        reserved_b = 1 << 7  ///< Reserved for future use
    };
    
    #pragma pack(push, 1) // Ensure 1-byte packing for packet_header alignment
    /**
    * @class packet_header
    * @brief Compact 24-bit protocol packet_header for packet metadata transmission.
    *
    * Bit layout:
    * ```
    * 31-28 : Type (4 bits)
    * 27-26 : Version (2 bits)
    * 25    : Encrypted (1 bit)
    * 24    : Fragmentation (1 bit)
    * 23-21 : Priority (3 bits) (0 = no priority, higher = more important)
    * 20-18 : Flags (3 bits)
    * 17    : (Has) Checksum (1 bit)
    * 16    : Reserved (1 bit)
    * 15-8  : Sender ID (8 bits)
    * 7-0   : Receiver ID (8 bits)
    * ```
    */
    class packet_header {
    public:
        /**
        * @brief Construct directly from raw 16-bit packet_header value
        *
        * @param raw_value The raw 16-bit value representing the first 2 bytes of the packet header, see diagram below.
        * @param receiver_id The 8-bit receiver ID (default is 1).
        * 
        *```
        * +-------------------------------------------------+-----------------------+-----------------+
        * | 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 | 15 14 13 12 11 10 9 8 | 7 6 5 4 3 2 1 0 |
        * |             raw_value [version (I)]             |     sender_id (I)     |    receiver_id  |
        * +-------------------------------------------------+-----------------------+-----------------+
        *```
        * @note `version` bits are discarded as they are set to global `EPROTOCOL_VERSION` only.
        * @note (I) stands for "Immutable" fields that are set at protocol level and can't be manually changed.
        */
        explicit inline packet_header(uint16_t raw_value, uint8_t receiver_id = 1) noexcept;

        /**
        * @brief Full field constructor.
        * 
        * Constructs a packet_header with all bit fields specified.
        * 
        * @param type Packet type (bits 23-20)
        * @param encrypted Whether the packet is encrypted (bit 17)
        * @param fragmented Whether the packet is fragmented (bit 16)
        * @param priority Packet priority (bits 15-13, 0 = no priority)
        * @param flags Control flags (bits 12-10)
        * @param validated Whether the packet has a checksum (bit 9)
        * @param reserved Reserved bit (bit 8, default false)
        * 
        * Diagram of the packet_header layout:
        * 
        * ```
        * +-------------+-------------+-----+------+-----------+----------+----------+----------+-----------------------+-----------------+
        * | 31 30 29 28 |    27 26    | 25  |  24  | 23 22 21  | 20 19 18 |    17    |    16    | 15 14 13 12 11 10 9 8 | 7 6 5 4 3 2 1 0 |
        * |    type     | version (I) | enc | frag | priority  |  flags   | checksum | reserved |     sender_id (I)     |    receiver_id  |
        * +-------------+-------------+-----+------+-----------+----------+----------+----------+-----------------------+-----------------+
        * ```
        * @note In the diagram, (I) stands for "Immutable" fields that are set at protocol level and can't be manually changed.
        */
        inline packet_header(
            header_type type,
            bool encrypted,
            bool fragmented,
            uint8_t priority,
            header_flags flags,
            bool validated,
            bool reserved = false,
            uint8_t receiver_id = 1
        ) noexcept;
        
        /**
        * @brief Extract type field (bits 28-31).
        *
        * ```
        * +-------------+-----+
        * | 31 30 29 28 | ... |
        * |   type      | ... |
        * +-------------+-----+
        * 
        * @return The packet type as a header_type enum value.
        * ```
        */
        [[nodiscard]] inline header_type type() const noexcept;
        
        /**
        * @brief Extract version field (bits 26-27).
        *
        * ```
        * +-----+---------+-----+
        * | ... |  27 26  | ... |
        * | ... | version | ... |
        * +-----+---------+-----+
        * ```
        */
        [[nodiscard]] inline uint8_t version() const noexcept;
        
        /**
        * @brief Extract encrypted flag (bit 25).
        *
        * ```
        * +-----+-----+-----+
        * | ... |  25 | ... |
        * | ... | enc | ... |
        * +-----+-----+-----+
        * ```
        */
        [[nodiscard]] inline bool encrypted() const noexcept;
        
        /**
        * @brief Extract fragmentation flag (bit 24).
        *
        * ```
        * +-----+------+-----+
        * | ... |  24  | ... |
        * | ... | frag | ... |
        * +-----+------+-----+
        * ```
        */
        [[nodiscard]] inline bool fragmented() const noexcept;
        
        /**
        * @brief Extract priority field (bits 21-23).
        *
        * ```
        * +-----+----------+-----+
        * | ... | 23 22 21 | ... |
        * | ... | priority | ... |
        * +-----+----------+-----+
        * ```
        */
        [[nodiscard]] inline uint8_t priority() const noexcept;
        /**
        * @brief Extract flags field (bits 18-20).
        *
        * ```
        * +-----+----------+-----+
        * | ... | 20 19 18 | ... |
        * | ... |   flags  | ... |
        * +-----+----------+-----+
        * ```
        */
        [[nodiscard]] inline header_flags flags() const noexcept;

        /**
        * @brief Extract validation (checksum) presence flag (bit 17).
        *
        * ```
        * +-----+--------------+-----+
        * | ... |       17     | ... |
        * | ... | has_checksum | ... |
        * +-----+--------------+-----+
        * ```
        */
        [[nodiscard]] inline bool validated() const noexcept;
        
        /**
        * @brief Extract reserved bit (bit 16).
        *
        * ```
        * +-----+----------+-----+
        * | ... |    16    | ... |
        * | ... | reserved | ... |
        * +-----+----------+-----+
        * ```
        */
        [[nodiscard]] inline bool reserved() const noexcept;


        /**        
        * @brief Extract sender ID (bits 8-15).
        *
        * ```
        * +-----+-----------------------+-----+
        * | ... | 15 14 13 12 11 10 9 8 | ... |
        * | ... |       sender_id       | ... |
        * +-----+-----------------------+-----+
        * ```
        * @return The 8-bit sender ID.
        */
        [[nodiscard]] inline uint8_t sender_id() const noexcept;
        
    private:
        /// @brief 16-bit space for the packet_header metadata
        std::uint16_t _space{}; 

        /**
        * @brief The 8 bit sender ID.
        *
        * Immutable; can be overridden by defining a custom value for `EBOARD_ID`.
        */
        std::uint8_t _sender_id{EBOARD_ID};

    public:
        /**
        * @brief The 8 bit receiver ID.
        * ```
        * +-----+----------------------+
        * | ... |   7 6 5 4 3 2 1 0    |
        * | ... |      receiver_id     |
        * +-----+----------------------+
        * ```
        */
        std::uint8_t receiver_id{};
    };
    #pragma pack(pop) // Restore previous packing alignment
    
} // namespace ecomm::protocol

#include "packet_header.inl"
#endif // ECOMM_PROTOCOL_PACKET_HEADER_HPP_