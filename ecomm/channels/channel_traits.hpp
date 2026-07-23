// SPDX-License-Identifier: MIT
/**
* @file channel_traits.hpp
*
* @brief Structural ("does it behave like a channel") introspection over
*        `channel<Impl>`-derived and `reliable_channel`-shaped types.
*
* @ingroup ecomm_channels ecomm::channels
*
* Shared by `ecomm::hub` and `ecomm::router`: both need to answer "can
* this channel currently handle this `Packet`" without requiring a common
* base class (there isn't one -- `reliable_channel` does not derive from
* `channel<Impl>`). Extracted here rather than duplicated between the two,
* since both `hub.hpp` and `router.hpp` need every trait in this file.
*
* @par The two channel shapes
* - **Fixed-Packet shape** (e.g. `reliable_channel`): ordinary, non-template
*   `send(Packet&)` and `try_receive()` returning exactly
*   `std::optional<Packet>`. `Packet` is baked into the type itself.
* - **Templated-Packet shape** (e.g. `channel<Impl>`-derived types):
*   `send(Packet&)` deducible from the argument, `try_receive<Packet>()`
*   requiring `Packet` named explicitly (there is nothing to deduce a return
*   type from). One instance can be probed for any number of `Packet` types.
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
* - 2026-07-19 Extracted from `ecomm/hub/hub.hpp`'s `ecomm::details` namespace
*      (moved to `ecomm::channels::details`, matching this file's new location)
*      when `hub` was split into `hub` (explicit-`Packet` fan-out/poll) and
*      `dispatcher` (heterogeneous-`Packet` dispatch), both of which need this
*      capability-checking machinery.
* - 2026-07-20 `dispatcher` renamed to `router` (see `fabric/hub.hpp`'s changelog).
* - 2026-07-21 Added `has_receive_raw` / `has_receive_raw_v` (detects the raw,
*      byte-granular `receive_raw` a streaming channel exposes) and
*      `is_streaming_channel_v` (a `channel<Impl>`-derived channel, as opposed to
*      the message-atomic `reliable_channel`). `router` uses both to decide which
*      channels need byte-level reassembly to avoid misframing a partially-arrived
*      larger packet as a smaller candidate -- see `fabric/router.hpp`'s changelog.
*/
#ifndef ECOMM_CHANNELS_CHANNEL_TRAITS_HPP_
#define ECOMM_CHANNELS_CHANNEL_TRAITS_HPP_

#include <cstddef>
#include <optional>
#include <type_traits>

#include "channel.hpp"
#include "send_result.hpp"

namespace ecomm::channels::details {

    /**
    * @brief `true` iff `T` behaves like a channel with a fixed packet type,
    *        for `Packet` specifically -- ordinary (non-template)
    *        `send(Packet&)` and `try_receive()` returning exactly
    *        `std::optional<Packet>`. Matches `reliable_channel`'s shape.
    *
    * @tparam T      The candidate channel type.
    * @tparam Packet The packet type it must operate on.
    */
    template<typename T, typename Packet, typename = void>
    struct has_fixed_packet_shape : std::false_type {};

    /// @brief Specialization selected when every required member is well-formed.
    template<typename T, typename Packet>
    struct has_fixed_packet_shape<T, Packet, std::void_t<
        decltype(std::declval<T&>().send(std::declval<Packet&>())),
        decltype(std::declval<T&>().try_receive())
    >> : std::bool_constant<
        std::is_convertible_v<
            decltype(std::declval<T&>().send(std::declval<Packet&>())),
            send_result
        > and
        std::is_same_v<
            decltype(std::declval<T&>().try_receive()),
            std::optional<Packet>
        >
    > {};

    /**
    * @brief `true` iff `T` behaves like a channel with a per-call packet
    *        type, for `Packet` specifically -- `send(Packet&)` deducible
    *        from the argument, `try_receive<Packet>()` requiring `Packet`
    *        named explicitly. Matches `channel<Impl>`'s shape.
    *
    * @tparam T      The candidate channel type.
    * @tparam Packet The packet type to probe support for.
    */
    template<typename T, typename Packet, typename = void>
    struct has_templated_packet_shape : std::false_type {};

    /// @brief Specialization selected when every required member is well-formed.
    template<typename T, typename Packet>
    struct has_templated_packet_shape<T, Packet, std::void_t<
        decltype(std::declval<T&>().send(std::declval<Packet&>())),
        decltype(std::declval<T&>().template try_receive<Packet>())
    >> : std::bool_constant<
        std::is_convertible_v<
            decltype(std::declval<T&>().send(std::declval<Packet&>())),
            send_result
        > and
        std::is_same_v<
            decltype(std::declval<T&>().template try_receive<Packet>()),
            std::optional<Packet>
        >
    > {};

