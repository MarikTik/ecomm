// SPDX-License-Identifier: MIT
/**
* @file hub.hpp
*
* @brief Defines `ecomm::hub`, a compile-time aggregate of several communication channels.
*
* @ingroup ecomm
*
* A `hub<Channels...>` combines several already-constructed channels -- any
* mix of `ecomm::channels::channel<Impl, Packet>` and
* `ecomm::channels::reliable_channel<Impl, Packet, ...>` instances -- behind
* one API. Think of it as a USB hub: several physical links (a UART leaf
* link, a Wi-Fi link, ...) plugged into one place, individually enabled or
* disabled at runtime.
*
* @par Channels may operate on different Packet types
* Unlike a single `channel<Impl, Packet>`, `hub` does not require every
* member to share one packet type. A coordinator's tight UART link to a
* robot's own actuators and its looser Wi-Fi link to the outside world
* routinely want different packet sizes -- a small, low-latency packet on
* the leaf link, a larger one carrying richer telemetry on the uplink. `hub`
* accepts both in one object:
* ```cpp
* hub h{robot_serial_link, world_wifi_link};   // different packet_t each
* h.send(small_packet_instance);               // Packet deduced -- reaches only matching channels
* auto p = h.try_receive<small_packet>();       // Packet NOT deducible here; must be named
* auto any = h.try_receive_any();               // polls every channel; returns a variant
* ```
* `send` deduces `Packet` from its argument like any function template, so
* it reads exactly like the homogeneous case. `try_receive<Packet>` cannot
* -- there is no argument to deduce a return type from, which is a hard
* rule of C++ template argument deduction, not a design choice `hub` could
* avoid without reverting to an output-parameter style `channel<>`
* deliberately moved away from (see `channel.hpp`'s 2026-05-27 changelog
* entry). `try_receive_any()` is the way to poll without already knowing
* which packet type is about to arrive -- see below.
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
* @par Compile-time validation
* Two `static_assert`s fire at `hub<...>`'s own instantiation point (not deep
* inside a call site) if misused:
* - Every `Channel` in `Channels...` must be "channel-like" for its own
*   `packet_t`: it must expose `using packet_t = SomePacket;` and the
*   `send`/`try_receive` signatures `channel<Impl, Packet>` and
*   `reliable_channel<...>` both already have. This is duck-typed, not
*   `is_base_of`-checked, because `reliable_channel` is a standalone class,
*   not derived from `channel<>`.
* - Every type in `Channels...` must be pairwise distinct (`hub` indexes its
*   internal per-channel enable/disable flags by type via
*   `etools::meta::typeset`). Use each channel's `tag` template parameter to
*   disambiguate two instances of the same transport, e.g.
*   `arduino_serial_channel<Packet, 0>` and `arduino_serial_channel<Packet, 1>`.
*
* A third check is per-call rather than class-wide: `send<Packet>` and
* `try_receive<Packet>` each `static_assert` that at least one channel in
* `Channels...` actually operates on the requested `Packet` -- naming a
* `Packet` no channel in this `hub` supports is a compile error, not a
* silent no-op.
*
* @par send() aggregates per-channel results
* `hub::send<Packet>()` fans the packet out to every *active* sender whose
* `packet_t` matches `Packet`, and returns one `std::optional<send_result>`
* per matching channel (in `Channels...` declaration order among that
* subset; disengaged for a matching channel that was not an active sender).
* This is what lets a caller distinguish "channel 2 timed out" from
* "everything worked" when the hub contains a `reliable_channel`.
*
* @par A hub that mixes blocking and non-blocking channels blocks as a whole
* `reliable_channel::send` busy-polls for an acknowledgement (see
* `reliable_channel.hpp`'s `@par BLOCKING`). `hub::send()` calls every active,
* matching sender in sequence, so a `reliable_channel` member's worst-case
* retry/timeout window delays every other matching channel's send in the
* same call. This is inherent to combining a blocking and a non-blocking
* transport in one fan-out call, not something `hub` can hide.
*
* @par try_receive_any(): polling without knowing the type in advance
* `try_receive_any()` polls every active receiver regardless of its
* `packet_t` and returns `std::optional<any_packet_t>`, where `any_packet_t`
* is a `std::variant` over the *distinct* packet types in `Channels...`
* (channels that happen to share a packet type collapse to one alternative).
* Consume it with `std::visit` and the `etools::meta::overload` helper:
* ```cpp
* if (auto pkt = h.try_receive_any()) {
*     std::visit(etools::meta::overload{
*         [](small_packet& p) { ... },
*         [](big_packet& p)   { ... },
*     }, *pkt);
* }
* ```
*
* @note Class template argument deduction works without an explicit guide:
*       `Channels...` is deduced directly from the constructor arguments.
*       ```cpp
*       ecomm::hub h{serial_channel, wifi_channel};
*       ```
*
* @example Example usage:
* @code
* ecomm::hub h{serial_channel, wifi_channel};   // different packet_t on each
*
* small_packet p = ...;
* auto results = h.send(p);                     // Packet deduced; only serial_channel matches
* auto maybe    = h.try_receive<small_packet>(); // must name Packet -- nothing to deduce from
* auto any      = h.try_receive_any();           // polls both; std::optional<std::variant<...>>
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
*      deduce a return type from) and must name `Packet` explicitly. Added `try_receive_any()`,
*      returning `std::optional<std::variant<...>>` over the distinct packet types in `Channels...`,
*      for polling without knowing in advance which type is about to arrive; consumed via
*      `etools::meta::overload` (etools v1.0.4) with `std::visit`.
* - 2026-07-15 Switched `any_packet_t` from a locally-defined dedup builder to
*      `etools::meta::unique_variant_t` (etools v1.0.5) -- the same "possibly-repeating pack ->
*      std::variant of only the distinct types" logic, promoted to etools since it has nothing
*      ecomm-specific about it.
*/
#ifndef ECOMM_HUB_HUB_HPP_
#define ECOMM_HUB_HUB_HPP_
#include <array>
#include <optional>
#include <tuple>
#include <type_traits>

