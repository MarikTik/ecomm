"""``Channel`` -- mirrors the CRTP base class in ``ecomm/channels/channel.hpp``.

A channel is a self-contained, typed, two-way communication endpoint bound
to one :class:`~ecomm.protocol.schema.PacketSchema`. It handles validation
transparently on receive and sealing on send.

Concrete transports (:class:`~ecomm.channels.serial_channel.SerialChannel`,
:class:`~ecomm.channels.tcp_channel.TcpChannel`) implement two primitives:

- ``_do_send(data: bytes) -> None`` -- write raw bytes to the physical
  medium.
- ``_do_try_receive(size: int) -> bytes | None`` -- read exactly ``size``
  raw bytes from the medium if that many are already available; return
  ``None`` otherwise (never partial data, never a blocking wait -- mirrors
  ``do_try_receive``'s ``available() < sizeof(Packet)`` early-out in both
  ``arduino_serial_channel.tpp`` and ``arduino_wifi_channel.tpp``).

``Channel`` then composes validation
(:func:`ecomm.protocol.validator.is_valid`) and sealing
(:func:`ecomm.protocol.validator.seal`) around those primitives so callers
always work with structurally valid packets, exactly mirroring
``channel.tpp``.

Where the C++ side uses CRTP (``channel<Impl, Packet>``) because virtual
dispatch has a runtime cost worth avoiding on a microcontroller, this
package uses ordinary abstract-base-class inheritance: a Python client
talking to firmware is not the resource-constrained side of the link, and
CRTP has no ergonomic benefit in a language without compile-time
monomorphization.
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
class Channel(ABC):
    """Abstract two-way communication channel bound to one packet schema.

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
    def send(self, packet: Packet) -> SendResult:
        """Send a packet.

        Seals the packet (computes and writes the FCS if
        ``schema.checksum`` is not
        :attr:`~ecomm.protocol.checksum.ChecksumPolicy.NONE`) then
        delegates the raw byte write to the transport. Always returns
        :attr:`~ecomm.channels.result.SendResult.OK`; this base channel
        makes no delivery guarantee -- use
        :class:`ecomm.channels.reliable.ReliableChannel` for that.

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
        self._do_send(packet.to_bytes())
        return SendResult.OK

    @ensure(
        lambda self, result: result is None or result.schema == self.schema,
        "a returned packet must belong to this channel's schema",
    )
    def try_receive(self) -> Packet | None:
        """Attempt to receive a packet.

        Delegates to the transport. If a complete packet was read, it
        passes :func:`~ecomm.protocol.validator.is_valid`, and (for
        network-topology packets) its ``receiver_id`` is either
        ``schema.board_id`` or
        :data:`~ecomm.protocol.config.BROADCAST_ADDRESS`, the packet is
        returned. Returns ``None`` if nothing is available, the packet is
        corrupt, or it is addressed to a different node -- mirrors the
        receiver-id filter added to ``channel.tpp`` on 2026-05-28.

        Returns:
            A validated, correctly-addressed :class:`Packet`, or ``None``.
        """
        raw = self._do_try_receive(self.schema.packet_size)
        if raw is None:
            return None
        return decode_validated_and_addressed(self.schema, raw)

    @abstractmethod
    def _do_send(self, data: bytes) -> None:
        """Write raw bytes to the physical medium.

        Args:
            data: Exactly ``schema.packet_size`` bytes to transmit.
        """
        raise NotImplementedError

    @abstractmethod
    def _do_try_receive(self, size: int) -> bytes | None:
        """Read exactly ``size`` raw bytes from the medium, without blocking.

        Args:
            size: Number of bytes to read (always ``schema.packet_size``).

        Returns:
            Exactly ``size`` bytes if that many were already available;
            ``None`` otherwise. Never returns a partial read.
        """
        raise NotImplementedError

    def close(self) -> None:
        """Release any resources held by this channel (default: no-op).

        Concrete transports that own an OS resource (a serial port, a
        socket) override this to release it.
        """
        return None

    def __enter__(self) -> "Channel":
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.close()
