// SPDX-License-Identifier: BSL-1.1
/**
* @file interface.hpp
*
* @brief Defines a CRTP base class for communication interfaces.
*
* @ingroup ecomm_interfaces ecomm::interfaces
*
* This file defines a template base class `ecomm::interface` that facilitates
* the creation of different communication interfaces (e.g., Serial, Wi-Fi) by
* enforcing a common API. Derived classes provide the specific implementation
* details for sending and receiving data.
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
* - 2025-07-17
*      - renamed implementation method of `IntrefaceImpl` to `delegate_try_receive` and `delegate_send`.
*/
#ifndef ECOMM_INTERFACES_INTERFACE_HPP_
#define ECOMM_INTERFACES_INTERFACE_HPP_
#include <cstdint>
#include <optional>

namespace ecomm::interfaces{

    /**
    * @struct interface
    * @brief A base class for communication interfaces.
    *
    * This template class defines a common interface for communication implementations.
    * It requires derived classes to implement `try_receive` and `send` methods,
    * ensuring a consistent way to interact with different communication types.
    *
    * @tparam InterfaceImpl The derived class implementing the specific communication interface
    * (e.g., SerialInterface, WifiInterface).
    */
    template<typename InterfaceImpl>
    struct interface{
        /**
        * @brief Attempts to receive a packet.
        *
        * This method attempts to receive a packet using the specific communication
        * interface implemented in the derived class. It delegates the actual
        * reception logic to the derived class's `try_receive` method.
        * 
        * @tparam Packet The type of packet to attempt receiving.
        * 
        * @return An `std::optional` containing the received packet if available,
        * or `std::nullopt` if no packet was received. The derived class
        * is responsible for handling the specifics of packet reception.
        */
        template<typename Packet>
        [[nodiscard]] inline std::optional<Packet> try_receive();
        
        /**
        * @brief Sends a packet.
        *
        * This method sends a packet using the specific communication interface
        * implemented in the derived class. It delegates the actual sending logic
        * to the derived class's `send` method.
        *
        * @param packet The packet to send.
        * The derived class is responsible for handling the specifics
        * of packet transmission.
        * 
        * @tparam Packet The type of packet to send.
        * 
        * @note There are no gurantees that the packet will not be altered during the 
        * transmission process. Specifically, the packet's checksum or parity field 
        * may be modified by the `Validator`'s `seal()` method.
        * 
        * @note The caller must not assume that the packet remains bitwise identical after sending.
        */
        template<typename Packet>
        inline void send(Packet &packet);
    };

} // namespace ecomm::interfaces

#include "interface.tpp" // Include the template implementation file
#endif // ECOMM_INTERFACES_INTERFACE_HPP_