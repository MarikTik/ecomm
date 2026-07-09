"""``PacketSchema`` -- the runtime stand-in for ecomm's C++ template parameters.

On the C++ side, ``packet_header<Topology, SequencePolicy, ChecksumPolicy>``
and ``packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>`` are
compile-time template instantiations: the compiler generates one concrete
struct layout per unique combination of arguments, and ``sizeof(header)`` /
``payload_size`` fall out as compile-time constants (see
``header_layout.hpp``'s eight explicit specializations and
``packet.hpp``'s ``static_assert``s).

Python has no template monomorphization, so :class:`PacketSchema` plays the
same role at object-construction time instead: a single frozen, validated
value that a :class:`ecomm.protocol.header.PacketHeader` and
:class:`ecomm.protocol.packet.Packet` are constructed against. Two packets
built from *equal* schemas are wire-compatible; two packets built from
different schemas are not, exactly as two C++ packets with different
template arguments are different, unrelated types.
"""

from __future__ import annotations

from dataclasses import dataclass

from ecomm._typing import beartype
from icontract import invariant

from ecomm.protocol import config
from ecomm.protocol.checksum import ChecksumPolicy
from ecomm.protocol.sequence import SequencePolicy
from ecomm.protocol.topology import Topology

#: Fixed 1-byte width of the packed protocol byte (type + options + version).
#: Always present; mirrors ``header_layout::_byte`` in every specialization.
_PROTOCOL_BYTE_SIZE: int = 1

#: Wire width, in bytes, of the ``sender_id`` + ``receiver_id`` pair present
#: only when ``Topology == topology::network``. Mirrors ``node_ids`` in
#: ``node_ids.hpp``.
_NODE_IDS_SIZE: int = 2


def _header_size(topology: Topology, sequence: SequencePolicy, checksum: ChecksumPolicy) -> int:
    """Compute the wire size of a header for the given field combination.

    Reproduces the arithmetic implicit in the eight ``header_layout``
    partial specializations: 1 protocol byte, plus ``sequence.size``, plus
    2 node-id bytes when ``topology`` is :attr:`Topology.NETWORK`, plus
    ``checksum.size``.

    Args:
        topology: Whether sender/receiver id fields are present.
        sequence: Whether the sequence-number field is present.
        checksum: Which checksum policy (and therefore FCS width) applies.

    Returns:
        Total header size in bytes.
    """
    size = _PROTOCOL_BYTE_SIZE + sequence.size + checksum.size
    if topology is Topology.NETWORK:
        size += _NODE_IDS_SIZE
    return size


@beartype
@invariant(
    lambda self: config.MIN_BOARD_ID <= self.board_id <= config.MAX_BOARD_ID,
    "board_id must be in range [1, 254]; 0 is reserved (unassigned) and 255 is the broadcast address",
)
@invariant(
    lambda self: self.packet_size > self.header_size,
    "packet_size must be strictly greater than header_size so at least one payload byte exists",
)
@dataclass(frozen=True)
class PacketSchema:
    """Frozen, validated configuration equivalent to a C++ template instantiation.

    Two :class:`Packet`/:class:`PacketHeader` instances are wire-compatible
    if and only if they were built from equal schemas -- same field
    presence, same checksum algorithm, same total size. This mirrors the
    C++ requirement that both peers link against ``packet<>`` instantiated
    with identical template arguments.

    Attributes:
        packet_size: Total size of the packet on the wire, in bytes,
            header included. Must be strictly greater than
            :attr:`header_size` so at least one payload byte exists
            (mirrors the ``static_assert`` in ``packet.hpp``).
        topology: Whether the header carries ``sender_id``/``receiver_id``.
            Defaults to :attr:`Topology.POINT_TO_POINT`, matching
            ``ECOMM_DEFAULT_TOPOLOGY``'s factory default in ``config.hpp``.
        sequence: Whether the header carries a ``seq_num`` field. Defaults
            to :attr:`SequencePolicy.NO_SEQUENCE`; required to be
            :attr:`SequencePolicy.SEQUENCED` when this schema is used with
            :class:`ecomm.channels.reliable.ReliableChannel`.
        checksum: Which checksum policy (if any) produces the FCS field.
            Defaults to :attr:`ChecksumPolicy.NONE`.
        board_id: This node's identifier, used as the default
            ``sender_id`` for outbound packets when ``topology`` is
            :attr:`Topology.NETWORK`. Mirrors ``ECOMM_BOARD_ID`` in
            ``config.hpp``. Valid range ``[1, 254]``; ``0`` is reserved
            ("unassigned") and ``255`` is the broadcast address.

    Note:
        The C++ side also enforces word-alignment of ``PacketSize``
        (``PacketSize % sizeof(std::size_t) == 0``) because DMA and serial
        drivers on the embedded target typically require it. That
        constraint is about the *firmware's* memory bus, not the wire
        format, and ``sizeof(std::size_t)`` differs between a 32-bit ESP
        target and a 64-bit host -- so it is deliberately not enforced
        here. Any ``packet_size`` that compiled successfully in the C++
        firmware already satisfies it; just pass the same value through.
    """

    packet_size: int
    topology: Topology = Topology.POINT_TO_POINT
    sequence: SequencePolicy = SequencePolicy.NO_SEQUENCE
    checksum: ChecksumPolicy = ChecksumPolicy.NONE
    board_id: int = config.MIN_BOARD_ID

    @property
    def header_size(self) -> int:
        """Wire size of the header for this schema, in bytes.

        Returns:
            ``1`` (protocol byte) ``+ sequence.size + (2 if topology is
            NETWORK else 0) + checksum.size``.
        """
        return _header_size(self.topology, self.sequence, self.checksum)

    @property
    def payload_size(self) -> int:
        """Wire size of the payload region for this schema, in bytes.

        Returns:
            ``packet_size - header_size``.
        """
        return self.packet_size - self.header_size
