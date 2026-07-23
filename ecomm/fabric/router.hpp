// SPDX-License-Identifier: MIT
/**
* @file router.hpp
*
* @brief Defines `ecomm::router`, heterogeneous packet dispatch across
*        several channels and possibly-unknown packet types, with byte-level
*        reassembly so a partially-arrived packet is never misframed.
*
* @ingroup ecomm
*
* A `router` polls a set of channels for whichever packet type arrives first
* and routes it to the handler that consumes that type. This is a different
* responsibility from `ecomm::hub` (`fabric/hub.hpp`): `hub::send<Packet>()`/
* `try_receive<Packet>()` require the caller to already know `Packet`; `router`
* is for when they don't -- the same relationship a real network router has to
* a real hub: a hub repeats everything to every port with no awareness of
* content, a router makes a forwarding decision based on what arrived.
*
* @par Constructed once, then polled -- it holds reassembly state
* `router` owns a small per-channel byte buffer, so it must persist across
* polls; it is **not** a throwaway temporary. Construct it from one
* `on_channel(channel_ref, handlers...)` group per channel, then poll it in
* your loop:
* ```cpp
* ecomm::router r{
*     on_channel(wifi_link,
*         [](telemetry_packet& p) { ... },
*         [](command_packet& p)   { ... }
*     ),
*     on_channel(serial_link,
*         [](command_packet& p)   { ... }
*     )
* };
* while (r.try_receive_any()) { }   // drains everything ready this tick
* ```
* Class template arguments are deduced from the `on_channel(...)` groups (a
* deduction guide is provided), so `ecomm::router r{...};` needs no explicit
* template arguments. The channels are referenced, not owned; they must outlive
* the router. A channel is polled by exactly the router that names it -- there
* is no separate enable/disable state (unlike `hub::set_role`); which channels
* participate is fixed by the groups the router was built from.
*
* @par Candidate types come from the handlers
* Each handler is a callable taking one `Packet&`; the packet types the
* handlers in a group declare *are* the candidates polled for on that group's
* channel. No packet type is ever named twice. Handlers must declare a concrete
* parameter type -- a generic `[](auto& p)` carries no type to poll for and is
* rejected with a `static_assert`.
*
* @par Why reassembly, not a plain typed read
* Streaming channels (`arduino_serial_channel`, `arduino_wifi_channel`,
* `esp_async_wifi_channel` -- everything derived from `channel<Impl>`) deliver
* raw bytes with no packet framing, and a typed `try_receive<Packet>()`
* *consumes* `sizeof(Packet)` bytes the moment that many are buffered, **before**
* it can check validity. With two candidate sizes on one channel that is
* destructive: a 48-byte packet that has only partially arrived (say 26 of 48
* bytes) fails the 48-byte probe harmlessly, but then satisfies a 16-byte probe
* on byte count alone -- consuming 16 bytes of the still-arriving larger packet,
* failing validation, and destroying it with no way to recover, since the
* transport has no peek. No backlog is required for this; it happens on the
* very first poll of a mid-arrival packet, and draining *more* often makes it
* *more* likely.
*
* `router` avoids it by never letting the channel frame: for each streaming
* channel it pulls raw bytes (`channel::receive_raw`) into a per-channel buffer
* sized to that group's largest candidate, and does framing itself against the
* buffered bytes -- validating a candidate *before* consuming it, and keeping
* anything that does not yet form a complete, valid packet for the next poll.
* A partially-arrived packet simply waits in the buffer until the rest arrives.
* `reliable_channel` is exempt: it is message-atomic (it delivers whole,
* already-framed packets and carries only one packet type), so `router` polls
* it directly via `try_receive()` with no buffering.
*
* @par Candidates are probed largest-first, automatically
* Against the buffered bytes, `router` tests candidate prefixes largest-first
* (`etools::meta::sort_t` with `etools::meta::size_greater`). A smaller
* candidate can validate against the leading bytes of a larger queued packet
* only by an FCS collision (astronomically unlikely), so testing large-to-small
* frames each packet correctly regardless of handler declaration order. When a
* candidate's prefix validates it is dispatched and consumed (the trailing
* remainder shifts to the front of the buffer); when none do and the buffer is
* full of unframable bytes, one leading byte is dropped to resync.
*
* @par Two guardrails, enforced at compile time
* For a streaming channel carrying more than one candidate type, `router`
* `static_assert`s that:
* - **candidate sizes are pairwise distinct** -- two equally-sized candidates
*   are indistinguishable to prefix framing (nothing but the FCS could tell
*   them apart, and that is not what an FCS is for); and
* - **every candidate carries a checksum** (`ChecksumPolicy != none`) -- a
*   checksum-less packet's `is_valid` is unconditionally `true`, so a torn or
*   misframed read would be dispatched as if genuine. With one candidate type
*   neither hazard exists, so a single-`none` channel is still allowed.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-07-19
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-07-19 Split out of `ecomm::hub` for single responsibility: `hub` addresses
*      channels with a caller-known `Packet` per call, `router` (then named
*      `dispatcher`) routes an arriving packet of unknown type to the handler for
*      whatever it turns out to be -- different responsibilities, so different
*      types. Dropped the class-level `Channels...` template entirely: channels are
*      now named per-call via `on_channel(channel_ref, handlers...)`, which also
*      dropped the old "bare handlers applied to every channel" flat form (channels
*      are always named explicitly now) and the active/inactive tracking
*      `hub::set_role` still needs (a channel simply isn't named in a call it
*      shouldn't be polled for). Replaced the hand-rolled largest-first sort with
*      `etools::meta::sort_t<etools::meta::size_greater, ...>` (etools v1.0.6).
* - 2026-07-20 Renamed from `ecomm::dispatcher` to `ecomm::router` and moved from
*      `ecomm/hub/` to `ecomm/fabric/` -- see hub.hpp's changelog for the reasoning
*      (the hub/router naming pair matches real networking-hardware behaviour more
*      precisely: a hub repeats without looking at content, a router forwards based
*      on it, exactly what these two classes do with packets).
* - 2026-07-21 Fixed a destructive-read defect on streaming channels carrying more
*      than one packet size: a typed `try_receive<Packet>()` consumes bytes before
*      it can validate them, so a partially-arrived larger packet was misframed and
*      destroyed by a smaller candidate's probe (no backlog required). `router` now
*      does its own byte-level framing over `channel::receive_raw` -- it became a
*      stateful object (constructed from its `on_channel(...)` groups, holding a
*      per-channel reassembly buffer, polled via the now-nullary `try_receive_any()`)
*      instead of a stateless temporary, because reassembly state must persist
*      across polls. `reliable_channel` (message-atomic) keeps the direct
*      `try_receive()` path. Added the pairwise-distinct-size and
*      every-candidate-checksummed guardrails as `static_assert`s.
*/
#ifndef ECOMM_FABRIC_ROUTER_HPP_
#define ECOMM_FABRIC_ROUTER_HPP_
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include <etools/meta/sort.hpp>
#include <etools/meta/typelist.hpp>

