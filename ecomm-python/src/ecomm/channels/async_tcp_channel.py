"""``AsyncTcpChannel`` -- asyncio-native counterpart to ``TcpChannel``.

Same wire behavior as :class:`~ecomm.channels.tcp_channel.TcpChannel`
(raw ``schema.packet_size`` bytes, no framing), built on
:func:`asyncio.open_connection` instead of a blocking socket plus manual
:func:`select.select` polling.

**Why not just wrap ``asyncio.StreamReader.readexactly`` directly under
``asyncio.wait_for(..., timeout=0)`` for the non-blocking ``try_receive``
case?** That was the first approach tried here, and it does not work:
``wait_for`` with a zero timeout can raise :class:`asyncio.TimeoutError`
*even when the requested bytes are already fully buffered* -- the timer
can fire before the awaited coroutine gets a chance to run at all, on
every CPython version tested. Relying on it would make ``try_receive()``
spuriously return ``None`` under load, silently dropping packets from the
caller's point of view (they are still in the OS receive buffer, but the
channel would never say so).

Instead, this module runs one small background task per channel (started
in :meth:`AsyncTcpChannel.connect`) that continuously drains the
:class:`asyncio.StreamReader` into a plain ``bytearray`` this class owns
directly. ``try_receive()`` then just checks that buffer's length --
no timeout heuristics, no race: because asyncio is single-threaded and
cooperative, nothing can mutate the buffer between checking its length and
consuming from it as long as neither step ``await``s in between.
:meth:`AsyncTcpChannel.receive` waits on an :class:`asyncio.Event` that the
pump task sets whenever new bytes arrive, so it suspends efficiently
rather than polling.
"""

from __future__ import annotations

import asyncio
from typing import Any

from ecomm._typing import beartype

from ecomm.protocol.schema import PacketSchema

from ecomm.channels.async_base import AsyncChannel

#: Size of each read the background pump task requests from the
#: StreamReader. Larger than any realistic ecomm packet so one read
#: typically drains several packets' worth of backlog at once.
_RECV_CHUNK_SIZE = 4096


@beartype
class AsyncTcpChannel(AsyncChannel):
    """Asyncio TCP client channel for talking to ecomm firmware over Wi-Fi.

    Construct via :meth:`connect` (an ``async`` factory, since opening a
    connection is itself a coroutine) rather than calling ``__init__``
    directly.

    Attributes:
        schema: Inherited from :class:`~ecomm.channels.async_base.AsyncChannel`.
    """

    def __init__(
        self,
        schema: PacketSchema,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ) -> None:
        """Wrap an already-open asyncio stream pair.

        Prefer :meth:`connect` unless you already have a connected
        ``(reader, writer)`` pair (e.g. from a test harness). Must be
        called from inside a running event loop, since it schedules the
        background receive pump immediately.

        Args:
            schema: The packet schema this channel operates on.
            reader: An open, connected stream reader.
            writer: An open, connected stream writer for the same
                connection as ``reader``.
        """
        super().__init__(schema)
        self._reader = reader
        self._writer = writer
        self._buffer = bytearray()
        self._data_event = asyncio.Event()
        self._closed_error: Exception | None = None
        self._pump_task = asyncio.ensure_future(self._pump())

    @classmethod
    async def connect(
        cls,
        schema: PacketSchema,
        host: str,
        port: int,
        **open_connection_kwargs: Any,
    ) -> "AsyncTcpChannel":
        """Open a TCP connection to a board's server and bind it to ``schema``.

        Args:
            schema: The packet schema this channel operates on.
            host: Hostname or IP address of the board's ``WiFiServer``.
            port: TCP port the board's ``WiFiServer`` is listening on.
            **open_connection_kwargs: Additional keyword arguments
                forwarded to :func:`asyncio.open_connection`.

        Returns:
            A connected :class:`AsyncTcpChannel`.
        """
        reader, writer = await asyncio.open_connection(host, port, **open_connection_kwargs)
        return cls(schema, reader, writer)

    async def _pump(self) -> None:
        """Continuously drain the stream reader into ``self._buffer``.

        Runs for the lifetime of the channel. Sets ``self._data_event``
        every time new bytes arrive (or the connection ends), so
        ``receive()`` can wait on that event instead of polling.
        """
        try:
            while True:
                chunk = await self._reader.read(_RECV_CHUNK_SIZE)
                if chunk == b"":
                    self._closed_error = ConnectionError("AsyncTcpChannel: connection closed by peer")
                    return
                self._buffer += chunk
                self._data_event.set()
        except asyncio.CancelledError:
            raise
        except Exception as exc:  # noqa: BLE001 -- surfaced to callers via _closed_error
            self._closed_error = exc
        finally:
            self._data_event.set()  # wake any waiter so it observes the closed/error state

    async def _wait_for_bytes(self, size: int) -> None:
        """Suspend until ``self._buffer`` holds at least ``size`` bytes."""
        while len(self._buffer) < size:
            if self._closed_error is not None:
                raise self._closed_error
            self._data_event.clear()
            await self._data_event.wait()

    async def _do_send(self, data: bytes) -> None:
        """Write ``data`` to the stream and await backpressure-aware drain."""
        self._writer.write(data)
        await self._writer.drain()

    async def _do_receive(self, size: int) -> bytes:
        """Suspend until ``size`` bytes are buffered, then pop and return them."""
        await self._wait_for_bytes(size)
        data = bytes(self._buffer[:size])
        del self._buffer[:size]
        return data

    async def _do_try_receive(self, size: int) -> bytes | None:
        """Pop and return ``size`` bytes if already buffered; else ``None``.

        Never awaits new network data -- only inspects the buffer the
        background pump task has already filled.
        """
        if len(self._buffer) < size:
            if self._closed_error is not None:
                raise self._closed_error
            return None
        data = bytes(self._buffer[:size])
        del self._buffer[:size]
        return data

    async def close(self) -> None:
        """Stop the background pump task and close the underlying connection."""
        self._pump_task.cancel()
        try:
            await self._pump_task
        except asyncio.CancelledError:
            pass
        self._writer.close()
        await self._writer.wait_closed()
