// SPDX-License-Identifier: MIT
/**
* @file reliable_channel.tpp
*
* @brief Template definitions for reliable_channel.
*
* @ingroup ecomm_channels ecomm::channels
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-28
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-28 Initial creation.
* - 2026-07-16 `_channel.try_receive()` calls became
*      `_channel.template try_receive<Packet>()` (channel<Impl>::try_receive is
*      now templated on Packet and no longer deducible with zero arguments).
*/
#ifndef ECOMM_CHANNELS_RELIABLE_CHANNEL_TPP_
#define ECOMM_CHANNELS_RELIABLE_CHANNEL_TPP_

#include "reliable_channel.hpp"

namespace ecomm::channels {

template<typename Impl, typename Packet, typename ClockPolicy,
         std::size_t MaxRetries, std::size_t BufferDepth>
template<typename... Args>
reliable_channel<Impl, Packet, ClockPolicy, MaxRetries, BufferDepth>
    ::reliable_channel(Args&&... args) noexcept
    : _channel{static_cast<Args&&>(args)...}
{}

template<typename Impl, typename Packet, typename ClockPolicy,
         std::size_t MaxRetries, std::size_t BufferDepth>
send_result
reliable_channel<Impl, Packet, ClockPolicy, MaxRetries, BufferDepth>
    ::send(Packet& packet) noexcept
{
    packet.header.seq_num = _tx_seq;

    for (std::size_t attempt = 0; attempt < MaxRetries; ++attempt) {
        static_cast<void>(_channel.send(packet));

        const tick_type start = ClockPolicy::now();
        while (static_cast<tick_type>(ClockPolicy::now() - start)
               < ClockPolicy::timeout_ticks())
        {
            if (poll_ack(_tx_seq)) {
                _tx_seq = static_cast<std::uint8_t>(_tx_seq + 1u);
                return send_result::ok;
            }
        }
    }

    return send_result::timeout;
}

template<typename Impl, typename Packet, typename ClockPolicy,
         std::size_t MaxRetries, std::size_t BufferDepth>
std::optional<Packet>
reliable_channel<Impl, Packet, ClockPolicy, MaxRetries, BufferDepth>
    ::try_receive() noexcept
{
    Packet out{};
    if (stage_pop(out)) return out;

    auto incoming = _channel.template try_receive<Packet>();
    if (not incoming) return std::nullopt;

    Packet& pkt = *incoming;

    if (pkt.header.has(protocol::header_options::ack)) return std::nullopt;

    const std::uint8_t seq = pkt.header.seq_num;

    if (seq == _rx_seq) {
        send_ack(seq);
        _rx_seq = static_cast<std::uint8_t>(_rx_seq + 1u);
        return pkt;
    }

    send_ack(seq);
    return std::nullopt;
}

template<typename Impl, typename Packet, typename ClockPolicy,
         std::size_t MaxRetries, std::size_t BufferDepth>
void
reliable_channel<Impl, Packet, ClockPolicy, MaxRetries, BufferDepth>
    ::send_ack(std::uint8_t seq) noexcept
{
    Packet ack{};
    ack.header = typename Packet::header_t{
        protocol::header_type::data,
        protocol::header_options::ack
    };
    ack.header.seq_num = seq;
    static_cast<void>(_channel.send(ack));
}

template<typename Impl, typename Packet, typename ClockPolicy,
         std::size_t MaxRetries, std::size_t BufferDepth>
bool
reliable_channel<Impl, Packet, ClockPolicy, MaxRetries, BufferDepth>
    ::poll_ack(std::uint8_t seq) noexcept
{
    auto incoming = _channel.template try_receive<Packet>();
    if (not incoming) return false;

    const Packet& pkt = *incoming;

    if (not pkt.header.has(protocol::header_options::ack)) {
        stage_push(pkt);
        return false;
    }

    return pkt.header.seq_num == seq;
}

template<typename Impl, typename Packet, typename ClockPolicy,
         std::size_t MaxRetries, std::size_t BufferDepth>
bool
reliable_channel<Impl, Packet, ClockPolicy, MaxRetries, BufferDepth>
    ::stage_push(const Packet& pkt) noexcept
{
    if (_stage_count == BufferDepth) return false;
    _stage[_stage_head] = pkt;
    _stage_head = (_stage_head + 1) % BufferDepth;
    ++_stage_count;
    return true;
}

template<typename Impl, typename Packet, typename ClockPolicy,
         std::size_t MaxRetries, std::size_t BufferDepth>
bool
reliable_channel<Impl, Packet, ClockPolicy, MaxRetries, BufferDepth>
    ::stage_pop(Packet& out) noexcept
{
    if (_stage_count == 0) return false;
    const std::size_t tail = (_stage_head + BufferDepth - _stage_count) % BufferDepth;
    out = _stage[tail];
    --_stage_count;
    return true;
}

} // namespace ecomm::channels

#endif // ECOMM_CHANNELS_RELIABLE_CHANNEL_TPP_
