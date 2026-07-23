// SPDX-License-Identifier: MIT
/**
* @file router.tpp
*
* @brief Implementation of `ecomm::router`'s methods.
*
* @ingroup ecomm
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
* - 2026-07-19 Initial creation, split out of `ecomm::hub`'s `hub.tpp`. See
*      `router.hpp`'s changelog for what changed in the split.
* - 2026-07-20 Renamed from `dispatcher.tpp` alongside `ecomm::dispatcher` becoming
*      `ecomm::router`.
* - 2026-07-21 Rewritten for the stateful, reassembling `router`: streaming channels
*      are framed byte-by-byte over `channel::receive_raw` (pull into the per-channel
*      buffer, validate a candidate prefix before consuming it, keep incomplete bytes
*      for the next poll, resync by dropping a byte when the buffer is full and
*      unframable); message-atomic channels keep the direct `try_receive()` probe.
*      See `router.hpp`'s changelog for why.
*/
#ifndef ECOMM_FABRIC_ROUTER_TPP_
#define ECOMM_FABRIC_ROUTER_TPP_
#include "router.hpp"

#include <cstring>

namespace ecomm {

    template<typename... Groups>
    bool router<Groups...>::try_receive_any() noexcept {
        // Short-circuiting `or` fold over each group's slot in declaration
        // order; stop at the first that dispatches a packet.
        return std::apply(
            [this](auto&... slots) {
                return (... or this->poll_slot(slots));
            },
            _slots
        );
    }

    template<typename... Groups>
    template<typename Slot>
    bool router<Groups...>::poll_slot(Slot& slot) noexcept {
        if constexpr (details::group_info<typename Slot::group_type>::streaming) {
            return poll_streaming(slot);
        } else {
            return poll_atomic(slot);
        }
    }

    // -------------------------------------------------------------------------
    // Streaming path: reassemble over channel::receive_raw
    // -------------------------------------------------------------------------

    template<typename... Groups>
    template<typename Slot>
    bool router<Groups...>::poll_streaming(Slot& slot) noexcept {
        constexpr std::size_t cap = std::tuple_size_v<decltype(slot.buffer)>;

        for (;;) {
            // Top up the reassembly buffer with whatever bytes are ready now.
            if (slot.fill < cap) {
                slot.fill += slot.group.channel.receive_raw(
                    slot.buffer.data() + slot.fill, cap - slot.fill);
            }

            // Frame what we have: dispatch and consume the first candidate whose
            // prefix validates (tested largest-first).
            if (try_frame_prefix(slot)) return true;

            // Nothing framed. If the buffer is not yet full, the leading bytes
            // may be a still-arriving larger packet -- keep them for next poll.
            if (slot.fill < cap) return false;

            // Buffer is full and nothing framed: the leading byte cannot begin
            // any valid packet, so drop it and resync. Loop to re-test (and
            // top up the byte just freed).
            std::memmove(slot.buffer.data(), slot.buffer.data() + 1, cap - 1);
            slot.fill = cap - 1;
        }
    }

    template<typename... Groups>
    template<typename Slot>
    bool router<Groups...>::try_frame_prefix(Slot& slot) noexcept {
        return std::apply(
            [this, &slot](auto&... handlers) {
                using order = etools::meta::sort_t<etools::meta::size_greater,
                    details::handler_packet_t<decltype(handlers)>...>;
                return this->try_frame_all(slot, slot.group.handlers, order{});
            },
            slot.group.handlers
        );
    }

    template<typename... Groups>
    template<typename Slot, typename HandlerTuple, typename... Sorted>
    bool router<Groups...>::try_frame_all(
        Slot& slot,
        HandlerTuple& handlers,
        etools::meta::typelist<Sorted...>
    ) noexcept {
        // Short-circuiting `or` fold over the size-sorted candidates: stop at
        // the first whose leading bytes form a complete, valid packet.
        return (... or try_frame<Sorted>(slot, handlers));
    }

    template<typename... Groups>
    template<typename Packet, typename Slot, typename HandlerTuple>
    bool router<Groups...>::try_frame(Slot& slot, HandlerTuple& handlers) noexcept {
        if (slot.fill < sizeof(Packet)) return false;

        Packet packet{};
        std::memcpy(&packet, slot.buffer.data(), sizeof(Packet));
        if (not protocol::validator<Packet>{}.is_valid(packet)) return false;

        // A valid frame: consume it regardless of addressing (leaving a packet
        // for another node in the buffer would block it forever), but only
        // dispatch it when it is addressed to us -- mirroring the receiver_id
        // filter channel<Impl>::try_receive applies.
        bool deliver = true;
        if constexpr (Packet::header_t::has_node_ids) {
            const auto dest = packet.header.receiver_id;
            deliver = dest == static_cast<std::uint8_t>(ECOMM_BOARD_ID)
                   or dest == static_cast<std::uint8_t>(0xFFu);
        }
        if (deliver) dispatch<Packet>(handlers, packet);

        const std::size_t remainder = slot.fill - sizeof(Packet);
        std::memmove(slot.buffer.data(), slot.buffer.data() + sizeof(Packet), remainder);
        slot.fill = remainder;
        return true;
    }

    // -------------------------------------------------------------------------
    // Message-atomic path: poll try_receive() directly (e.g. reliable_channel)
    // -------------------------------------------------------------------------

    template<typename... Groups>
    template<typename Slot>
    bool router<Groups...>::poll_atomic(Slot& slot) noexcept {
        return std::apply(
            [this, &slot](auto&... handlers) {
                using order = etools::meta::sort_t<etools::meta::size_greater,
                    details::handler_packet_t<decltype(handlers)>...>;
                return this->try_atomic_all(slot.group.channel, slot.group.handlers, order{});
            },
            slot.group.handlers
        );
    }

    template<typename... Groups>
    template<typename Channel, typename HandlerTuple, typename... Sorted>
    bool router<Groups...>::try_atomic_all(
        Channel& channel,
        HandlerTuple& handlers,
        etools::meta::typelist<Sorted...>
    ) noexcept {
        return (... or try_atomic<Sorted>(channel, handlers));
    }

    template<typename... Groups>
    template<typename Packet, typename Channel, typename HandlerTuple>
    bool router<Groups...>::try_atomic(Channel& channel, HandlerTuple& handlers) noexcept {
        if constexpr (channels::details::is_channel_like_v<Channel, Packet>) {
            auto maybe = channels::details::invoke_try_receive<Packet>(channel);
            if (not maybe) return false;
            dispatch<Packet>(handlers, *maybe);
            return true;
        } else {
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // Dispatch to the one handler declaring this Packet.
    // -------------------------------------------------------------------------

    template<typename... Groups>
    template<typename Packet, typename HandlerTuple>
    void router<Groups...>::dispatch(HandlerTuple& handlers, Packet& packet) noexcept {
        // The `if constexpr` is what keeps this well-formed: without it, every
        // handler would have to be callable with every Packet.
        std::apply(
            [&packet](auto&... handler) {
                auto invoke_if_matching = [&packet](auto& h) {
                    if constexpr (std::is_same_v<details::handler_packet_t<decltype(h)>, Packet>) {
                        h(packet);
                    }
                };
                (invoke_if_matching(handler), ...);
            },
            handlers
        );
    }

} // namespace ecomm

#endif // ECOMM_FABRIC_ROUTER_TPP_