#include "../channels/channel_traits.hpp"

namespace ecomm::details {

    // -----------------------------------------------------------------------
    // Handler introspection -- `router`'s candidate packet types are read off
    // the handlers' own parameter lists, so the caller never names a packet
    // type twice (once for router to poll for, once in the lambda that
    // consumes it).
    // -----------------------------------------------------------------------

    /// @brief Extracts `P` from a handler whose `operator()` takes `P&`.
    template<typename T>
    struct handler_packet;

    template<typename Lambda, typename R, typename P>
    struct handler_packet<R (Lambda::*)(P&) const> { using type = P; };

    template<typename Lambda, typename R, typename P>
    struct handler_packet<R (Lambda::*)(P&)> { using type = P; };

    /// @brief `true` iff `H` has a non-overloaded, non-template `operator()`
    ///        whose address can be taken -- i.e. a concrete (non-generic)
    ///        lambda or functor, the only shape a packet type can be read from.
    template<typename H, typename = void>
    struct is_handler : std::false_type {};

    template<typename H>
    struct is_handler<H, std::void_t<
        typename handler_packet<decltype(&std::remove_reference_t<H>::operator())>::type
    >> : std::true_type {};

    template<typename H>
    inline constexpr bool is_handler_v = is_handler<H>::value;

    /// @brief The packet type a handler consumes.
    template<typename H>
    using handler_packet_t =
        typename handler_packet<decltype(&std::remove_reference_t<H>::operator())>::type;