#include <etools/meta/overload.hpp>
#include <etools/meta/traits.hpp>
#include <etools/meta/typeset.hpp>
#include <etools/meta/unique_variant.hpp>

#include "../channels/send_result.hpp"

namespace ecomm::details {

    /**
    * @brief `true` iff `T` behaves like a channel operating on `Packet`.
    *
    * "Behaves like" means: exposes `using packet_t = Packet;`, and provides
    * `send(Packet&)` returning something convertible to
    * `ecomm::channels::send_result` and `try_receive()` returning exactly
    * `std::optional<Packet>`. Both `ecomm::channels::channel<Impl, Packet>`
    * and `ecomm::channels::reliable_channel<Impl, Packet, ...>` satisfy this
    * without being related by inheritance, which is why this is a
    * duck-typed trait rather than an `is_base_of` check.
    *
    * @tparam T      The candidate channel type.
    * @tparam Packet The packet type it must operate on.
    */
    template<typename T, typename Packet, typename = void>
    struct is_channel_like : std::false_type {};

    /// @brief Specialization selected when every required member is well-formed.
    template<typename T, typename Packet>
    struct is_channel_like<T, Packet, std::void_t<
        typename T::packet_t,
        decltype(std::declval<T&>().send(std::declval<Packet&>())),
        decltype(std::declval<T&>().try_receive())
    >> : std::bool_constant<
        std::is_same_v<typename T::packet_t, Packet> and
        std::is_convertible_v<
            decltype(std::declval<T&>().send(std::declval<Packet&>())),
            channels::send_result
        > and
        std::is_same_v<
            decltype(std::declval<T&>().try_receive()),
            std::optional<Packet>
        >
    > {};

    /// @brief Convenience variable template for `is_channel_like`.
    template<typename T, typename Packet>
    inline constexpr bool is_channel_like_v = is_channel_like<T, Packet>::value;

