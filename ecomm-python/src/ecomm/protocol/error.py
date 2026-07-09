"""Error envelope, mirroring ``ecomm::protocol`` in ``error.hpp`` / ``error.tpp``.

When a packet's header has :attr:`~ecomm.protocol.header_options.HeaderOptions.ERROR`
set, its payload region is reinterpreted as an error envelope: a fixed-size
``error_code`` followed by a length-prefixed message.

Wire layout inside the packet payload (for a payload of size ``P``)::

    +----------------+----------------------------+---------------------+----------+
    |   error_code   |   length field             |    message bytes    |   pad    |
    |   (uint16 LE)  |  (uint8/16/32 LE, see below)|    length bytes     |   ...    |
    +----------------+----------------------------+---------------------+----------+
            2                  1, 2, or 4                  length          rest of P

The width of the length field is selected from ``max_message_length``
(default :data:`ecomm.protocol.config.DEFAULT_MAX_ERROR_MESSAGE_LENGTH`) the
same way ``etools::meta::smallest_uint_t`` selects it on the C++ side:
``<= 255`` -> 1 byte, ``<= 65535`` -> 2 bytes (the default), ``<=
4294967295`` -> 4 bytes.

Note:
    ``packet.header`` options are orthogonal to this envelope. Callers
    must set :attr:`~ecomm.protocol.header_options.HeaderOptions.ERROR`
    themselves when constructing the packet (e.g. ``Packet(schema,
    HeaderType.DATA, HeaderOptions.ERROR)``) -- :func:`write_error` only
    touches the payload, exactly as ``error_envelope::write`` only touches
    the payload region on the C++ side.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum

from ecomm._typing import beartype
from icontract import ensure, require

from ecomm.protocol import config
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.packet import Packet

_ERROR_CODE_SIZE = 2  # uint16, always -- independent of the length-field width.

#: Largest error-code value that fits the fixed 2-byte (uint16) code field.
_ERROR_CODE_MAX = 0xFFFF

#: Largest ``max_message_length`` the length-prefix width selection can carry
#: (a 4-byte / uint32 field). Mirrors the ``> 4294967295`` ``#error`` guard in
#: ``config.hpp``.
_MAX_MESSAGE_LENGTH_CEILING = 0xFFFFFFFF


class ErrorCode(IntEnum):
    """Protocol-level error identifiers carried in an error envelope.

    The top byte is a subsystem tag so host-side dispatch can fan out
    without a giant switch:

    - ``0x00xx`` -- framing / packet structure
    - ``0x01xx`` -- transport (serial, wifi, ...)
    - ``0x02xx`` -- dispatch / hub
    - ``0x40xx``+ -- reserved for user-defined application errors (see
      :attr:`USER_RANGE_BEGIN`).

    Codes ``0x0000``..``0x3FFF`` are reserved for the ecomm library itself.
    Codes ``0x4000``..``0xFFFF`` belong to the application consuming
    ecomm -- assign them however you like.

    Note:
        Treat values as part of the wire protocol: once assigned, do not
        reuse or renumber across protocol versions.

    Attributes:
        OK: Sentinel for "no error"; should rarely appear on the wire.
        MALFORMED_HEADER: Header bits could not be parsed.
        CHECKSUM_MISMATCH: Recomputed FCS did not match the packet's FCS.
        VERSION_MISMATCH: Sender uses an incompatible protocol version.
        PAYLOAD_TOO_SMALL: Declared payload smaller than the envelope it
            claims to carry.
        MALFORMED_ERROR_ENVELOPE: Returned via :func:`read_error` when the
            envelope itself is corrupt.
        TRANSPORT_TIMEOUT: Peer did not respond in the configured window.
        TRANSPORT_DISCONNECTED: Underlying link reported a disconnect.
        UNKNOWN_HANDLER_ID: Received handler id has no registered handler.
        HANDLER_NOT_REGISTERED: Handler removed or never installed for
            this id.
        USER_RANGE_BEGIN: First value reserved for user-defined error
            codes. Anything below belongs to the ecomm library; anything
            at or above is free for the consuming application.
    """

    OK = 0x0000
    MALFORMED_HEADER = 0x0001
    CHECKSUM_MISMATCH = 0x0002
    VERSION_MISMATCH = 0x0003
    PAYLOAD_TOO_SMALL = 0x0004
    MALFORMED_ERROR_ENVELOPE = 0x0005

    TRANSPORT_TIMEOUT = 0x0100
    TRANSPORT_DISCONNECTED = 0x0101

    UNKNOWN_HANDLER_ID = 0x0200
    HANDLER_NOT_REGISTERED = 0x0201

    USER_RANGE_BEGIN = 0x4000


def is_user_error_code(code: ErrorCode) -> bool:
    """Test whether ``code`` is in the user-defined range.

    Args:
        code: The error code to classify.

    Returns:
        ``True`` iff ``code >= ErrorCode.USER_RANGE_BEGIN``.
    """
    return int(code) >= int(ErrorCode.USER_RANGE_BEGIN)


@beartype
@dataclass(frozen=True)
class ErrorView:
    """Decoded view of an error envelope.

    Attributes:
        code: Decoded protocol-level error identifier. Typed as
            ``ErrorCode | int`` because a user-defined application code
            (see :attr:`ErrorCode.USER_RANGE_BEGIN`) has no matching
            ``ErrorCode`` member and is passed through as a plain
            ``int``.
        message: The message bytes. Not null-terminated (there is no such
            concept in a Python ``bytes`` object); use ``len(message)`` or
            :attr:`length`.
        length: Number of bytes in :attr:`message`. Redundant with
            ``len(message)`` in Python; kept as a distinct field to mirror
            ``error_view`` in ``error.hpp`` field-for-field.
    """

    code: ErrorCode | int
    message: bytes
    length: int


def _length_field_width(max_message_length: int) -> int:
    """Select the length-field width, mirroring ``etools::meta::smallest_uint_t``.

    Args:
        max_message_length: The configured message capacity.

    Returns:
        ``1``, ``2``, or ``4`` bytes.
    """
    if max_message_length <= 0xFF:
        return 1
    if max_message_length <= 0xFFFF:
        return 2
    return 4


_LENGTH_STRUCT_FORMAT = {1: "B", 2: "H", 4: "I"}


def _prefix_size(max_message_length: int) -> int:
    """Total bytes consumed by the fixed-size code + length prefix.

    Args:
        max_message_length: The configured message capacity.

    Returns:
        ``_ERROR_CODE_SIZE + _length_field_width(max_message_length)``.
    """
    return _ERROR_CODE_SIZE + _length_field_width(max_message_length)


def _valid_max_message_length(max_message_length: int) -> bool:
    """Whether ``max_message_length`` is a usable envelope capacity.

    Mirrors the two ``#error`` guards on ``ECOMM_MAX_ERROR_MESSAGE_LENGTH``
    in ``config.hpp``: it must be strictly positive and must fit in a
    uint32 (the widest length-prefix field this codec emits).

    Args:
        max_message_length: The value to check.

    Returns:
        ``True`` iff ``1 <= max_message_length <= 0xFFFFFFFF``.
    """
    return 1 <= max_message_length <= _MAX_MESSAGE_LENGTH_CEILING


@beartype
@require(
    lambda max_message_length: _valid_max_message_length(max_message_length),
    "max_message_length must be in range [1, 0xFFFFFFFF]",
)
@require(lambda code: 0 <= int(code) <= _ERROR_CODE_MAX, "error code must fit in a uint16 (0..0xFFFF)")
@require(lambda message, max_message_length: len(message) <= max_message_length, "message exceeds max_message_length")
@require(
    lambda packet, max_message_length: _prefix_size(max_message_length) <= len(packet.payload),
    "payload is too small to hold the error code + length prefix",
)
@require(
    lambda packet, message, max_message_length: len(message) <= len(packet.payload) - _prefix_size(max_message_length),
    "message exceeds this packet's message capacity",
)
@ensure(
    lambda message, max_message_length, result: result == _prefix_size(max_message_length) + len(message),
    "the returned byte count must equal prefix_size + len(message)",
)
@ensure(
    lambda packet, result: result <= len(packet.payload),
    "write must never claim to have written more than the payload holds",
)
def write_error(
    packet: Packet,
    code: ErrorCode | int,
    message: bytes = b"",
    *,
    max_message_length: int = config.DEFAULT_MAX_ERROR_MESSAGE_LENGTH,
) -> int:
    """Write an error code with an optional message into a packet's payload.

    Wire layout: ``[error_code (2B)] [length (1-4B)] [message bytes]``. The
    remainder of the payload beyond the written bytes is left untouched.
    Mirrors ``error_envelope<PayloadSize>::write``.

    Args:
        packet: The packet whose payload should receive the envelope.
            ``packet.header`` is not modified -- set
            :attr:`~ecomm.protocol.header_options.HeaderOptions.ERROR`
            yourself when constructing the packet.
        code: Protocol-level error identifier. Typed as ``ErrorCode |
            int`` so user-defined codes (see
            :attr:`ErrorCode.USER_RANGE_BEGIN`) can be written without a
            matching enum member.
        message: The message bytes. Need not be text; any bytes are
            accepted. An empty message is valid and produces a
            zero-length field on the wire.
        max_message_length: Message capacity this envelope was configured
            for. Must match the value ``read_error`` is later called
            with, and mirrors ``ECOMM_MAX_ERROR_MESSAGE_LENGTH``.

    Returns:
        Total bytes written into the payload (``prefix_size + len(message)``).
    """
    length_width = _length_field_width(max_message_length)
    prefix_size = _prefix_size(max_message_length)

    cursor = 0
    packet.payload[cursor : cursor + _ERROR_CODE_SIZE] = struct.pack("<H", int(code))
    cursor += _ERROR_CODE_SIZE
    packet.payload[cursor : cursor + length_width] = struct.pack(
        "<" + _LENGTH_STRUCT_FORMAT[length_width], len(message)
    )
    cursor += length_width
    if message:
        packet.payload[cursor : cursor + len(message)] = message

    return prefix_size + len(message)


@beartype
@require(
    lambda max_message_length: _valid_max_message_length(max_message_length),
    "max_message_length must be in range [1, 0xFFFFFFFF]",
)
@require(
    lambda packet, require_error_flag: not require_error_flag or packet.header.has(HeaderOptions.ERROR),
    "packet header does not have HeaderOptions.ERROR set",
)
def read_error(
    packet: Packet,
    *,
    max_message_length: int = config.DEFAULT_MAX_ERROR_MESSAGE_LENGTH,
    require_error_flag: bool = True,
) -> ErrorView | None:
    """Decode an error envelope from a packet's payload.

    Mirrors ``as_error`` (``require_error_flag=True``, the default) and
    ``as_error_unchecked`` (``require_error_flag=False``) from ``error.hpp``.

    Args:
        packet: The packet to interpret.
        max_message_length: Must match the value used when the envelope
            was written (see :func:`write_error`), so the length-field
            width matches.
        require_error_flag: When ``True`` (the default), this function's
            precondition requires ``packet.header`` to have
            :attr:`~ecomm.protocol.header_options.HeaderOptions.ERROR`
            set -- mirrors the ``assert`` precondition on ``as_error``.
            Pass ``False`` to decode without that check (mirrors
            ``as_error_unchecked``), e.g. in tests.

    Returns:
        A decoded :class:`ErrorView` on success, or ``None`` if the
        envelope is structurally invalid (declared length overruns the
        available payload) -- a malformed envelope is a wire condition,
        not a programmer error, so it is reported through the return
        value rather than an exception.
    """
    length_width = _length_field_width(max_message_length)
    prefix_size = _prefix_size(max_message_length)
    payload = bytes(packet.payload)

    if len(payload) < prefix_size:
        return None

    (raw_code,) = struct.unpack_from("<H", payload, 0)
    (length,) = struct.unpack_from("<" + _LENGTH_STRUCT_FORMAT[length_width], payload, _ERROR_CODE_SIZE)

    available = len(payload) - prefix_size
    if length > available:
        return None

    message = payload[prefix_size : prefix_size + length]
    try:
        code = ErrorCode(raw_code)
    except ValueError:
        code = raw_code  # unknown code (e.g. a user-defined one); pass through as an int

    return ErrorView(code=code, message=message, length=length)
