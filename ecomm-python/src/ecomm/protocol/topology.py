"""``Topology`` -- per-schema communication shape selector.

Mirrors ``ecomm::protocol::topology`` in ``ecomm/protocol/topology.hpp``.

A device may participate in multiple links simultaneously, each with its own
communication shape -- e.g. a UART leaf link that is strictly
point-to-point and a Wi-Fi link that is part of a multi-node mesh. On the
C++ side topology is a per-instantiation template parameter of
``packet_header``/``packet``; on the Python side it is a field of
:class:`ecomm.protocol.schema.PacketSchema`, played at object-construction
time instead of compile time.
"""

from __future__ import annotations

from enum import IntEnum


class Topology(IntEnum):
    """Communication shape selector applied per :class:`PacketSchema`.

    Affects only header layout (presence of ``sender_id`` / ``receiver_id``).
    Does not imply anything about routing logic.

    Attributes:
        POINT_TO_POINT: No sender/receiver id fields in the header.
        NETWORK: Sender and receiver id fields present in the header.
    """

    POINT_TO_POINT = 0
    NETWORK = 1
