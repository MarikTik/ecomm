"""Tests for ecomm.protocol.packet.Packet."""

import pytest

from ecomm.protocol.checksum import ChecksumPolicy
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.topology import Topology


def test_default_payload_is_zero_filled():
    schema = PacketSchema(16)
    packet = Packet(schema)
    assert bytes(packet.payload) == b"\x00" * schema.payload_size


def test_payload_size_matches_schema():
    schema = PacketSchema(32, Topology.NETWORK, checksum=ChecksumPolicy.CRC16)
    packet = Packet(schema)
    assert len(packet.payload) == schema.payload_size


def test_to_bytes_length_equals_packet_size():
    schema = PacketSchema(24, Topology.NETWORK, checksum=ChecksumPolicy.CRC32)
    packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
    assert len(packet.to_bytes()) == schema.packet_size


def test_round_trip_through_bytes_preserves_payload():
    schema = PacketSchema(20, Topology.NETWORK)
    packet = Packet(schema, HeaderType.LOG, HeaderOptions.NONE)
    packet.payload[:] = bytes(range(schema.payload_size))

    decoded = Packet.from_bytes(schema, packet.to_bytes())
    assert bytes(decoded.payload) == bytes(packet.payload)
    assert decoded.header.type is HeaderType.LOG


def test_from_bytes_rejects_wrong_length():
    schema = PacketSchema(16)
    with pytest.raises(Exception):
        Packet.from_bytes(schema, b"\x00" * 15)


def test_payload_is_mutable_bytearray():
    schema = PacketSchema(16)
    packet = Packet(schema)
    packet.payload[0] = 0xFF
    assert packet.payload[0] == 0xFF
