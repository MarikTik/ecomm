"""Tests for ecomm.protocol.validator -- mirrors validator.hpp's seal/is_valid contract."""

import pytest

from ecomm.protocol.checksum import ChecksumPolicy
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.validator import is_valid, seal


def test_none_policy_is_always_valid_even_when_unsealed():
    schema = PacketSchema(16, checksum=ChecksumPolicy.NONE)
    packet = Packet(schema)
    assert is_valid(packet)


def test_none_policy_seal_is_a_noop():
    schema = PacketSchema(16, checksum=ChecksumPolicy.NONE)
    packet = Packet(schema)
    before = packet.to_bytes()
    seal(packet)
    assert packet.to_bytes() == before


@pytest.mark.parametrize(
    "policy",
    [
        ChecksumPolicy.SUM8,
        ChecksumPolicy.SUM16,
        ChecksumPolicy.SUM32,
        ChecksumPolicy.CRC8,
        ChecksumPolicy.CRC16,
        ChecksumPolicy.CRC32,
        ChecksumPolicy.CRC64,
        ChecksumPolicy.FLETCHER16,
        ChecksumPolicy.FLETCHER32,
        ChecksumPolicy.ADLER32,
        ChecksumPolicy.INTERNET16,
    ],
)
def test_sealed_packet_is_valid(policy):
    schema = PacketSchema(32, checksum=policy)
    packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
    packet.payload[:] = bytes(range(schema.payload_size))
    seal(packet)
    assert is_valid(packet)


def test_unsealed_nonzero_fcs_is_invalid():
    schema = PacketSchema(32, checksum=ChecksumPolicy.CRC16)
    packet = Packet(schema)
    packet.header.fcs = 0x1234  # never sealed
    assert not is_valid(packet)


def test_corrupted_payload_is_invalid():
    schema = PacketSchema(32, checksum=ChecksumPolicy.CRC32)
    packet = Packet(schema)
    packet.payload[:] = bytes(range(schema.payload_size))
    seal(packet)

    packet.payload[0] ^= 0xFF  # corrupt one byte after sealing
    assert not is_valid(packet)


def test_is_valid_does_not_mutate_the_packet():
    schema = PacketSchema(32, checksum=ChecksumPolicy.CRC16)
    packet = Packet(schema)
    seal(packet)
    before = packet.to_bytes()
    is_valid(packet)
    assert packet.to_bytes() == before


def test_seal_zeroes_fcs_before_recomputing_so_it_is_idempotent():
    """``seal`` always zeroes ``fcs`` before hashing, so calling it again on
    an otherwise-unchanged packet reproduces the same value -- mirrors the
    sealing contract documented in ``validator.hpp`` step 1 ("zero
    packet.header.fcs") preceding step 2 ("hash all PacketSize bytes").
    """
    schema = PacketSchema(32, checksum=ChecksumPolicy.CRC16)
    packet = Packet(schema)
    seal(packet)
    first_fcs = packet.header.fcs
    seal(packet)
    assert packet.header.fcs == first_fcs
