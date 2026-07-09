"""``SequencePolicy`` -- mirrors ``ecomm/protocol/sequence.hpp``.

Controls whether a header carries a one-byte ``seq_num`` field immediately
after the protocol byte. The field is used by
:class:`ecomm.channels.reliable.ReliableChannel` to match outbound packets
with their acknowledgements; it is absent when the channel layer does not
need reliability.

| Policy         | seq_num field | Wire bytes added |
|----------------|----------------|-------------------|
| ``NO_SEQUENCE``| absent         | 0                 |
| ``SEQUENCED``  | present        | 1                 |
"""

from __future__ import annotations

from enum import IntEnum


class SequencePolicy(IntEnum):
    """Sequence-number policy tag for :class:`ecomm.protocol.schema.PacketSchema`.

    Attributes:
        NO_SEQUENCE: No ``seq_num`` field. The default; leaves the wire
            layout unchanged relative to the pre-sequence protocol era.
        SEQUENCED: Adds a one-byte ``seq_num`` field, wrapping at 255,
            placed immediately after the protocol byte and before any node
            id or FCS fields. Required by
            :class:`ecomm.channels.reliable.ReliableChannel`.
    """

    NO_SEQUENCE = 0
    SEQUENCED = 1

    @property
    def size(self) -> int:
        """Number of bytes this policy adds to the header.

        Returns:
            ``0`` for :attr:`NO_SEQUENCE`, ``1`` for :attr:`SEQUENCED`.
        """
        return 0 if self is SequencePolicy.NO_SEQUENCE else 1
