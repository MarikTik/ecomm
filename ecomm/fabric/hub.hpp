// SPDX-License-Identifier: MIT
/**
* @file hub.hpp
*
* @brief Defines `ecomm::hub`, a compile-time aggregate of several communication channels.
*
* @ingroup ecomm
*
* A `hub<Channels...>` combines several already-constructed channels -- any
* mix of `ecomm::channels::channel<Impl>`-derived types and
* `ecomm::channels::reliable_channel<Impl, Packet, ...>` instances -- behind
* one API. Think of it as a USB hub: several physical links (a UART leaf
* link, a Wi-Fi link, ...) plugged into one place, individually enabled or
* disabled at runtime, each addressed with an explicit, caller-known `Packet`
* type per call.
*
* @par hub is for a known Packet; router is for an unknown one
* `hub::send<Packet>()` and `hub::try_receive<Packet>()` require the caller
* to already know which `Packet` they're dealing with -- these are single
* responsibility, single-type operations. Polling for *whichever* of several
* packet types shows up and routing it to the handler for that type is a
* different responsibility (heterogeneous dispatch, not explicit addressing)
* and lives in `ecomm::router` (`fabric/router.hpp`) instead. If you want
* both, use both -- channels are held by reference, so nothing stops
* constructing a `hub` and a `router` over the same physical channels.
*
* @par Routing is capability-based, not identity-based
* Since `channel<Impl>` moved `Packet` to a per-call template parameter (see
* `channel.hpp`), most channels no longer have one fixed packet type to
* compare against -- `arduino_serial_channel<>` and `arduino_wifi_channel<>`
* accept any packet type per call, and `esp_async_wifi_channel<BufferCapacity>`
* accepts any packet type that fits its byte ring. `reliable_channel` is the
* one exception: its ack/retry state is genuinely per-stream, so it stays
* fixed to one `Packet`.
*
* `hub` reflects this directly: instead of asking "what is this channel's
* packet type," it asks "can this channel handle *this* `Packet`, right now"
* -- checked structurally (SFINAE on `send`/`try_receive` well-formedness),
* not via a type-level `packet_t` alias. The practical consequence: a
* `send<Packet>()` call reaches *every* active sender that can handle
* `Packet`, which for a hub of only flexible channels usually means all of
* them at once.
* ```cpp
* hub h{serial_link, wifi_link};   // both flexible: both accept any Packet
* h.send(small_packet_instance);   // -> reaches BOTH serial_link and wifi_link
*
* h.set_role<decltype(wifi_link)>(channels::role::receiver);   // opt out as a sender explicitly
* h.send(small_packet_instance);                               // -> serial_link only, until re-enabled
* ```
* If you want a channel to only ever carry one packet type *within a
* particular hub*, even though the channel itself could carry more, use
* `set_role` to keep it out of calls for the packet types you don't want it
* to see, or simply don't put it in a hub whose other members would send it
* packets it shouldn't handle.
*
* @par Compile-time validation
* Two `static_assert`s fire at `hub<...>`'s own instantiation point (not deep
* inside a call site) if misused:
* - Every `Channel` in `Channels...` must be recognizable as *some* kind of
*   channel: either it derives from `ecomm::channels::channel<Channel>` (the
*   flexible, per-call-`Packet` shape), or it exposes `using packet_t =
*   SomePacket;` together with non-template `send(packet_t&)` /
*   `try_receive()` matching that `packet_t` (the fixed shape, e.g.
*   `reliable_channel`). This is a coarse sanity check -- "is this actually a
*   channel" -- not a check against any specific `Packet`, since a flexible
*   channel has no single packet type to check against.
* - Every type in `Channels...` must be pairwise distinct (`hub` indexes its
*   internal per-channel enable/disable flags by type via
*   `etools::meta::typeset`). Use each channel's `tag` template parameter to
*   disambiguate two instances of the same transport, e.g.
*   `arduino_serial_channel<0>` and `arduino_serial_channel<1>`.
*
* A third check is per-call rather than class-wide: `send<Packet>` and
* `try_receive<Packet>` each `static_assert` that at least one channel in
* `Channels...` can currently handle the requested `Packet` -- naming a
* `Packet` no channel in this `hub` supports is a compile error, not a
* silent no-op.
*
* @par Ownership: hub does not own its channels
* `hub` stores **references** to channels the caller already constructed and
* keeps alive elsewhere (typically as `static`/file-scope objects, the same
* pattern every channel already uses for the hardware it wraps -- see
* `arduino_serial_channel`'s `HardwareSerial&`). This is a deliberate choice,
* not an oversight: `esp_async_wifi_channel` registers its own address with
* AsyncTCP at construction time
* (`_server.onClient([](void*, AsyncClient*){...}, this)`), so moving such a
* channel into an owning container after construction leaves AsyncTCP holding
* a dangling pointer. Reference storage makes that class of bug structurally
* impossible: a channel named in `Channels...` is never relocated, ever.
*
* @par send() aggregates per-channel results
* `hub::send<Packet>()` fans the packet out to every *active* sender that can
* handle `Packet`, and returns one `std::optional<send_result>` per such
* channel (in `Channels...` declaration order among that subset; disengaged
* for a matching channel that was not an active sender). This is what lets a
* caller distinguish "channel 2 timed out" from "everything worked" when the
* hub contains a `reliable_channel`.
*
* @par A hub that mixes blocking and non-blocking channels blocks as a whole
* `reliable_channel::send` busy-polls for an acknowledgement (see
* `reliable_channel.hpp`'s `@par BLOCKING`). `hub::send()` calls every active,
* matching sender in sequence, so a `reliable_channel` member's worst-case
* retry/timeout window delays every other matching channel's send in the
* same call. This is inherent to combining a blocking and a non-blocking
* transport in one fan-out call, not something `hub` can hide.
*
* @note Class template argument deduction works without an explicit guide:
*       `Channels...` is deduced directly from the constructor arguments.
*       ```cpp
*       ecomm::hub h{serial_channel, wifi_channel};
*       ```
*
* @example Example usage:
* @code
* ecomm::hub h{serial_channel, wifi_channel};   // both flexible
*
* small_packet p = ...;
* auto results = h.send(p);                      // reaches every channel that can carry small_packet
* auto maybe   = h.try_receive<small_packet>();  // must name Packet -- nothing to deduce from
* @endcode
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2025-07-03
*
* @copyright
* MIT License
* Copyright (c) 2025 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2025-07-03 Initial creation.
* - 2025-07-17 Added static assertion to verify that all interfaces in `Interfaces` template parameter pack
*      derive from `interface<Interface>` to ensure type safety and guarantee proper base delegation.
* - 2025-07-21 Fixed improper namespace usage in `typeset` declarations. Added namespace tags for
*      `interface` in static assertion.
* - 2026-07-14 Full rewrite against the current `channel<Impl, Packet>` / `reliable_channel<...>` API
*      (the `interface` type this previously targeted no longer exists). Channels are now stored by
*      reference instead of owned by value. `send()` aggregates a `std::optional<send_result>` per
*      channel. Validation is duck-typed via `packet_t` + method-signature SFINAE, not inheritance.
* - 2026-07-15 Dropped the single class-level `Packet` parameter: `hub<Packet, Channels...>` became
*      `hub<Channels...>`, so a hub may combine channels operating on different packet types (e.g. a
*      small packet on a UART leaf link and a larger one on a Wi-Fi uplink). `send<Packet>` deduces
*      `Packet` from its argument like any function template; `try_receive<Packet>` cannot (nothing to
*      deduce a return type from) and must name `Packet` explicitly.
* - 2026-07-16 Switched from identity-based routing (`Channel::packet_t == Packet`) to
*      capability-based routing (`Channel` can handle `Packet`, checked structurally), following
*      `channel<Impl, Packet>` -> `channel<Impl>` (Packet moved to a per-call template parameter
*      there too -- see channel.hpp's own 2026-07-16 changelog entry). Most channels no longer have a
*      single fixed packet type to compare against, so `hub` now checks "can `Channel` handle
*      `Packet`" via SFINAE on `send`/`try_receive` well-formedness instead.
* - 2026-07-17 Replaced `use_sender`/`use_receiver`/`remove_sender`/`remove_receiver` (four methods,
*      each toggling one of two independent flags) with a single `set_role<Channel>(role)`,
*      assigning both flags at once from one of four explicit states (`sender`, `receiver`,
*      `transceiver`, `none`) rather than incrementally flipping one bit at a time.
* - 2026-07-19 Split for single responsibility: `try_receive_any`/`on_channel` (heterogeneous,
*      handler-driven dispatch across several possibly-unknown packet types) moved out to the new
*      `ecomm::dispatcher` (`hub/dispatcher.hpp`) -- a different responsibility from `hub`'s explicit,
*      caller-known-`Packet` `send`/`try_receive`. `hub_role` moved out to `ecomm::channels::role`
*      (`channels/role.hpp`): the concept belongs to a channel, not to `hub`. The capability-checking
*      SFINAE machinery (`is_channel_like_v` and friends) moved to the shared
*      `ecomm::channels::details` (`channels/channel_traits.hpp`), since `dispatcher` needs it too.
* - 2026-07-20 Renamed `ecomm::dispatcher` to `ecomm::router` (a better fit for what it does -- content-
*      aware forwarding by packet type, the same relationship a real router has to a real hub) and moved
*      the `hub`/`router` pair from `ecomm/hub/` to `ecomm/fabric/` (a directory named "hub" no longer
*      described both types living in it; "fabric" is standard industry vocabulary for "the collection of
*      interconnect devices" and favours neither).
*/
#ifndef ECOMM_FABRIC_HUB_HPP_
#define ECOMM_FABRIC_HUB_HPP_
#include <array>
#include <optional>
#include <tuple>
#include <type_traits>

