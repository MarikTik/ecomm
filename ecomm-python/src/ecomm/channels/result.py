"""``SendResult`` -- mirrors ``ecomm/channels/send_result.hpp``.

Returned by :meth:`ecomm.channels.base.Channel.send` and
:meth:`ecomm.channels.reliable.ReliableChannel.send`. For the unreliable
:class:`~ecomm.channels.base.Channel`, the only possible value is
:attr:`SendResult.OK` -- the call always delegates to the transport without
any acknowledgement. For :class:`~ecomm.channels.reliable.ReliableChannel`,
:attr:`SendResult.TIMEOUT` signals that all retransmit attempts were
exhausted before a matching ack was received.
"""

from __future__ import annotations

from enum import IntEnum


class SendResult(IntEnum):
    """Outcome of a channel send operation.

    Attributes:
        OK: The packet was handed to the transport (unreliable channel),
            or an acknowledgement was received within the retry budget
            (reliable channel).
        TIMEOUT: All retransmit attempts were exhausted without receiving
            a matching ack. Only returned by
            :meth:`~ecomm.channels.reliable.ReliableChannel.send`; the
            base :class:`~ecomm.channels.base.Channel` always returns
            :attr:`OK`.
    """

    OK = 0
    TIMEOUT = 1
