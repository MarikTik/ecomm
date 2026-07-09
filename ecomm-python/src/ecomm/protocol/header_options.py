"""``HeaderOptions`` flags -- mirrors ``ecomm/protocol/header_options.hpp``.

Independent single-bit flags stored in bits 4..2 of the protocol byte. Each
member's numeric value is already shifted to its final bit position, exactly
as in the C++ ``enum class header_options : std::uint8_t``, so a
``PacketHeader`` can OR the value directly into the packed byte without
additional shifting.

``HeaderOptions`` is an :class:`enum.IntFlag`, so members combine with the
ordinary Python bitwise operators (``|``, ``&``, ``^``, ``~``) the same way
``etools::meta::enable_flags`` lets ``header_options`` combine on the C++
side::

    opts = HeaderOptions.ERROR | HeaderOptions.ENCRYPTED
"""

from __future__ import annotations

from enum import IntFlag

#: Bitmask covering every bit position ``HeaderOptions`` may occupy.
#: Spans bits 4..2 of the protocol byte: ``0b0001_1100``. Mirrors
#: ``header_options_mask`` in ``header_options.hpp``. A header constructor
#: masks caller-supplied option flags with this constant before storing,
#: preventing any spurious bits from corrupting the type or version fields.
HEADER_OPTIONS_MASK: int = 0b0001_1100


class HeaderOptions(IntFlag):
    """Independent single-bit flags that modify how a packet is interpreted.

    Note:
        These values are wire-stable. Never renumber or move an existing
        member -- doing so would silently break backward compatibility with
        any peer running older firmware.

    Attributes:
        NONE: No flags set. Use as a neutral constructor argument.
        ERROR: Payload is an error envelope (see
            :mod:`ecomm.protocol.error`).
        ACK: Reliability acknowledgement (see
            :mod:`ecomm.channels.reliable`).
        ENCRYPTED: Payload is encrypted; decryption is the caller's
            responsibility.
    """

    NONE = 0
    ERROR = 1 << 4
    ACK = 1 << 3
    ENCRYPTED = 1 << 2
