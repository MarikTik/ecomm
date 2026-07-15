// SPDX-License-Identifier: MIT
/**
* @file hub.tpp
*
* @brief Implementation of `ecomm::hub`'s methods.
*
* @ingroup ecomm
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
* - 2025-07-21 Corrected template argument deduction in `maybe_try_receive`.
* - 2026-07-14 Full rewrite against the current `channel<Impl, Packet>` /
*      `reliable_channel<...>` API. `_channels` is now a tuple of references
*      (nothing is moved); `send()` collects a `std::optional<send_result>`
*      per channel via `std::apply` + a left-to-right fold instead of
*      discarding every result.
* - 2026-07-15 Rewritten for the heterogeneous `hub<Channels...>` (no class-level
*      `Packet`). `send<Packet>`/`try_receive<Packet>` now filter `Channels...` by
*      `packet_t` via `if constexpr` inside per-channel visitor helpers, so a
*      non-matching channel is skipped both at compile time and in the output
*      array's indexing. Added `try_receive_any()`, polling every active receiver
*      regardless of packet type and returning the first hit as `any_packet_t`.
*/
#ifndef ECOMM_HUB_HUB_TPP_
#define ECOMM_HUB_HUB_TPP_
#include "hub.hpp"

namespace ecomm {

    template<typename... Channels>
    hub<Channels...>::hub(Channels&... channels) noexcept
        : _channels{channels...}
    {
        (use_sender<Channels>(), ...);
        (use_receiver<Channels>(), ...);
    }

    template<typename... Channels>
    template<typename Channel>
    void hub<Channels...>::use_sender() noexcept {
        _sender_statuses.template set<Channel>();
    }

    template<typename... Channels>
    template<typename Channel>
    void hub<Channels...>::use_receiver() noexcept {
        _receiver_statuses.template set<Channel>();
    }

    template<typename... Channels>
    template<typename Channel>
    void hub<Channels...>::remove_sender() noexcept {
        _sender_statuses.template reset<Channel>();
    }

    template<typename... Channels>
    template<typename Channel>
    void hub<Channels...>::remove_receiver() noexcept {
        _receiver_statuses.template reset<Channel>();
    }

    template<typename... Channels>
    template<typename Packet>
    typename hub<Channels...>::template send_results_t<Packet>
    hub<Channels...>::send(Packet& packet) noexcept {
        static_assert(details::count_matching_v<Packet, typename Channels::packet_t...> >= 1,
            "ecomm::hub::send<Packet>: no channel in this hub operates on Packet.");

        send_results_t<Packet> results{};
        std::size_t next = 0;

        // The comma-operator fold guarantees left-to-right evaluation, so `next`
        // only advances for Packet-matching channels, in declaration order among
        // that subset -- the same order `results` uses.
        std::apply(
            [this, &packet, &results, &next](auto&... channels) {
                (maybe_send_if_matching(channels, packet, results, next), ...);
            },
            _channels
        );

        return results;
    }

    template<typename... Channels>
    template<typename Packet>
    std::optional<Packet> hub<Channels...>::try_receive() noexcept {
        static_assert(details::count_matching_v<Packet, typename Channels::packet_t...> >= 1,
            "ecomm::hub::try_receive<Packet>: no channel in this hub operates on Packet.");

        std::optional<Packet> result;

        std::apply(
            [this, &result](auto&... channels) {
                // Short-circuiting `or` fold: stop at the first Packet-matching
                // channel that yields a packet. Non-matching channels are
                // filtered out inside try_receive_if_matching and never polled.
                (... or try_receive_if_matching(channels, result));
            },
            _channels
        );

        return result;
    }

    template<typename... Channels>
    std::optional<typename hub<Channels...>::any_packet_t> hub<Channels...>::try_receive_any() noexcept {
        std::optional<any_packet_t> result;

        std::apply(
            [this, &result](auto&... channels) {
                (... or ([this, &result, &channels] {
                    auto maybe = maybe_try_receive(channels);
                    if (maybe) {
                        result = any_packet_t{std::move(*maybe)};
                        return true;
                    }
                    return false;
                }()));
            },
            _channels
        );

        return result;
    }

    template<typename... Channels>
    template<typename Channel>
    void hub<Channels...>::maybe_send(
        Channel& channel,
        typename Channel::packet_t& packet,
        std::optional<channels::send_result>& out
    ) noexcept {
        if (_sender_statuses.template test<Channel>())
            out = channel.send(packet);
    }

    template<typename... Channels>
    template<typename Channel>
    std::optional<typename Channel::packet_t> hub<Channels...>::maybe_try_receive(Channel& channel) noexcept {
        if (_receiver_statuses.template test<Channel>())
            return channel.try_receive();
        return std::nullopt;
    }

    template<typename... Channels>
    template<typename Packet, typename Channel>
    void hub<Channels...>::maybe_send_if_matching(
        Channel& channel,
        Packet& packet,
        send_results_t<Packet>& results,
        std::size_t& next
    ) noexcept {
        if constexpr (std::is_same_v<typename Channel::packet_t, Packet>)
            maybe_send(channel, packet, results[next++]);
    }

    template<typename... Channels>
    template<typename Packet, typename Channel>
    bool hub<Channels...>::try_receive_if_matching(Channel& channel, std::optional<Packet>& result) noexcept {
        if constexpr (std::is_same_v<typename Channel::packet_t, Packet>) {
            auto maybe = maybe_try_receive(channel);
            if (maybe) {
                result = std::move(maybe);
                return true;
            }
        }
        return false;
    }

} // namespace ecomm

#endif // ECOMM_HUB_HUB_TPP_
