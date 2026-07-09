"""``seal`` / ``is_valid`` -- mirrors ``ecomm::protocol::validator<Packet>``.

Two operations, exactly as documented in ``validator.hpp``:

- :func:`seal` finalizes a packet before transmission by computing and
  writing the FCS into ``packet.header.fcs``. A no-op when
  ``schema.checksum`` is :attr:`~ecomm.protocol.checksum.ChecksumPolicy.NONE`.
- :func:`is_valid` checks whether a received packet is structurally sound.
  For :attr:`~ecomm.protocol.checksum.ChecksumPolicy.NONE` this is always
  ``True`` (no FCS to check). For every other policy it recomputes the FCS
  over the canonical byte region and compares it against
  ``packet.header.fcs``.

**Checksum region** (what bytes are hashed), reproduced from
``validator.hpp``:

1. Zero ``packet.header.fcs`` in place.
2. Hash all ``schema.packet_size`` bytes of the packet.
3. Write the result back into ``packet.header.fcs``.

``is_valid`` follows the same zeroing step on a local copy before
recomputing, so the comparison is deterministic regardless of what value
``fcs`` currently holds -- and the caller's packet is left observationally
unchanged, matching the ``const_cast`` + restore dance in ``validator.tpp``.
"""

from __future__ import annotations

from ecomm._typing import beartype

from ecomm.protocol.checksum import ChecksumPolicy
from ecomm.protocol.compute import compute_checksum
from ecomm.protocol.packet import Packet


@beartype
def seal(packet: Packet) -> None:
    """Compute and write the FCS into ``packet.header.fcs``.

    Must be called on every outgoing packet before handing it to a
    transport -- :class:`ecomm.channels.base.Channel` does this
    automatically inside ``send()``.

    Args:
        packet: The packet to seal, mutated in place.
    """
    if packet.schema.checksum is ChecksumPolicy.NONE:
        return
    packet.header.fcs = 0
    packet.header.fcs = compute_checksum(packet.schema.checksum, packet.to_bytes())


@beartype
def is_valid(packet: Packet) -> bool:
    """Verify the packet's FCS against a freshly recomputed value.

    Args:
        packet: The received packet to validate. Left unmodified.

    Returns:
        ``True`` unconditionally when ``schema.checksum`` is
        :attr:`~ecomm.protocol.checksum.ChecksumPolicy.NONE`; otherwise
        ``True`` iff the recomputed FCS matches ``packet.header.fcs``.
    """
    if packet.schema.checksum is ChecksumPolicy.NONE:
        return True

    received_fcs = packet.header.fcs
    packet.header.fcs = 0
    recomputed = compute_checksum(packet.schema.checksum, packet.to_bytes())
    packet.header.fcs = received_fcs

    return received_fcs == recomputed
