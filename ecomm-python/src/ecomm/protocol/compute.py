"""Checksum computation engines, mirroring ``ecomm/protocol/compute.tpp``.

Each function here reproduces one ``ecomm::protocol::compute<ChecksumPolicy>``
specialization's *software* code path exactly (the path the C++ side also
takes on any host that is not an ESP32 with ``esp_crc.h`` available -- i.e.
precisely the code path a Python client must match to interoperate). Given a
policy, :func:`compute_checksum` dispatches to the matching function and
returns an unsigned integer already masked to the policy's wire width.

Every algorithm below is implemented directly rather than delegated to a
third-party library, on purpose: a library is only as trustworthy as the
standard it claims to follow, and a "standard" checksum can still differ
from ecomm's exact variant by one undocumented parameter (initial value,
final XOR, bit reflection, ...) -- CRC in particular has no single
canonical form. Each function here is verified directly against the
compiled C++ ``compute<ChecksumPolicy>`` output (see
``tests/protocol/test_compute.py``), which is the only correctness bar
that actually matters for wire interop. Keeping every algorithm
self-contained means that bar can never silently move out from under this
package -- a dependency's next release, or its own upstream bug fix,
cannot change what this file computes.
"""

from __future__ import annotations

from collections.abc import Callable

from ecomm._typing import beartype
from icontract import ensure, require

from ecomm.protocol._crc_tables import CRC8_TABLE, CRC16_TABLE, CRC32_TABLE, CRC64_TABLE
from ecomm.protocol.checksum import ChecksumPolicy


def _sum_generic(data: bytes, width_bits: int) -> int:
    """Additive sum over every byte, wrapping at ``width_bits``.

    Mirrors ``details::sum_generic`` in ``compute.tpp``. The C++ template
    has a word-bulk fast path gated on ``sizeof(SumType) >=
    sizeof(std::size_t)``; that branch is never taken for ``sum8``/``sum16``/
    ``sum32`` (all narrower than a 32- or 64-bit ``size_t``), so the
    byte-by-byte accumulation below is the code path that actually runs on
    every real target, and the one this function reproduces.

    Args:
        data: Bytes to sum.
        width_bits: Accumulator width in bits (8, 16, or 32).

    Returns:
        The sum, masked to ``width_bits`` (matching unsigned integer
        wraparound in C++).
    """
    assert width_bits in (8, 16, 32), f"sum width must be 8, 16, or 32 bits; got {width_bits}"
    mask = (1 << width_bits) - 1
    total = 0
    for byte in data:
        total = (total + byte) & mask
    return total


def _crc_generic(data: bytes, table: tuple[int, ...], width_bits: int) -> int:
    """Table-driven MSB-first CRC, mirroring ``details::crc_generic``.

    Args:
        data: Bytes to checksum.
        table: 256-entry lookup table for the target CRC width.
        width_bits: CRC width in bits (8, 16, 32, or 64).

    Returns:
        The CRC value (``initial=0``, ``final_xor=0``, matching every
        call site in ``compute.tpp``), masked to ``width_bits``.
    """
    assert width_bits in (8, 16, 32, 64), f"CRC width must be 8, 16, 32, or 64 bits; got {width_bits}"
    assert len(table) == 256, f"CRC lookup table must have exactly 256 entries; got {len(table)}"
    mask = (1 << width_bits) - 1
    crc = 0
    for byte in data:
        index = ((crc >> (width_bits - 8)) ^ byte) & 0xFF
        crc = ((crc << 8) ^ table[index]) & mask
    return crc


def _fletcher16(data: bytes) -> int:
    """Mirrors ``compute<fletcher16>::operator()``."""
    sum1 = 0
    sum2 = 0
    for byte in data:
        sum1 = (sum1 + byte) % 255
        sum2 = (sum2 + sum1) % 255
    return (sum2 << 8) | sum1


def _fletcher32(data: bytes) -> int:
    """Mirrors ``compute<fletcher32>::operator()``.

    The C++ implementation reinterprets the buffer as ``const uint16_t*``;
    on every little-endian target ecomm supports (see the endianness note
    in ``error.hpp``), that means consecutive byte pairs are read
    little-endian.
    """
    sum1 = 0
    sum2 = 0
    word_count = len(data) // 2
    for i in range(word_count):
        word = data[2 * i] | (data[2 * i + 1] << 8)
        sum1 = (sum1 + word) % 65535
        sum2 = (sum2 + sum1) % 65535
    if len(data) % 2 != 0:
        last = data[-1]
        sum1 = (sum1 + last) % 65535
        sum2 = (sum2 + sum1) % 65535
    return (sum2 << 16) | sum1


def _adler32(data: bytes) -> int:
    """Mirrors ``compute<adler32>::operator()``. See the module docstring
    for why this (and every other algorithm here) is hand-implemented
    rather than delegated to a library.
    """
    mod_adler = 65521
    sum1 = 1
    sum2 = 0
    for byte in data:
        sum1 = (sum1 + byte) % mod_adler
        sum2 = (sum2 + sum1) % mod_adler
    return (sum2 << 16) | sum1


def _internet16(data: bytes) -> int:
    """Mirrors ``compute<internet16>::operator()`` (RFC 1071)."""
    total = 0
    word_count = len(data) // 2
    for i in range(word_count):
        word = data[2 * i] | (data[2 * i + 1] << 8)
        total += word
    if len(data) % 2 != 0:
        total += data[-1] << 8
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


#: One algorithm per non-NONE policy. A plain dispatch table reads as a
#: complete coverage list at a glance, and keeps adding a new policy to a
#: single line here plus its implementation function above.
_ALGORITHMS: dict[ChecksumPolicy, Callable[[bytes], int]] = {
    ChecksumPolicy.SUM8: lambda data: _sum_generic(data, 8),
    ChecksumPolicy.SUM16: lambda data: _sum_generic(data, 16),
    ChecksumPolicy.SUM32: lambda data: _sum_generic(data, 32),
    ChecksumPolicy.CRC8: lambda data: _crc_generic(data, CRC8_TABLE, 8),
    ChecksumPolicy.CRC16: lambda data: _crc_generic(data, CRC16_TABLE, 16),
    ChecksumPolicy.CRC32: lambda data: _crc_generic(data, CRC32_TABLE, 32),
    ChecksumPolicy.CRC64: lambda data: _crc_generic(data, CRC64_TABLE, 64),
    ChecksumPolicy.FLETCHER16: _fletcher16,
    ChecksumPolicy.FLETCHER32: _fletcher32,
    ChecksumPolicy.ADLER32: _adler32,
    ChecksumPolicy.INTERNET16: _internet16,
}


@beartype
@require(lambda policy: policy is not ChecksumPolicy.NONE, "compute_checksum is undefined for ChecksumPolicy.NONE")
@ensure(lambda policy, result: 0 <= result < policy.max_value, "checksum must fit the policy's wire width")
def compute_checksum(policy: ChecksumPolicy, data: bytes) -> int:
    """Compute the checksum of ``data`` under ``policy``.

    Args:
        policy: Which algorithm to run. Must not be
            :attr:`ChecksumPolicy.NONE` (there is nothing to compute --
            callers should skip calling this entirely for that policy, the
            same way ``validator<packet<..., none>>`` is a no-op
            specialization in C++).
        data: The exact byte region to checksum. Callers are responsible
            for zeroing the FCS field in this region first, matching the
            sealing contract documented in ``validator.hpp``.

    Returns:
        The checksum value, masked to the policy's wire width
        (:attr:`ChecksumPolicy.max_value`).
    """
    return _ALGORITHMS[policy](data)