    /**
    * @brief `true` iff `T` can currently handle `Packet`, via either shape.
    *
    * A type can only ever genuinely satisfy one of the two (a `try_receive`
    * is either an ordinary method or a member template, never both at once
    * for the same call), so this is a safe, unambiguous either-or in
    * practice, not a real overlap.
    */
    template<typename T, typename Packet>
    inline constexpr bool is_channel_like_v =
        has_fixed_packet_shape<T, Packet>::value or has_templated_packet_shape<T, Packet>::value;

    /**
    * @brief `true` iff `T` is recognizable as *some* kind of channel -- a
    *        coarse sanity check, not a check against any specific `Packet`
    *        (a flexible channel has no single one).
    *
    * Two shapes are accepted: deriving from `channel<T>` (the flexible shape
    * every `channel<Impl>`-based concrete channel has), or exposing
    * `using packet_t = SomePacket;` and being self-consistent with the fixed
    * shape for that `packet_t` (matches `reliable_channel`, which does not
    * derive from `channel<>`).
    */
    template<typename T, typename = void>
    struct is_recognizable_fixed_channel : std::false_type {};

    template<typename T>
    struct is_recognizable_fixed_channel<T, std::void_t<typename T::packet_t>>
        : std::bool_constant<has_fixed_packet_shape<T, typename T::packet_t>::value> {};

    template<typename T>
    inline constexpr bool is_recognizable_channel_v =
        std::is_base_of_v<channel<T>, T> or is_recognizable_fixed_channel<T>::value;

    /// @brief Number of `Channels...` that can currently handle `Packet`.
    template<typename Packet, typename... Channels>
    inline constexpr std::size_t count_matching_v = (0 + ... + (is_channel_like_v<Channels, Packet> ? 1 : 0));

    /**
    * @brief `true` iff `T` exposes a raw, byte-granular receive:
    *        `std::size_t receive_raw(std::byte*, std::size_t)` -- consume up to
    *        the requested number of currently-buffered bytes with no typed
    *        framing or validation, returning how many were copied.
    *
    * This is what lets a caller do its own framing above the transport (see
    * `ecomm::router`'s reassembly path). It is provided by `channel<Impl>`-derived
    * types (streaming transports); `reliable_channel` deliberately does not have
    * it -- it is message-atomic and owns its own framing.
    */
    template<typename T, typename = void>
    struct has_receive_raw : std::false_type {};

    /// @brief Specialization selected when the raw receive member is well-formed.
    template<typename T>
    struct has_receive_raw<T, std::void_t<
        decltype(std::declval<T&>().receive_raw(std::declval<std::byte*>(), std::declval<std::size_t>()))
    >> : std::bool_constant<
        std::is_same_v<
            decltype(std::declval<T&>().receive_raw(std::declval<std::byte*>(), std::declval<std::size_t>())),
            std::size_t
        >
    > {};

    template<typename T>
    inline constexpr bool has_receive_raw_v = has_receive_raw<T>::value;

    /**
    * @brief `true` iff `T` is a streaming (`channel<Impl>`-derived) channel --
    *        one whose bytes arrive without framing, so a partially-arrived
    *        larger packet can be misframed as a smaller one on a byte-count
    *        probe. These are the channels `router` must reassemble for.
    *
    * `reliable_channel` is *not* a streaming channel by this test (it does not
    * derive from `channel<>`): it delivers whole, already-framed packets, so
    * `router` polls it directly via `try_receive()` with no reassembly.
    */
    template<typename T>
    inline constexpr bool is_streaming_channel_v = std::is_base_of_v<channel<T>, T>;

    /**
    * @brief Calls `channel.try_receive<Packet>()` or `channel.try_receive()`,
    *        whichever calling convention `Channel` actually has.
    *
    * `try_receive` cannot deduce `Packet` the way `send` deduces it from an
    * argument -- there is nothing to deduce a return type from -- so the two
    * shapes need genuinely different call syntax: an explicit `<Packet>` for
    * the templated shape, none at all for the fixed shape (which would not
    * even compile with one, `try_receive` not being a template there).
    */
    template<typename Packet, typename Channel>
    std::optional<Packet> invoke_try_receive(Channel& channel) noexcept {
        if constexpr (has_templated_packet_shape<Channel, Packet>::value) {
            return channel.template try_receive<Packet>();
        } else {
            return channel.try_receive();
        }
    }

} // namespace ecomm::channels::details

#endif // ECOMM_CHANNELS_CHANNEL_TRAITS_HPP_
