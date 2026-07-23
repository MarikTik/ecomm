// SPDX-License-Identifier: MIT
/**
* @file role.hpp
*
* @brief A channel's participation state within an `ecomm::hub`.
*
* @ingroup ecomm_channels ecomm::channels
*
* `role` belongs to a channel, not to `hub` itself: it describes whether that
* particular channel currently participates in `hub::send()`,
* `hub::try_receive()`, both, or neither. `hub::set_role<Channel>(role)`
* assigns it.
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
* - 2026-07-19 Moved out of `ecomm/hub/hub.hpp` (previously `hub_role`) into
*      `ecomm::channels`, since the concept it names belongs to a channel, not
*      to `hub`.
*/
#ifndef ECOMM_CHANNELS_ROLE_HPP_
#define ECOMM_CHANNELS_ROLE_HPP_

#include <cstdint>

namespace ecomm::channels {

    /**
    * @enum role
    *
    * @brief The four participation states a channel can be in within a `hub`.
    *
    * Assigned wholesale via `hub::set_role<Channel>(role)` rather than
    * toggled one flag at a time, so a channel's participation is always set
    * to one complete, unambiguous state in a single call.
    *
    * - `sender`      -- active for `hub::send()` only.
    * - `receiver`    -- active for `hub::try_receive()` only.
    * - `transceiver` -- active for both; every channel's starting state when
    *                    a `hub` is constructed.
    * - `none`        -- active for neither; `hub` skips this channel
    *                    entirely until its role is changed again.
    */
    enum class role : std::uint8_t {
        sender      = 0, ///< Active for `hub::send()` only.
        receiver    = 1, ///< Active for `hub::try_receive()` only.
        transceiver = 2, ///< Active for both send and receive (the default).
        none        = 3, ///< Active for neither.
    };

} // namespace ecomm::channels

#endif // ECOMM_CHANNELS_ROLE_HPP_
