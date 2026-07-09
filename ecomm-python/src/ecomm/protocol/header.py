"""``PacketHeader`` -- mirrors the eight ``packet_header<...>`` specializations.

On the C++ side, ``packet_header<Topology, SequencePolicy, ChecksumPolicy>``
has eight explicit partial specializations, one per combination of the three
policies, each a distinct standard-layout type with only the fields its
combination needs (see ``header_layout.hpp``). Python has one
:class:`PacketHeader` class instead; the :class:`~ecomm.protocol.schema.PacketSchema`
passed at construction time determines which fields actually get written to
or read from the wire in :meth:`PacketHeader.to_bytes` /
:meth:`PacketHeader.from_bytes` -- unused fields simply exist as inert
Python attributes (always zero-initialized, exactly like the C++ default
constructor's ``@post`` contracts), never serialized.

Wire layout (identical to the ``@par Wire layout`` block in
``packet_header.hpp``)::

    +------------------+-------------+------------------+-----------------+-------------------------------+
    | proto byte  (1B) | seq_num (*s)| sender_id (*n)   | receiver_id (*n)| fcs  (ChecksumPolicy.size $)  |
    +------------------+-------------+------------------+-----------------+-------------------------------+
    (*s) only present when schema.sequence == SequencePolicy.SEQUENCED
    (*n) only present when schema.topology == Topology.NETWORK
    ($)  only present when schema.checksum != ChecksumPolicy.NONE

Protocol byte layout::

     7..5 : type      (3 bits)  -- HeaderType enum, 6 values used (2 reserved)
        4 : error     (1 bit)   -- HeaderOptions.ERROR
        3 : ack       (1 bit)   -- HeaderOptions.ACK
        2 : encrypted (1 bit)   -- HeaderOptions.ENCRYPTED
     1..0 : version   (2 bits)  -- PROTOCOL_VERSION, not caller-settable
"""

from __future__ import annotations

import struct

from ecomm._typing import beartype
from icontract import ensure, invariant, require

from ecomm.errors import MalformedPacketError
from ecomm.protocol import config
from ecomm.protocol.header_options import HEADER_OPTIONS_MASK, HeaderOptions
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.topology import Topology

_TYPE_SHIFT = 5
_TYPE_MASK = 0x7
_VERSION_MASK = 0x3


