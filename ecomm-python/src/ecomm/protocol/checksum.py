"""``ChecksumPolicy`` -- mirrors ``ecomm/protocol/checksum.hpp``.

Each policy defines the wire width of the FCS (frame check sequence) field
that :class:`ecomm.protocol.header.PacketHeader` appends when the policy is
not :attr:`ChecksumPolicy.NONE`. This module is layout-only, exactly like
its C++ counterpart -- the actual algorithms live in
:mod:`ecomm.protocol.compute`.
"""

from __future__ import annotations

from enum import Enum
from typing import NamedTuple


class _ChecksumInfo(NamedTuple):
    """Wire metadata for one checksum policy.

    Attributes:
        size: Width of the FCS field in bytes on the wire.
        struct_format: :mod:`struct` format character (without byte-order
            prefix) matching ``size``, used to pack/unpack the FCS field.
    """

    size: int
    struct_format: str


class ChecksumPolicy(Enum):
    """Checksum algorithm selector for :class:`ecomm.protocol.schema.PacketSchema`.

    Attributes:
        NONE: No checksum. Disables integrity checking entirely and
            occupies no wire storage. Use for fully trusted links (e.g. a
            channel already wrapped in TCP, which has its own checksum) or
            to minimize per-packet overhead.
        SUM8: 8-bit additive sum. Extremely simple error detection for
            very short frames.
        SUM16: 16-bit additive sum.
        SUM32: 32-bit additive sum.
        CRC8: 8-bit CRC. Better burst-error detection than a sum for small
            frames.
        CRC16: 16-bit CRC (CCITT polynomial, matching the C++ lookup
            table). Common in serial protocols (MODBUS, USB-PD, CAN).
        CRC32: 32-bit CRC. Industry standard for strong integrity
            detection (Ethernet, Wi-Fi, storage, file formats).
        CRC64: 64-bit CRC. Used in extremely high-integrity protocols or
            large data transfers.
        FLETCHER16: 16-bit Fletcher checksum. Faster than CRC for small
            frames while providing decent integrity.
        FLETCHER32: 32-bit Fletcher checksum.
        ADLER32: 32-bit Adler checksum (a modified Fletcher checksum).
        INTERNET16: 16-bit Internet checksum (RFC 1071), the one's
            complement sum used by IP/TCP/UDP headers.
    """

    NONE = "none"
    SUM8 = "sum8"
    SUM16 = "sum16"
    SUM32 = "sum32"
    CRC8 = "crc8"
    CRC16 = "crc16"
    CRC32 = "crc32"
    CRC64 = "crc64"
    FLETCHER16 = "fletcher16"
    FLETCHER32 = "fletcher32"
    ADLER32 = "adler32"
    INTERNET16 = "internet16"

    @property
    def size(self) -> int:
        """Width of the FCS field in bytes on the wire.

        Returns:
            ``0`` for :attr:`NONE`; otherwise the byte width of the
            policy's ``value_type`` (matching ``ChecksumPolicy::size`` in
            ``checksum.hpp``).
        """
        return _CHECKSUM_INFO[self].size

    @property
    def struct_format(self) -> str:
        """:mod:`struct` format character for this policy's FCS field.

        Returns:
            A single-character :mod:`struct` format code with no
            byte-order prefix (callers prepend ``"<"`` for little-endian,
            matching the ecomm wire protocol).
        """
        return _CHECKSUM_INFO[self].struct_format

    @property
    def max_value(self) -> int:
        """Value one past the largest representable FCS value.

        Returns:
            ``2 ** (8 * size)``, i.e. the modulus checksum accumulators
            wrap at -- mirrors unsigned integer overflow of the C++
            ``value_type``. Always ``1`` for :attr:`NONE` (``size == 0``).
        """
        return 1 << (8 * self.size)


_CHECKSUM_INFO: dict[ChecksumPolicy, _ChecksumInfo] = {
    ChecksumPolicy.NONE: _ChecksumInfo(0, ""),
    ChecksumPolicy.SUM8: _ChecksumInfo(1, "B"),
    ChecksumPolicy.SUM16: _ChecksumInfo(2, "H"),
    ChecksumPolicy.SUM32: _ChecksumInfo(4, "I"),
    ChecksumPolicy.CRC8: _ChecksumInfo(1, "B"),
    ChecksumPolicy.CRC16: _ChecksumInfo(2, "H"),
    ChecksumPolicy.CRC32: _ChecksumInfo(4, "I"),
    ChecksumPolicy.CRC64: _ChecksumInfo(8, "Q"),
    ChecksumPolicy.FLETCHER16: _ChecksumInfo(2, "H"),
    ChecksumPolicy.FLETCHER32: _ChecksumInfo(4, "I"),
    ChecksumPolicy.ADLER32: _ChecksumInfo(4, "I"),
    ChecksumPolicy.INTERNET16: _ChecksumInfo(2, "H"),
}
