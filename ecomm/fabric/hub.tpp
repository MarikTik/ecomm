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
*      array's indexing.
* - 2026-07-16 Switched the per-channel filter from `Channel::packet_t == Packet`
*      to `channels::details::is_channel_like_v<Channel, Packet>` (capability-based,
*      see hub.hpp). `try_receive`'s inner call became
*      `channels::details::invoke_try_receive<Packet>(channel)`, dispatching to
*      whichever calling convention `Channel` actually has (`try_receive()` or
*      `try_receive<Packet>()`).
* - 2026-07-17 Replaced `use_sender`/`use_receiver`/`remove_sender`/`remove_receiver`
*      with a single `set_role<Channel>(role)`, assigning both flags from one
*      explicit state instead of toggling them independently.
* - 2026-07-19 `try_receive_any`/`on_channel` and their supporting machinery moved out
*      to `ecomm::dispatcher` (`hub/dispatcher.hpp`) -- a different responsibility
*      from this file's explicit, caller-known-`Packet` `send`/`try_receive`.
*      `hub_role` moved to `ecomm::channels::role` (`channels/role.hpp`).
* - 2026-07-20 Moved from `ecomm/hub/` to `ecomm/fabric/` alongside `dispatcher`
*      being renamed to `ecomm::router` -- see hub.hpp's changelog for why.
*/
#ifndef ECOMM_FABRIC_HUB_TPP_
#define ECOMM_FABRIC_HUB_TPP_
#include "hub.hpp"

namespace ecomm {

    template<typename... Channels>
    hub<Channels...>::hub(Channels&... channels) noexcept
        : _channels{channels...}
    {
        (set_role<Channels>(channels::role::transceiver), ...);
    }

    template<typename... Channels>
    template<typename Channel>
    void hub<Channels...>::set_role(channels::role role) noexcept {
        switch (role) {
            case channels::role::sender:
                _sender_statuses.template set<Channel>();
                _receiver_statuses.template reset<Channel>();
                break;
            case channels::role::receiver:
                _sender_statuses.template reset<Channel>();
                _receiver_statuses.template set<Channel>();
                break;
            case channels::role::transceiver:
                _sender_statuses.template set<Channel>();
                _receiver_statuses.template set<Channel>();
                break;
            case channels::role::none:
                _sender_statuses.template reset<Channel>();
                _receiver_statuses.template reset<Channel>();
                break;
        }
    }

    template<typename... Channels>
    template<typename Packet>
    typename hub<Channels...>::template send_results_t<Packet>
    hub<Channels...>::send(Packet& packet) noexcept {
        static_assert(channels::details::count_matching_v<Packet, Channels...> >= 1,
            "ecomm::hub::send<Packet>: no channel in this hub can currently handle Packet.");

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
        static_assert(channels::details::count_matching_v<Packet, Channels...> >= 1,
            "ecomm::hub::try_receive<Packet>: no channel in this hub can currently handle Packet.");

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
    template<typename Packet, typename Channel>
    void hub<Channels...>::maybe_send_if_matching(
        Channel& channel,
        Packet& packet,
        send_results_t<Packet>& results,
        std::size_t& next
    ) noexcept {
        if constexpr (channels::details::is_channel_like_v<Channel, Packet>) {
            // The slot must be claimed once per Packet-matching channel
            // regardless of active-sender status, so a later channel's slot
            // never shifts left just because an earlier matching channel
            // happened to be inactive -- results stays positional against
            // Channels... declaration order among the matching subset.
            auto& slot = results[next++];
            if (_sender_statuses.template test<Channel>())
                slot = channel.send(packet);
        }
    }

    template<typename... Channels>
    template<typename Packet, typename Channel>
    bool hub<Channels...>::try_receive_if_matching(Channel& channel, std::optional<Packet>& result) noexcept {
        if constexpr (channels::details::is_channel_like_v<Channel, Packet>) {
            if (_receiver_statuses.template test<Channel>()) {
                auto maybe = channels::details::invoke_try_receive<Packet>(channel);
                if (maybe) {
                    result = std::move(maybe);
                    return true;
                }
            }
        }
        return false;
    }

} // namespace ecomm

#endif // ECOMM_FABRIC_HUB_TPP_
