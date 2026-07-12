// SPDX-License-Identifier: MIT
/**
* @file send_result.hpp
*
* @brief Return type for channel send operations.
*
* @ingroup ecomm_channels ecomm::channels
*
* `send_result` is returned by `channel::send` and `reliable_channel::send`.
* For `channel` (the unreliable base), the only possible value is `ok` -- the
* call always delegates to `do_send` without any acknowledgement.
* For `reliable_channel`, `timeout` signals that all retransmit attempts were
* exhausted before a matching ack was received.
*
* Using an enum class rather than `bool` makes call sites self-documenting and
* leaves room for additional result codes (e.g. `nack`, `aborted`) without
* changing the function signature.
*
* @author Mark Tikhonov <mtik.philosopher@gmail.com>
*
* @date 2026-05-27
*
* @copyright
* MIT License
* Copyright (c) 2026 Mark Tikhonov
* See LICENSE file for details.
*
* @par Changelog
* - 2026-05-27 Initial creation.
*/
#ifndef ECOMM_CHANNELS_SEND_RESULT_HPP_
#define ECOMM_CHANNELS_SEND_RESULT_HPP_

#include <cstdint>

namespace ecomm::channels {

    /**
    * @enum send_result
    *
    * @brief Outcome of a channel send operation.
    *
    * - `ok`      -- the packet was handed to the transport (unreliable channel),
    *               or an acknowledgement was received within the retry budget
    *               (reliable channel).
    * - `timeout` -- all retransmit attempts were exhausted without receiving a
    *               matching ack. Only returned by `reliable_channel::send`;
    *               `channel::send` always returns `ok`.
    */
    enum class send_result : std::uint8_t {
        ok      = 0, ///< Packet delivered (or best-effort sent for unreliable channel).
        timeout = 1, ///< Ack not received after MaxRetries retransmissions.
    };

} // namespace ecomm::channels

#endif // ECOMM_CHANNELS_SEND_RESULT_HPP_