    /**
    * @brief `true` iff `T` is channel-like for its own `T::packet_t`.
    *
    * The self-checking counterpart to `is_channel_like<T, Packet>`, used by
    * `hub`'s validation once `Packet` is no longer a class-wide parameter:
    * each channel only needs to be internally consistent, not agree with
    * some external type. SFINAE-safe when `T` has no `packet_t` at all
    * (falls back to `false`, not a hard error).
    */
    template<typename T, typename = void>
    struct is_self_channel_like : std::false_type {};

    template<typename T>
    struct is_self_channel_like<T, std::void_t<typename T::packet_t>>
        : std::bool_constant<is_channel_like_v<T, typename T::packet_t>> {};

    template<typename T>
    inline constexpr bool is_self_channel_like_v = is_self_channel_like<T>::value;

    /// @brief Number of types in `Ts...` equal to `Packet`.
    template<typename Packet, typename... Ts>
    inline constexpr std::size_t count_matching_v = (0 + ... + (std::is_same_v<Ts, Packet> ? 1 : 0));

} // namespace ecomm::details

namespace ecomm {

    /**
    * @class hub
    *
    * @brief Combines several channels -- possibly operating on different
    *        packet types -- behind one API.
    *
    * See the file-level documentation above for the ownership model, the
    * compile-time validations, how `send<Packet>()`'s aggregated result
    * differs from a single channel's, and `try_receive_any()`.
    *
    * @tparam Channels A pack of channel-like types (see
    *                  `details::is_self_channel_like`), pairwise distinct,
    *                  each referencing a channel the caller owns and keeps
    *                  alive for at least as long as this `hub`. Channels may
    *                  operate on different packet types.
    */
    template<typename... Channels>
    class hub {
        static_assert(sizeof...(Channels) >= 1,
            "ecomm::hub: at least one channel is required.");
        static_assert(etools::meta::is_distinct_v<Channels...>,
            "ecomm::hub: all Channels must be pairwise distinct types. Use each "
            "channel's `tag` template parameter to disambiguate multiple instances "
            "of the same transport (e.g. arduino_serial_channel<Packet, 0> and "
            "arduino_serial_channel<Packet, 1>).");
        static_assert((details::is_self_channel_like_v<Channels> and ...),
            "ecomm::hub: every Channel must expose `using packet_t = /* some type */;` "
            "and provide `channels::send_result send(packet_t&) noexcept` and "
            "`std::optional<packet_t> try_receive() noexcept` -- i.e. behave like "
            "ecomm::channels::channel<Impl, Packet> or "
            "ecomm::channels::reliable_channel<Impl, Packet, ...>.");

    public:
        /**
        * @brief Per-channel outcome of a fan-out `send<Packet>()`, in the
        *        declaration order of the `Channels...` subset whose
        *        `packet_t` is `Packet`.
        *
        * Slot `i` is engaged with the `send_result` returned by the `i`-th
        * `Packet`-matching channel if it was an active sender at the time
        * of the call, or disengaged if it was not (and therefore was not
        * sent to at all). Sized to exactly the number of matching channels
        * -- a channel operating on a different packet type has no slot at
        * all, rather than a permanently-disengaged one.
        */
        template<typename Packet>
        using send_results_t = std::array<std::optional<channels::send_result>, details::count_matching_v<Packet, typename Channels::packet_t...>>;

        /**
        * @brief A `std::variant` over the distinct packet types in `Channels...`.
        *
        * See `try_receive_any()`.
        */
        using any_packet_t = etools::meta::unique_variant_t<typename Channels::packet_t...>;

        /**
        * @brief Bind a hub to already-constructed channels, enabling all of them.
        *
        * `hub` stores references, not copies -- see the ownership note in this
        * file's documentation. Every channel starts enabled for both sending
        * and receiving; use `remove_sender`/`remove_receiver` to opt one out.
        *
        * @param[in] channels One lvalue reference per type in `Channels...`,
        *                     in the same order. Each must outlive this `hub`.
        */
        explicit hub(Channels&... channels) noexcept;

        /// @brief Enable `Channel` as an active sender (default: already enabled).
        template<typename Channel>
        void use_sender() noexcept;