    // -----------------------------------------------------------------------
    // Compile-time facts about a group's candidate packet types.
    // -----------------------------------------------------------------------

    /// @brief Largest `sizeof` among `Ps...` (0 for an empty pack). This is the
    ///        reassembly buffer size a streaming channel's group needs.
    template<typename... Ps>
    constexpr std::size_t max_packet_size() noexcept {
        std::size_t m = 0;
        ((m = sizeof(Ps) > m ? sizeof(Ps) : m), ...);
        return m;
    }

    /// @brief `true` iff every `sizeof(Ps)` is distinct from every other.
    template<typename... Ps>
    constexpr bool sizes_pairwise_distinct() noexcept {
        constexpr std::size_t n = sizeof...(Ps);
        if constexpr (n < 2) {
            return true;
        } else {
            const std::size_t sizes[] = { sizeof(Ps)... };
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = i + 1; j < n; ++j)
                    if (sizes[i] == sizes[j]) return false;
            return true;
        }
    }

    /// @brief `true` iff every `Ps` carries a checksum (`ChecksumPolicy != none`,
    ///        i.e. a non-zero FCS field). Vacuously `true` for an empty pack.
    template<typename... Ps>
    constexpr bool all_checksummed() noexcept {
        return ((Ps::header_t::fcs_size > 0) and ...);
    }

} // namespace ecomm::details

namespace ecomm {

    /**
    * @struct channel_handlers
    *
    * @brief One channel bound to its handler group, as produced by `on_channel`.
    *
    * Construct via `on_channel(channel, handlers...)` rather than directly.
    *
    * @tparam Channel  The channel these handlers apply to.
    * @tparam Handlers The handler callables, each taking one `Packet&`.
    */
    template<typename Channel, typename... Handlers>
    struct channel_handlers {
        Channel& channel;
        std::tuple<Handlers...> handlers;
    };

    namespace details {

        /// @brief `true` iff `T` is an `on_channel(...)` group.
        template<typename T>
        struct is_channel_handlers : std::false_type {};

        template<typename Channel, typename... Handlers>
        struct is_channel_handlers<channel_handlers<Channel, Handlers...>> : std::true_type {};

        template<typename T>
        inline constexpr bool is_channel_handlers_v = is_channel_handlers<std::decay_t<T>>::value;

        /**
        * @brief Compile-time facts about one `on_channel(...)` group: its channel
        *        type, whether that channel needs byte-level reassembly, its
        *        candidate count, its reassembly buffer size, and whether it
        *        satisfies the two multi-candidate guardrails.
        */
        template<typename Group>
        struct group_info;

        template<typename Channel, typename... Handlers>
        struct group_info<channel_handlers<Channel, Handlers...>> {
            using channel_type = Channel;

            /// @brief A streaming channel is framed by `router`; a message-atomic
            ///        one (`reliable_channel`) is polled directly.
            static constexpr bool streaming =
                channels::details::is_streaming_channel_v<Channel>;

            static constexpr std::size_t candidate_count = sizeof...(Handlers);

            /// @brief Reassembly buffer size: the largest candidate for a
            ///        streaming channel, 0 for a message-atomic one (unused).
            static constexpr std::size_t buffer_size =
                streaming ? max_packet_size<handler_packet_t<Handlers>...>() : 0;