#include <etools/meta/traits.hpp>
#include <etools/meta/typeset.hpp>

#include "../channels/channel_traits.hpp"
#include "../channels/role.hpp"
#include "../channels/send_result.hpp"

namespace ecomm {

    /**
    * @class hub
    *
    * @brief Combines several channels -- possibly operating on different
    *        packet types -- behind one API, addressed with an explicit,
    *        caller-known `Packet` type per call.
    *
    * See the file-level documentation above for the ownership model, the
    * compile-time validations, capability-based routing, and how
    * `send<Packet>()`'s aggregated result differs from a single channel's.
    * For heterogeneous dispatch (poll for whichever of several packet types
    * arrives and route it to a handler), see `ecomm::router` instead.
    *
    * @tparam Channels A pack of channel-like types (see
    *                  `channels::details::is_recognizable_channel_v`),
    *                  pairwise distinct, each referencing a channel the
    *                  caller owns and keeps alive for at least as long as
    *                  this `hub`.
    */
    template<typename... Channels>
    class hub {
        static_assert(sizeof...(Channels) >= 1,
            "ecomm::hub: at least one channel is required.");
        static_assert(etools::meta::is_distinct_v<Channels...>,
            "ecomm::hub: all Channels must be pairwise distinct types. Use each "
            "channel's `tag` template parameter to disambiguate multiple instances "
            "of the same transport (e.g. arduino_serial_channel<0> and "
            "arduino_serial_channel<1>).");
        static_assert((channels::details::is_recognizable_channel_v<Channels> and ...),
            "ecomm::hub: every Channel must be recognizable as a channel -- either "
            "derive from ecomm::channels::channel<Channel> (per-call Packet, e.g. "
            "arduino_serial_channel<>), or expose `using packet_t = SomePacket;` with "
            "matching non-template `send(packet_t&)`/`try_receive()` (fixed Packet, "
            "e.g. ecomm::channels::reliable_channel<Impl, Packet, ...>).");

