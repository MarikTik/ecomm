"""``AsyncChannel`` -- asyncio-native counterpart to ``ecomm.channels.base.Channel``.

:class:`~ecomm.channels.base.Channel` composes validation and sealing
around two *synchronous, non-blocking* primitives (``_do_send``,
``_do_try_receive``) and expects the caller to poll ``try_receive()`` in a
loop. That is the right shape for a single hardware transport driven from
one thread, but it does not scale to talking to several boards
concurrently: polling N channels means either N threads or a manual
round-robin loop that wastes CPU checking channels with nothing to say.

``AsyncChannel`` composes the same validation/sealing logic around
*asyncio* primitives instead, so a caller can ``await`` many channels
concurrently (via ``asyncio.gather``, a `TaskGroup`, or an event loop
scheduling many coroutines) and each one only consumes CPU when it
actually has work to do -- the event loop suspends a coroutine awaiting
:meth:`AsyncChannel.receive` until the underlying transport actually has
data, with no polling loop anywhere in user code.

Concrete transports (:class:`~ecomm.channels.async_tcp_channel.AsyncTcpChannel`)
implement three primitives:

- ``_do_send(data: bytes) -> None`` -- write raw bytes to the transport.
- ``_do_receive(size: int) -> bytes`` -- suspend until exactly ``size``
  bytes are available, then return them. Never returns a partial read.
- ``_do_try_receive(size: int) -> bytes | None`` -- return exactly
  ``size`` bytes if that many are *already* buffered, else ``None``
  immediately, without suspending. This is the async equivalent of
  :meth:`ecomm.channels.base.Channel._do_try_receive`'s
  non-blocking-poll contract.
"""

from __future__ import annotations

from abc import ABC, abstractmethod

from ecomm._typing import beartype
from icontract import ensure, require

from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.validator import seal

from ecomm.channels._decode import decode_validated_and_addressed
from ecomm.channels.result import SendResult


@beartype
class AsyncChannel(ABC):
    """Abstract asyncio two-way communication channel bound to one packet schema.

    Attributes:
        schema: The :class:`~ecomm.protocol.schema.PacketSchema` every
            packet sent or received through this channel is built
            against.
    """

    def __init__(self, schema: PacketSchema) -> None:
        """Bind this channel to a packet schema.

        Args:
            schema: The schema every packet sent or received through
                this channel must match.
        """
        self.schema = schema

    @require(
        lambda self, packet: packet.schema == self.schema,
        "packet.schema must equal the channel's schema (wire-incompatible otherwise)",
    )
    async def send(self, packet: Packet) -> SendResult:
        """Send a packet.

        Seals the packet (computes and writes the FCS if
        ``schema.checksum`` is not
        :attr:`~ecomm.protocol.checksum.ChecksumPolicy.NONE`) then awaits
        the raw byte write. Always returns
        :attr:`~ecomm.channels.result.SendResult.OK`; this channel makes
        no delivery guarantee.

        Args:
            packet: Packet to send. Must have been built from a schema
                equal to this channel's ``schema`` -- otherwise the
                serialized bytes would not match what the channel (and
                the remote peer) expect. Its FCS field may be overwritten
                by sealing; all other fields are preserved.

        Returns:
            :attr:`~ecomm.channels.result.SendResult.OK` unconditionally.
        """
        seal(packet)
        await self._do_send(packet.to_bytes())
        return SendResult.OK

    @ensure(
        lambda self, result: result is None or result.schema == self.schema,
        "a returned packet must belong to this channel's schema",
    )
    async def receive(self) -> Packet | None:
        """Await the next packet, suspending efficiently until it arrives.

        This is the primary, recommended way to consume an
        ``AsyncChannel``: it does not poll or busy-wait -- the coroutine
        is suspended by the event loop and resumed exactly when enough
        bytes have arrived.

        Returns:
            The next packet if it passes
            :func:`~ecomm.protocol.validator.is_valid` and (for
            network-topology packets) is addressed to this board or
            broadcast; ``None`` if that one packet was corrupt or
            misaddressed. Callers that want to skip bad packets and keep
            waiting should call this again (typically in a ``while True``
            receive loop).
        """
        raw = await self._do_receive(self.schema.packet_size)
        return decode_validated_and_addressed(self.schema, raw)

    @ensure(
        lambda self, result: result is None or result.schema == self.schema,
        "a returned packet must belong to this channel's schema",
    )
    async def try_receive(self) -> Packet | None:
        """Return a packet only if one is already fully buffered.

        Never suspends waiting for new network data -- returns
        immediately either way. Useful inside a single event-loop tick
        where you want to drain whatever has already arrived without
        yielding control.

        Returns:
            A validated, correctly-addressed :class:`Packet`, or
            ``None`` if nothing complete is buffered yet (or the one
            buffered packet was corrupt/misaddressed).
        """
        raw = await self._do_try_receive(self.schema.packet_size)
        if raw is None:
            return None
        return decode_validated_and_addressed(self.schema, raw)

    @abstractmethod
    async def _do_send(self, data: bytes) -> None:
        """Write raw bytes to the transport.

        Args:
            data: Exactly ``schema.packet_size`` bytes to transmit.
        """
        raise NotImplementedError

    @abstractmethod
    async def _do_receive(self, size: int) -> bytes:
        """Suspend until exactly ``size`` bytes are available, then return them.

        Args:
            size: Number of bytes to read (always ``schema.packet_size``).

        Returns:
            Exactly ``size`` bytes.

        Raises:
            ConnectionError: The transport was closed before ``size``
                bytes arrived.
        """
        raise NotImplementedError

    @abstractmethod
    async def _do_try_receive(self, size: int) -> bytes | None:
        """Return exactly ``size`` bytes if already buffered, else ``None``.

        Must never suspend waiting for new data to arrive.

        Args:
            size: Number of bytes to read (always ``schema.packet_size``).

        Returns:
            Exactly ``size`` bytes, or ``None`` if fewer are currently
            buffered.

        Raises:
            ConnectionError: The transport was closed and fewer than
                ``size`` bytes remain buffered.
        """
        raise NotImplementedError

    async def close(self) -> None:
        """Release any resources held by this channel (default: no-op).

        Concrete transports that own an OS resource (a socket) override
        this to release it.
        """
        return None

    async def __aenter__(self) -> "AsyncChannel":
        return self

    async def __aexit__(self, *exc_info: object) -> None:
        await self.close()