            static constexpr bool sizes_distinct =
                sizes_pairwise_distinct<handler_packet_t<Handlers>...>();

            static constexpr bool checksummed =
                all_checksummed<handler_packet_t<Handlers>...>();
        };

        /**
        * @struct group_slot
        *
        * @brief One `on_channel(...)` group plus the reassembly state `router`
        *        keeps for it across polls: a byte buffer sized to the group's
        *        largest candidate and the number of valid bytes currently in it.
        *
        * Aggregate: constructed as `group_slot<Group>{ std::move(group) }`,
        * with `buffer`/`fill` defaulted. `buffer` has size 0 for a
        * message-atomic channel, which never touches it.
        */
        template<typename Group>
        struct group_slot {
            using group_type = Group;

            static_assert(!group_info<Group>::streaming
                    || channels::details::has_receive_raw_v<typename group_info<Group>::channel_type>,
                "ecomm::router: a streaming channel must provide "
                "receive_raw(std::byte*, std::size_t) -- every channel<Impl>-derived "
                "channel does. (reliable_channel is message-atomic and exempt.)");

            static_assert(!group_info<Group>::streaming
                    || group_info<Group>::sizes_distinct,
                "ecomm::router: the candidate packet types on one channel must have "
                "pairwise-distinct sizeof. Two equally-sized candidates cannot be told "
                "apart by prefix framing (only their FCS would differ, which is not what "
                "a checksum is for). Put same-sized types on separate channels.");

            static_assert(!group_info<Group>::streaming
                    || group_info<Group>::candidate_count < 2
                    || group_info<Group>::checksummed,
                "ecomm::router: when a streaming channel carries more than one packet "
                "type, every candidate must use a checksum (ChecksumPolicy != none). A "
                "checksum-less packet always 'validates', so a torn or misframed read "
                "would be dispatched as genuine. Use a single packet type per channel to "
                "keep ChecksumPolicy = none.");

            Group group;
            std::array<std::byte, group_info<Group>::buffer_size> buffer{};
            std::size_t fill = 0;
        };

    } // namespace details

    /**
    * @brief Bind a set of packet handlers to one specific channel, for
    *        constructing an `ecomm::router`.
    *
    * ```cpp
    * ecomm::router r{
    *     on_channel(wifi_link,
    *         [](telemetry_packet& p) { ... },
    *         [](command_packet& p)   { ... }
    *     ),
    *     on_channel(serial_link,
    *         [](command_packet& p)   { ... }
    *     )
    * };
    * ```
    * `Channel` is deduced from `channel` -- no explicit template argument is
    * needed.
    *
    * @tparam Channel  Deduced from `channel`.
    * @tparam Handlers Deduced from `handlers`.
    * @param[in] channel  The channel this group applies to. Referenced, not
    *                     copied; must outlive the router.
    * @param[in] handlers One callable per packet type this channel should be
    *                     probed for, each taking a single `Packet&`.
    */
    template<typename Channel, typename... Handlers>
    [[nodiscard]] constexpr channel_handlers<Channel, std::decay_t<Handlers>...>
    on_channel(Channel& channel, Handlers&&... handlers) noexcept {
        return {channel, std::tuple<std::decay_t<Handlers>...>{std::forward<Handlers>(handlers)...}};
    }