    public:
        /**
        * @brief Per-channel outcome of a fan-out `send<Packet>()`, in the
        *        declaration order of the `Channels...` subset that can
        *        currently handle `Packet`.
        *
        * Slot `i` is engaged with the `send_result` returned by the `i`-th
        * matching channel if it was an active sender at the time of the
        * call, or disengaged if it was not (and therefore was not sent to at
        * all). Sized to exactly the number of matching channels -- a channel
        * that cannot handle `Packet` has no slot at all, rather than a
        * permanently-disengaged one.
        */
        template<typename Packet>
        using send_results_t = std::array<std::optional<channels::send_result>, channels::details::count_matching_v<Packet, Channels...>>;

        /**
        * @brief Bind a hub to already-constructed channels, enabling all of them.
        *
        * `hub` stores references, not copies -- see the ownership note in this
        * file's documentation. Every channel starts at `channels::role::transceiver`
        * (active for both sending and receiving); use `set_role` to change one.
        *
        * @param[in] channels One lvalue reference per type in `Channels...`,
        *                     in the same order. Each must outlive this `hub`.
        */
        explicit hub(Channels&... channels) noexcept;

        /**
        * @brief Set `Channel`'s participation in this hub to exactly `role`.
        *
        * Assigns both the active-sender and active-receiver flags at once,
        * from one of the four explicit `channels::role` states -- there is
        * no partial or incremental update; whatever `Channel`'s
        * participation was before this call, it is exactly `role` afterward.
        *
        * @tparam Channel One of `Channels...`.
        * @param[in] role The complete participation state to assign.
        */
        template<typename Channel>
        void set_role(channels::role role) noexcept;

