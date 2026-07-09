"""``Packet`` -- mirrors ``ecomm::protocol::packet<...>`` in ``packet.hpp``.

A packet is the fundamental unit of data on the wire: a header followed
immediately by a raw payload region. Nothing else -- application-layer
concepts such as handler ids and status codes are not part of the packet;
they live in the first bytes of the payload and are interpreted by the
layer above, exactly as documented in ``packet.hpp``.
"""

from __future__ import annotations

from ecomm._typing import beartype
from icontract import ensure, invariant, require

from ecomm.protocol.header import PacketHeader
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.schema import PacketSchema


@beartype
@invariant(
    lambda self: len(self.payload) == self.schema.payload_size,
    "payload must stay exactly schema.payload_size bytes",
)
@invariant(
    lambda self: self.header.schema == self.schema,
    "packet.header must stay bound to the same schema as the packet",
)
class Packet:
    """A fixed-size wire packet: a header followed by a raw payload.

    Attributes:
        schema: The schema this packet was built against.
        header: The packet's :class:`~ecomm.protocol.header.PacketHeader`.
        payload: Mutable byte buffer of exactly ``schema.payload_size``
            bytes. The application may overlay any structure on this
            region (handler id, task id, error envelope, firmware chunk,
            ...) by slicing and packing/unpacking through it; the packet
            itself imposes no schema, matching the C++ ``std::byte
            payload[payload_size]`` member.
    """

    __slots__ = ("schema", "header", "payload")

    def __init__(
        self,
        schema: PacketSchema,
        type_: HeaderType = HeaderType.DATA,
        options: HeaderOptions = HeaderOptions.NONE,
    ) -> None:
        """Construct a packet with a given header type and option flags.

        Delegates header construction to
        :meth:`~ecomm.protocol.header.PacketHeader.__init__`. The payload
        is zero-initialized.

        Args:
            schema: The schema to build this packet against.
            type_: Top-level packet classification.
            options: OR-combined :class:`HeaderOptions` flags; pass
                :attr:`HeaderOptions.NONE` when no flags are needed.
        """
        self.schema = schema
        self.header = PacketHeader(schema, type_, options)
        self.payload = bytearray(schema.payload_size)

    @ensure(
        lambda self, result: len(result) == self.schema.packet_size,
        "a serialized packet must be exactly schema.packet_size bytes",
    )
    def to_bytes(self) -> bytes:
        """Serialize this packet to its wire representation.

        Returns:
            Exactly ``schema.packet_size`` bytes: the header followed by
            the payload.
        """
        return self.header.to_bytes() + bytes(self.payload)

    @classmethod
    @require(lambda schema, data: len(data) == schema.packet_size, "data must be exactly schema.packet_size bytes")
    @ensure(lambda schema, result: result.schema == schema, "the decoded packet must be bound to the given schema")
    @ensure(
        lambda schema, result: len(result.payload) == schema.payload_size,
        "the decoded packet's payload must be exactly schema.payload_size bytes",
    )
    def from_bytes(cls, schema: PacketSchema, data: bytes) -> "Packet":
        """Deserialize a packet from its wire representation.

        Note:
            This does not validate the checksum -- callers that received
            ``data`` from an untrusted transport should call
            :func:`ecomm.protocol.validator.is_valid` on the result before
            trusting it. (:class:`ecomm.channels.base.Channel` does this
            automatically for every packet returned by ``try_receive()``.)

        Args:
            schema: The schema ``data`` was encoded against.
            data: Exactly ``schema.packet_size`` bytes.

        Returns:
            A :class:`Packet` with header and payload decoded from
            ``data``.
        """
        packet = cls(schema)
        packet.header = PacketHeader.from_bytes(schema, data[: schema.header_size])
        packet.payload = bytearray(data[schema.header_size :])
        return packet