    /**
    * @class router
    *
    * @brief Heterogeneous packet dispatch with byte-level reassembly: poll a
    *        set of channels for whichever packet type arrives first, and route
    *        it to the handler for that type -- without a partially-arrived
    *        packet ever being misframed as a smaller one.
    *
    * See the file-level documentation above for the full design -- candidate
    * types read from handler signatures, per-channel reassembly of streaming
    * transports, automatic largest-first prefix framing, and the two
    * compile-time guardrails.
    *
    * Constructed from one `on_channel(...)` group per channel and polled with
    * the nullary `try_receive_any()`; it holds a reassembly buffer per
    * streaming channel, so it must outlive a single poll (unlike a temporary).
    *
    * @tparam Groups Deduced from the `on_channel(...)` arguments -- each must be
    *                a `channel_handlers` produced by `on_channel`.
    */
    template<typename... Groups>
    class router {
        static_assert(sizeof...(Groups) >= 1,
            "ecomm::router: at least one on_channel(...) group is required.");
        static_assert((details::is_channel_handlers_v<Groups> and ...),
            "ecomm::router: every constructor argument must be an "
            "on_channel(channel, handlers...) group.");

    public:
        /**
        * @brief Construct a router from one `on_channel(...)` group per channel.
        *
        * @param[in] groups The `on_channel(channel, handlers...)` groups. Their
        *                   channels are referenced and must outlive the router.
        */
        explicit router(Groups... groups) noexcept
            : _slots{ details::group_slot<Groups>{ std::move(groups) }... }
        {}

        /**
        * @brief Poll each channel once and dispatch at most one ready packet.
        *
        * For each group in declaration order: a streaming channel is topped up
        * via `receive_raw` and its buffer is framed largest-first; a
        * message-atomic channel is polled directly. Stops at the first group
        * that dispatches a packet.
        *
        * @return `true` if a packet was consumed (and dispatched to its handler,
        *         unless it was addressed to another node), `false` if no channel
        *         had a complete packet ready. Suitable for draining:
        *         `while (r.try_receive_any()) {}`.
        */
        [[nodiscard]] bool try_receive_any() noexcept;

    private:
        /// @brief Poll one group, dispatching to the streaming or atomic path.
        template<typename Slot>
        bool poll_slot(Slot& slot) noexcept;

        /// @brief Streaming path: top up the reassembly buffer, then frame it.
        template<typename Slot>
        bool poll_streaming(Slot& slot) noexcept;

        /// @brief Frame the buffered bytes, testing candidate prefixes largest-first.
        template<typename Slot>
        bool try_frame_prefix(Slot& slot) noexcept;

        /// @brief Fold over the size-sorted candidates for the streaming path.
        template<typename Slot, typename HandlerTuple, typename... Sorted>
        bool try_frame_all(Slot& slot, HandlerTuple& handlers,
                           etools::meta::typelist<Sorted...>) noexcept;

        /// @brief Test the leading `sizeof(Packet)` buffered bytes as `Packet`;
        ///        on a valid frame, consume it (and dispatch if addressed to us).
        template<typename Packet, typename Slot, typename HandlerTuple>
        bool try_frame(Slot& slot, HandlerTuple& handlers) noexcept;

        /// @brief Message-atomic path: poll `channel.try_receive()` directly.
        template<typename Slot>
        bool poll_atomic(Slot& slot) noexcept;

        /// @brief Fold over the size-sorted candidates for the atomic path.
        template<typename Channel, typename HandlerTuple, typename... Sorted>
        bool try_atomic_all(Channel& channel, HandlerTuple& handlers,
                            etools::meta::typelist<Sorted...>) noexcept;

        /// @brief Probe one atomic `Packet`; on a hit, dispatch it.
        template<typename Packet, typename Channel, typename HandlerTuple>
        bool try_atomic(Channel& channel, HandlerTuple& handlers) noexcept;

        /// @brief Invoke the one handler in `handlers` declaring `Packet`.
        template<typename Packet, typename HandlerTuple>
        void dispatch(HandlerTuple& handlers, Packet& packet) noexcept;

        std::tuple<details::group_slot<Groups>...> _slots;
    };

    /// @brief Deduce `router`'s `Groups...` from its `on_channel(...)` arguments.
    template<typename... Groups>
    router(Groups...) -> router<Groups...>;

} // namespace ecomm

#include "router.tpp"
#endif // ECOMM_FABRIC_ROUTER_HPP_