@beartype
@invariant(lambda self: 0 <= self.raw <= 0xFF, "the packed protocol byte must fit in one byte")
@invariant(lambda self: 0 <= self.seq_num <= 0xFF, "seq_num must fit in one byte (wraps at 255)")
@invariant(lambda self: 0 <= self.sender_id <= 0xFF, "sender_id must fit in one byte")
@invariant(lambda self: 0 <= self.receiver_id <= 0xFF, "receiver_id must fit in one byte")
@invariant(lambda self: 0 <= self.fcs < self.schema.checksum.max_value, "fcs must fit in the schema's checksum width")
class PacketHeader:
    """A protocol header bound to a specific :class:`PacketSchema`.

    Attributes:
        schema: The schema this header was built against. Determines
            which fields :meth:`to_bytes` writes and :meth:`from_bytes`
            expects.
        seq_num: Per-direction sequence counter, wrapping at 255.
            Meaningful only when ``schema.sequence`` is
            :attr:`~ecomm.protocol.sequence.SequencePolicy.SEQUENCED`;
            managed by :class:`ecomm.channels.reliable.ReliableChannel`.
            Application code should treat it as opaque.
        sender_id: Identifier of the node that originated this packet.
            Meaningful only when ``schema.topology`` is
            :attr:`~ecomm.protocol.topology.Topology.NETWORK`. Defaults
            to ``schema.board_id``.
        receiver_id: Identifier of the intended recipient. Meaningful
            only when ``schema.topology`` is
            :attr:`~ecomm.protocol.topology.Topology.NETWORK`. Caller
            must assign a valid node id (or
            :data:`ecomm.protocol.config.BROADCAST_ADDRESS`) before the
            packet is passed to a channel's ``send()``.
        fcs: Frame check sequence. Zero until
            :func:`ecomm.protocol.validator.seal` is called; meaningful
            only when ``schema.checksum`` is not
            :attr:`~ecomm.protocol.checksum.ChecksumPolicy.NONE`.
    """

    __slots__ = ("schema", "_byte", "seq_num", "sender_id", "receiver_id", "fcs")

    def __init__(
        self,
        schema: PacketSchema,
        type_: HeaderType = HeaderType.DATA,
        options: HeaderOptions = HeaderOptions.NONE,
    ) -> None:
        """Construct a wire-ready header with a given type and options.

        Packs ``type_`` into bits 7..5, ``options`` (masked to
        :data:`~ecomm.protocol.header_options.HEADER_OPTIONS_MASK`) into
        bits 4..2, and :data:`~ecomm.protocol.config.PROTOCOL_VERSION`
        into bits 1..0. ``sender_id`` defaults to ``schema.board_id``;
        all other fields are zero-initialized, mirroring the two-parameter
        ``packet_header`` constructor's postconditions in
        ``packet_header.hpp``.

        Args:
            schema: The schema this header is built against.
            type_: Packet classification.
            options: OR-combination of :class:`HeaderOptions` flags.
        """
        self.schema = schema
        self._byte = (
            ((int(type_) & _TYPE_MASK) << _TYPE_SHIFT)
            | (int(options) & HEADER_OPTIONS_MASK)
            | (config.PROTOCOL_VERSION & _VERSION_MASK)
        )
        self.seq_num = 0
        self.sender_id = schema.board_id if schema.topology is Topology.NETWORK else 0
        self.receiver_id = 0
        self.fcs = 0

    @property
    def raw_type(self) -> int:
        """The raw 3-bit type field (bits 7..5) as an integer ``0..7``.

        Unlike :attr:`type`, this never raises: it exposes the encoding
        even when it is one of the reserved values (``6`` / ``7``) that
        has no :class:`HeaderType` member yet. Use it to inspect or route
        on a possibly-unknown type without a ``try``/``except`` -- e.g.
        ``if header.raw_type == HeaderType.DATA`` works, and
        ``header.raw_type in (6, 7)`` detects a reserved type safely.
        """
        return (self._byte >> _TYPE_SHIFT) & _TYPE_MASK

    @property
    def type(self) -> HeaderType:
        """Packet classification extracted from bits 7..5.

        Returns:
            The decoded :class:`HeaderType`.

        Raises:
            MalformedPacketError: The type field holds a reserved
                encoding (``0x6`` / ``0x7``) with no assigned
                :class:`HeaderType`. This can only occur on a decoded
                (received) packet; see :attr:`raw_type` for a
                non-raising alternative.
        """
        raw_type = self.raw_type
        try:
            return HeaderType(raw_type)
        except ValueError:
            raise MalformedPacketError(
                f"header type field holds reserved value 0x{raw_type:X}; "
                f"only 0x0..0x5 are assigned HeaderType values. "
                f"Use PacketHeader.raw_type to inspect it without raising."
            ) from None

    @property
    def options(self) -> HeaderOptions:
        """All option flags extracted from bits 4..2.

        Prefer :meth:`has` for single-flag tests.
        """
        return HeaderOptions(self._byte & HEADER_OPTIONS_MASK)

    def has(self, option: HeaderOptions) -> bool:
        """Test whether every bit in ``option`` is set.

        Args:
            option: Flag (or OR-combination of flags) to test for.

        Returns:
            ``True`` iff all bits of ``option`` are set in :attr:`options`.
        """
        return (self.options & option) == option

    @property
    def version(self) -> int:
        """Protocol version extracted from bits 1..0."""
        return self._byte & _VERSION_MASK

    @property
    def raw(self) -> int:
        """The raw packed protocol byte."""
        return self._byte

    @ensure(
        lambda self, result: len(result) == self.schema.header_size,
        "a serialized header must be exactly schema.header_size bytes",
    )
    def to_bytes(self) -> bytes:
        """Serialize this header to its wire representation.

        Field presence and order follow ``schema`` exactly (protocol
        byte, then ``seq_num`` if sequenced, then ``sender_id`` +
        ``receiver_id`` if networked, then ``fcs`` if checksummed) --
        see the module-level wire layout diagram.

        Returns:
            Exactly ``schema.header_size`` bytes.
        """
        out = bytearray()
        out.append(self._byte)
        if self.schema.sequence.size:
            out.append(self.seq_num)
        if self.schema.topology is Topology.NETWORK:
            out.append(self.sender_id)
            out.append(self.receiver_id)
        if self.schema.checksum.size:
            out += struct.pack("<" + self.schema.checksum.struct_format, self.fcs)
        return bytes(out)

    @classmethod
    @require(lambda schema, data: len(data) == schema.header_size, "data must be exactly schema.header_size bytes")
    @ensure(lambda schema, result: result.schema == schema, "the decoded header must be bound to the given schema")
    def from_bytes(cls, schema: PacketSchema, data: bytes) -> "PacketHeader":
        """Deserialize a header from its wire representation.

        Args:
            schema: The schema ``data`` was encoded against.
            data: Exactly ``schema.header_size`` bytes.

        Returns:
            A :class:`PacketHeader` with fields decoded from ``data``.
        """
        header = cls(schema)
        cursor = 0
        header._byte = data[cursor]
        cursor += 1
        if schema.sequence.size:
            header.seq_num = data[cursor]
            cursor += 1
        if schema.topology is Topology.NETWORK:
            header.sender_id = data[cursor]
            header.receiver_id = data[cursor + 1]
            cursor += 2
        if schema.checksum.size:
            (header.fcs,) = struct.unpack_from("<" + schema.checksum.struct_format, data, cursor)
        return header
