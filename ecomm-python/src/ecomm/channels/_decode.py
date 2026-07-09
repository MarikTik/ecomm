"""Shared decode/validate/address-filter step for :class:`~ecomm.channels.base.Channel`
and :class:`~ecomm.channels.async_base.AsyncChannel`.

Both channel hierarchies turn raw received bytes into a usable
:class:`~ecomm.protocol.packet.Packet` the same way: decode, run
:func:`~ecomm.protocol.validator.is_valid`, and (for network-topology
packets) check the ``receiver_id`` against this board's id or broadcast --
mirroring the filter added to ``channel.tpp``'s ``try_receive`` on
2026-05-28. Kept in one place so the sync and async implementations cannot
drift apart.
"""

from __future__ import annotations

from ecomm._typing import beartype
from icontract import ensure

from ecomm.protocol.config import BROADCAST_ADDRESS
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.topology import Topology
from ecomm.protocol.validator import is_valid


@beartype
@ensure(
    lambda schema, result: result is None or result.schema == schema,
    "a decoded packet must be bound to the given schema",
)
def decode_validated_and_addressed(schema: PacketSchema, raw: bytes) -> Packet | None:
    """Decode raw wire bytes into a validated, correctly-addressed packet.

    Args:
        schema: The schema ``raw`` was encoded against.
        raw: Exactly ``schema.packet_size`` bytes.

    Returns:
        The decoded :class:`~ecomm.protocol.packet.Packet` if it passes
        :func:`~ecomm.protocol.validator.is_valid` and, for
        network-topology schemas, is addressed to ``schema.board_id`` or
        :data:`~ecomm.protocol.config.BROADCAST_ADDRESS`; ``None``
        otherwise.
    """
    packet = Packet.from_bytes(schema, raw)
    if not is_valid(packet):
        return None

    if schema.topology is Topology.NETWORK:
        dest = packet.header.receiver_id
        if dest != schema.board_id and dest != BROADCAST_ADDRESS:
            return None

    return packet
