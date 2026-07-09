"""``HeaderType`` -- top-level packet classification.

Mirrors ``ecomm::protocol::header_type`` in ``ecomm/protocol/header_type.hpp``
bit-for-bit: a 3-bit field stored in bits 7..5 of the protocol byte.

Note:
    These values are wire-stable. Never renumber an existing member --
    doing so would silently break backward compatibility with any peer
    running older firmware (or an older version of this package).
"""

from __future__ import annotations

from enum import IntEnum


class HeaderType(IntEnum):
    """Top-level classification of what a packet carries.

    Stored in bits 7..5 of the protocol byte (3 bits). Six values are
    defined; encodings ``0x6`` and ``0x7`` are reserved for future packet
    kinds and must not appear on the wire until assigned.

    Attributes:
        DATA: Generic application data. Most packets use this type.
        CONTROL: Protocol-level commands (reset, sync, configuration).
        AUTH: Authentication or credential exchange.
        SESSION: Session lifecycle: initiation, teardown, handshake.
        LOG: Diagnostic log messages or telemetry.
        FIRMWARE: Firmware image chunks or update-related payloads.
    """

    DATA = 0x0
    CONTROL = 0x1
    AUTH = 0x2
    SESSION = 0x3
    LOG = 0x4
    FIRMWARE = 0x5