        /// @brief Enable `Channel` as an active receiver (default: already enabled).
        template<typename Channel>
        void use_receiver() noexcept;

        /// @brief Disable `Channel` as a sender; `send()` will skip it.
        template<typename Channel>
        void remove_sender() noexcept;

        /// @brief Disable `Channel` as a receiver; `try_receive()`/`try_receive_any()` will skip it.
        template<typename Channel>
        void remove_receiver() noexcept;

        /**
        * @brief Send a packet through every active sender channel whose
        *        `packet_t` matches `Packet`.
        *
        * `Packet` is deduced from `packet`'s type -- no explicit template
        * argument is needed, exactly as for a single `channel<>::send`.
        * `static_assert`s that at least one channel in `Channels...`
        * actually operates on `Packet`.
        *
        * Calls `send(packet)` on each matching channel currently enabled
        * via `use_sender`, in declaration order, unconditionally (a failure
        * on one channel does not skip the rest). See the file-level `@par`
        * on mixing blocking and non-blocking channels for why this call's
        * worst-case latency is the sum of its matching members'.
        *
        * @tparam Packet Deduced from `packet`.
        * @param[in,out] packet Packet to send. Each active matching channel
        *                       may modify its FCS field via its own sealing step.
        *
        * @return One `std::optional<send_result>` per `Packet`-matching
        *         channel, in declaration order among that subset;
        *         disengaged for ones that were not active senders.
        */
        template<typename Packet>
        [[nodiscard]] send_results_t<Packet> send(Packet& packet) noexcept;

        /**
        * @brief Attempt to receive a `Packet` from any active receiver
        *        channel whose `packet_t` is `Packet`.
        *
        * Unlike `send`, `Packet` cannot be deduced here -- there is no
        * argument to deduce a return type from, a hard rule of C++
        * template argument deduction -- so it must be named explicitly:
        * `hub.try_receive<small_packet>()`. `static_assert`s that at least
        * one channel in `Channels...` actually operates on `Packet`.
        *
        * Polls each `Packet`-matching channel currently enabled via
        * `use_receiver`, in declaration order among that subset, and
        * returns the first packet found. Channels after the first hit are
        * not polled on this call.
        *
        * @tparam Packet Must be named explicitly.
        * @return The first available packet, or `std::nullopt` if no active
        *         matching receiver had one.
        */
        template<typename Packet>
        [[nodiscard]] std::optional<Packet> try_receive() noexcept;

        /**
        * @brief Attempt to receive a packet of any type from any active
        *        receiver channel.
        *
        * Use this when you do not know in advance which packet type is
        * about to arrive -- e.g. a coordinator servicing a small-packet
        * leaf link and a large-packet uplink from one poll call. Polls
        * every active receiver channel, in `Channels...` declaration
        * order, and returns the first packet found, wrapped in
        * `any_packet_t`. Consume the result with `std::visit` and
        * `etools::meta::overload` (see the file-level example).
        *
        * @return The first available packet across every active receiver,
        *         or `std::nullopt` if none had one.
        */
        [[nodiscard]] std::optional<any_packet_t> try_receive_any() noexcept;

    private:
        /// @brief Send to a single channel if it is an active sender; record the result.
        template<typename Channel>
        void maybe_send(Channel& channel, typename Channel::packet_t& packet, std::optional<channels::send_result>& out) noexcept;

        /// @brief Poll a single channel if it is an active receiver.
        template<typename Channel>
        std::optional<typename Channel::packet_t> maybe_try_receive(Channel& channel) noexcept;

        /// @brief `send<Packet>`'s per-channel visitor: no-ops for a non-matching Channel.
        template<typename Packet, typename Channel>
        void maybe_send_if_matching(Channel& channel, Packet& packet, send_results_t<Packet>& results, std::size_t& next) noexcept;

        /// @brief `try_receive<Packet>`'s per-channel visitor: no-ops for a non-matching Channel.
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
#endif // ECOMM_HUB_HUB_HPP_
