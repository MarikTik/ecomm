"""Shared test doubles for ecomm.channels tests.

``QueueChannel`` is a real :class:`ecomm.channels.base.Channel` subclass
backed by plain in-memory byte buffers instead of hardware, so channel-level
logic (validation, address filtering, sealing, and the ack/retry protocol in
:class:`ecomm.channels.reliable.ReliableChannel`) can be tested without a
serial port or socket. ``make_linked_pair`` wires two ``QueueChannel``
instances together so sends on one land directly in the other's receive
buffer, modeling an ideal (lossless) transport; ``drop_next_send`` lets a
test simulate one lost packet to exercise retry/timeout paths.
"""

from __future__ import annotations

from ecomm.channels.base import Channel
from ecomm.protocol.schema import PacketSchema


class QueueChannel(Channel):
    """A :class:`Channel` backed by explicit send/receive byte buffers."""

    def __init__(self, schema: PacketSchema, send_buffer: bytearray, recv_buffer: bytearray) -> None:
        super().__init__(schema)
        self._send_buffer = send_buffer
        self._recv_buffer = recv_buffer
        self.drop_next_n_sends = 0

    def _do_send(self, data: bytes) -> None:
        if self.drop_next_n_sends > 0:
            self.drop_next_n_sends -= 1
            return
        self._send_buffer += data

    def _do_try_receive(self, size: int) -> bytes | None:
        if len(self._recv_buffer) < size:
            return None
        out = bytes(self._recv_buffer[:size])
        del self._recv_buffer[:size]
        return out


def make_linked_pair(schema_a: PacketSchema, schema_b: PacketSchema) -> tuple[QueueChannel, QueueChannel]:
    """Build two ``QueueChannel``s wired so each one's sends reach the other."""
    buf_a_to_b = bytearray()
    buf_b_to_a = bytearray()
    a = QueueChannel(schema_a, send_buffer=buf_a_to_b, recv_buffer=buf_b_to_a)
    b = QueueChannel(schema_b, send_buffer=buf_b_to_a, recv_buffer=buf_a_to_b)
    return a, b
