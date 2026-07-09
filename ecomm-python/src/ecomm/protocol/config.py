"""Protocol-wide constants mirroring ``ecomm/protocol/config.hpp``.

The C++ side exposes these as preprocessor macros (``ECOMM_PROTOCOL_VERSION``,
``ECOMM_BOARD_ID``, ...) so firmware can override them per compilation unit.
Python has no preprocessor, so the same values are plain module-level
constants here. Only :data:`PROTOCOL_VERSION` is truly fixed (it is a wire
constant baked into every header byte); the others are supplied per call
site via :class:`ecomm.protocol.schema.PacketSchema` instead of being global,
since a single Python process may talk to several boards with different ids.

Attributes:
    PROTOCOL_VERSION: The 2-bit protocol version stamped into bits 1..0 of
        every header byte. Mirrors ``ECOMM_PROTOCOL_VERSION`` in
        ``config.hpp``, currently ``0``. Peers running a different version
        are not wire-compatible; ``PacketHeader.version()`` lets a receiver
        detect a mismatch.
    MIN_BOARD_ID: Lowest valid unicast board id (inclusive). ``0`` is
        reserved to mean "unassigned / no sender".
    MAX_BOARD_ID: Highest valid unicast board id (inclusive). ``255``
        (:data:`BROADCAST_ADDRESS`) is reserved for broadcast.
    BROADCAST_ADDRESS: The ``receiver_id`` value meaning "every node on this
        link should accept this packet". Mirrors the ``0xFF`` literal used
        in ``channel.tpp``'s ``try_receive`` address filter.
    UNASSIGNED_ADDRESS: The ``sender_id``/``receiver_id`` value meaning "no
        node" -- never a valid unicast target.
    DEFAULT_MAX_ERROR_MESSAGE_LENGTH: Default cap on the human-readable
        message length inside an error envelope. Mirrors
        ``ECOMM_MAX_ERROR_MESSAGE_LENGTH`` in ``config.hpp``.
"""

from __future__ import annotations

PROTOCOL_VERSION: int = 0

MIN_BOARD_ID: int = 1
MAX_BOARD_ID: int = 254
BROADCAST_ADDRESS: int = 255
UNASSIGNED_ADDRESS: int = 0

DEFAULT_MAX_ERROR_MESSAGE_LENGTH: int = 65535
