"""``TcpChannel`` -- mirrors ``ecomm/channels/arduino_wifi_channel.hpp``.

Reads and writes fixed-size packets as raw binary blobs over an active TCP
connection, exactly as ``arduino_wifi_channel`` does over Arduino's
``WiFiClient``: no framing bytes, no length prefix -- just
``schema.packet_size`` bytes back to back.

``arduino_wifi_channel`` wraps a ``WiFiServer``: the *firmware* accepts
incoming connections, so a PC/Raspberry Pi client connects *to* the board.
:class:`TcpChannel` therefore acts as the TCP client side, opening a
connection to the board's listening address.

Because TCP already provides delivery and integrity guarantees, the
recommended :class:`~ecomm.protocol.schema.PacketSchema` for this channel
uses ``checksum=ChecksumPolicy.NONE``, exactly as the ``arduino_wifi_channel``
docstring recommends on the C++ side.

TCP is a byte stream with no built-in message boundaries, unlike a
microcontroller's ``WiFiClient.available()``/``read()`` pair, which buffers
internally. :class:`TcpChannel` reproduces that buffering behavior with an
internal accumulation buffer plus :func:`select.select` for non-blocking
polling, so ``try_receive()`` never blocks and never returns a partial
packet -- the same contract ``do_try_receive`` upholds in
``arduino_wifi_channel.tpp``.
"""

from __future__ import annotations

import select
import socket as socket_module
from typing import Any

from ecomm._typing import beartype

from ecomm.protocol.schema import PacketSchema

from ecomm.channels.base import Channel

#: Size of each non-blocking read attempt from the socket. Larger than any
#: realistic ecomm packet so a single readable chunk typically satisfies
#: several packets' worth of backlog in one syscall.
_RECV_CHUNK_SIZE = 4096


@beartype
class TcpChannel(Channel):
    """TCP client channel for talking to ecomm firmware over Wi-Fi.

    Attributes:
        schema: Inherited from :class:`~ecomm.channels.base.Channel`.
        socket: The underlying :class:`socket.socket` instance.
    """

    def __init__(
        self,
        schema: PacketSchema,
        host: str,
        port: int,
        connect_timeout: float | None = 10.0,
        **socket_kwargs: Any,
    ) -> None:
        """Connect to a board's TCP server and bind the connection to ``schema``.

        Args:
            schema: The packet schema this channel operates on.
            host: Hostname or IP address of the board's ``WiFiServer``.
            port: TCP port the board's ``WiFiServer`` is listening on.
            connect_timeout: Seconds to wait for the initial connection
                before raising :class:`TimeoutError`. ``None`` waits
                indefinitely.
            **socket_kwargs: Additional keyword arguments forwarded to
                :func:`socket.create_connection`.
        """
        super().__init__(schema)
        self.socket = socket_module.create_connection(
            (host, port), timeout=connect_timeout, **socket_kwargs
        )
        self.socket.setblocking(False)
        self._recv_buffer = bytearray()

    def _do_send(self, data: bytes) -> None:
        """Write ``data`` to the socket. Mirrors ``do_send``."""
        self.socket.sendall(data)

    def _do_try_receive(self, size: int) -> bytes | None:
        """Read ``size`` bytes if that many are already buffered.

        Polls the socket with a zero-timeout :func:`select.select` call
        so this never blocks; any bytes that arrive are appended to an
        internal buffer, and exactly ``size`` bytes are popped off the
        front once enough have accumulated.

        Args:
            size: Number of bytes to read.

        Returns:
            Exactly ``size`` bytes, or ``None`` if fewer are currently
            available.

        Raises:
            ConnectionError: The peer closed the connection.
        """
        readable, _, _ = select.select([self.socket], [], [], 0)
        if readable:
            chunk = self.socket.recv(_RECV_CHUNK_SIZE)
            if chunk == b"":
                raise ConnectionError("TcpChannel: connection closed by peer")
            self._recv_buffer += chunk

        if len(self._recv_buffer) < size:
            return None

        data = bytes(self._recv_buffer[:size])
        del self._recv_buffer[:size]
        return data

    def close(self) -> None:
        """Close the underlying socket."""
        self.socket.close()
