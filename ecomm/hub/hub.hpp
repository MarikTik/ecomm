// SPDX-License-Identifier: BSL-1.1
/**
* @file hub.hpp
*
* @brief Defines the `ecomm::hub` class for managing multiple communication interfaces.
*
* @ingroup ecomm
* 
* This file declares the `ecomm::hub` template class, which acts as a central communication manager
* that enables simultaneous operation of multiple communication interfaces (e.g., serial, Wi-Fi, etc.)
*
* (Imagine it as the USB hub of your computer where you can plug a mouse, keyboard, headphones into it and than connect the other end to a single 
* port on your computer. The hub will then manage the communication with all of them)
* 
* The `hub` class provides runtime control over which interfaces are active for sending and receiving packets,
* using a lightweight compile-time-indexed bitset called `typeset`. The class supports dynamically enabling or disabling
* each interface's participation in send and receive operations.
*
* All interfaces must conform to a common interface API, providing `send(const Packet&)` and `try_receive()` methods.
* 
* @note To simplify usage with common defaults (all interfaces enabled for send/receive by default),
*       prefer using the helper function `make_hub()`, which deduces template arguments and enables both
*       sending and receiving on all provided interfaces.
* 
* @example Example usage:
* @code
* ecomm::hub<my_packet, my_interface> hub;
*
* my_packet_t p = ...;
* hub.send(p); // Send via all interfaces
* auto maybe = hub.try_receive(); // Try receive from any active receiver
* @endcode
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
*      - Added static assertion to verify that all interfaces in `Interfaces` template parameter pack
*        derive from `interface<Interface>` to ensure type safety and guarantee proper base delegation.
* - 2025-07-21
*      - Fixed improper namespace usage in `typeset` declarations.
*      - Added namespace tags for `interface` in static assertion. 
*/
#ifndef ECOMM_HUB_HUB_HPP_
#define ECOMM_HUB_HUB_HPP_
#include <type_traits>
#include <tuple>
#include <optional>
#include "../interfaces/interface.hpp"
#include <etools/meta/typeset.hpp>

namespace ecomm{

    /**
    * @class hub
    * @brief Manages multiple communication interfaces for simultaneous packet transmission and reception.
    *
    * This template class allows the user to define a collection of communication interfaces and control
    * which ones are currently active for sending and/or receiving data.
    *
    * @tparam Packet The type of packet being sent or received by all interfaces.
    * 
    * @tparam Interfaces A variadic pack of interface types (e.g., serial, Wi-Fi), each of which must implement
    *                    the interface API.
    * 
    * @warning hub does not perform any validation on the interfaces passed to it, e.g. it won't check whether they 
    * derive from the actual `interface` class, or whether they support this given packet type.
    * All of that is the user's responsibility.
    * 
    * @note The `hub` class enables all interfaces for sending and receiving by default.
    */
    template<typename Packet, typename ...Interfaces>
    class hub{
    public: 
        /**
        * @brief Constructs the hub, taking ownership of the provided interface instances.
        *
        * Initializes the internal tuple of interfaces by moving from the provided arguments.
        * Performs a static assertion to ensure all provided interface types inherit from
        * `ecomm::interface::interface`.
        *
        * @param args Rvalue references to the interface objects (one for each type in Interfaces...).
        */
        inline explicit hub(Interfaces&&... args);

        /**
        * @brief Enables an interface for sending packets.
        *
        * Marks the given interface type as active for outbound communication.
        * Only interfaces explicitly enabled with this method will be used in `send()`.
        *
        * @tparam Interface The interface type to activate for sending.
        */
        template<typename Interface>
        inline void use_sender();
        
        /**
        * @brief Enables an interface for receiving packets.
        *
        * Marks the given interface type as active for inbound communication.
        * Only interfaces explicitly enabled with this method will be checked in `try_receive()`.
        *
        * @tparam Interface The interface type to activate for receiving.
        */
        template<typename Interface>
        inline void use_receiver();
        
        /**
        * @brief Disables an interface for sending packets.
        *
        * Marks the given interface type as inactive for outbound communication.
        * After calling this, the interface will no longer be used in `send()`.
        *
        * @tparam Interface The interface type to deactivate for sending.
        */
        template<typename Interface>
        inline void remove_sender();

        /**
        * @brief Disables an interface for receiving packets.
        *
        * Marks the given interface type as inactive for inbound communication.
        * After calling this, the interface will no longer be queried in `try_receive()`.
        *
        * @tparam Interface The interface type to deactivate for receiving.
        */
        template<typename Interface>
        inline void remove_receiver();

        /**
        * @brief Sends a packet through all active sender interfaces.
        *
        * Iterates over all registered interfaces and sends the packet to those currently
        * marked as active senders using `use_sender()`.
        *
        * @param packet The packet to send.
        */
        inline void send(Packet &packet);
        
        /**
        * @brief Attempts to receive a packet from any active receiver interface.
        *
        * Iterates through all interfaces marked as active receivers and invokes their
        * `try_receive()` method. Returns the first successfully received packet, if any.
        *
        * @return An `std::optional<Packet>` containing the received packet, or `std::nullopt` if none.
        */
        inline std::optional<Packet> try_receive();

    private:
        /**
        * @brief Sends a packet through a single interface, conditionally.
        *
        * Helper function used in fold expressions. Checks whether the specified interface
        * is currently marked as active for sending before calling `send()` on it.
        *
        * @tparam Interface The type of the interface.
        * @param interface Reference to the interface instance.
        * @param packet The packet to send.
        */
        template<typename Interface>
        inline void maybe_send(Interface& interface, Packet& packet);
        
        /**
        * @brief Attempts to receive a packet from a single interface, conditionally.
        *
        * Helper function used in fold expressions. Checks whether the specified interface
        * is currently marked as active for receiving before calling `try_receive()` on it.
        *
        * @tparam Interface The type of the interface.
        * @param interface Reference to the interface instance.
        * @return An optional packet if received, or `std::nullopt` otherwise.
        */
        template<typename Interface>
        inline std::optional<Packet> maybe_try_receive(Interface& interface);


        std::tuple<Interfaces...> _interfaces; ///< Tuple of communication interfaces.
        etools::meta::typeset<Interfaces...> _sender_statuses; ///< Typeset for managing flags associated with sender interfaces.
        etools::meta::typeset<Interfaces...> _receiver_statuses; ///< Typeset for managing flags associated with receiver interfaces.

        static_assert((std::is_base_of_v<interfaces::interface<Interfaces>, Interfaces> && ...), "All interfaces must derive from `interface<>`");
    };

} // namespace ecomm

#include "hub.tpp"
#endif // ECOMM_HUB_HUB_HPP_