        /**
        * @brief Send a packet through every active sender channel that can
        *        currently handle `Packet`.
        *
        * `Packet` is deduced from `packet`'s type -- no explicit template
        * argument is needed, exactly as for a single `channel<>::send`.
        * `static_assert`s that at least one channel in `Channels...`
        * currently can handle `Packet`.
        *
        * Calls `send(packet)` on each matching channel whose role is
        * `sender` or `transceiver`, in declaration order, unconditionally (a failure
        * on one channel does not skip the rest). See the file-level `@par`
        * on mixing blocking and non-blocking channels for why this call's
        * worst-case latency is the sum of its matching members'.
        *
        * @tparam Packet Deduced from `packet`.
        * @param[in,out] packet Packet to send. Each active matching channel
        *                       may modify its FCS field via its own sealing step.
        *
        * @return One `std::optional<send_result>` per matching channel, in
        *         declaration order among that subset; disengaged for ones
        *         that were not active senders.
        */
        template<typename Packet>
        [[nodiscard]] send_results_t<Packet> send(Packet& packet) noexcept;

        /**
        * @brief Attempt to receive a `Packet` from any active receiver
        *        channel that can currently handle it.
        *
        * Unlike `send`, `Packet` cannot be deduced here -- there is no
        * argument to deduce a return type from, a hard rule of C++
        * template argument deduction -- so it must be named explicitly:
        * `hub.try_receive<small_packet>()`. `static_assert`s that at least
        * one channel in `Channels...` currently can handle `Packet`.
        *
        * Polls each matching channel whose role is `receiver` or
        * `transceiver`, in declaration order among that subset, and returns the first
        * packet found. Channels after the first hit are not polled on this
        * call.
        *
        * @tparam Packet Must be named explicitly.
        * @return The first available packet, or `std::nullopt` if no active
        *         matching receiver had one.
        */
        template<typename Packet>
        [[nodiscard]] std::optional<Packet> try_receive() noexcept;

    private:
        /// @brief `send<Packet>`'s per-channel visitor: no-ops for a Channel that cannot handle Packet.
        template<typename Packet, typename Channel>
        void maybe_send_if_matching(Channel& channel, Packet& packet, send_results_t<Packet>& results, std::size_t& next) noexcept;

        /// @brief `try_receive<Packet>`'s per-channel visitor: no-ops for a Channel that cannot handle Packet.
        template<typename Packet, typename Channel>
        bool try_receive_if_matching(Channel& channel, std::optional<Packet>& result) noexcept;

        std::tuple<Channels&...> _channels; ///< Non-owning references to the combined channels.
        etools::meta::typeset<Channels...> _sender_statuses;   ///< Per-type active-sender flags.
        etools::meta::typeset<Channels...> _receiver_statuses; ///< Per-type active-receiver flags.
    };

    /**
    * @brief `Channels...` deduces directly from the constructor arguments;
    *        no explicit guide is needed for that alone, but one is declared
    *        for documentation and to pin the deduction to lvalue references
    *        (matching the constructor) rather than something looser.
    */
    template<typename... Channels>
    hub(Channels&...) -> hub<Channels...>;

} // namespace ecomm

#include "hub.tpp"
#endif // ECOMM_FABRIC_HUB_HPP_
