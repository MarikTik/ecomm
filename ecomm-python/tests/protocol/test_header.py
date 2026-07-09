"""Tests for ecomm.protocol.header.PacketHeader.

The parametrized wire-vector test reproduces bytes captured by compiling and
running the real ``packet_header<Topology, SequencePolicy, ChecksumPolicy>``
specializations (via ``validator<Packet>::seal`` over a full ``packet<>``)
from the C++ source, across all eight topology x sequence x checksum
layout combinations. This is the load-bearing correctness test for the
whole package: if any of these vectors regress, a Python client would
silently desync from real firmware.
"""

import pytest

from ecomm.protocol.checksum import ChecksumPolicy
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.sequence import SequencePolicy
from ecomm.protocol.topology import Topology
from ecomm.protocol.validator import seal

# (label, schema, type_, options, seq_num, sender_id, receiver_id, expected_hex)
_CPP_WIRE_VECTORS = [
    (
        "p2p_nos_none",
        PacketSchema(16, Topology.POINT_TO_POINT, SequencePolicy.NO_SEQUENCE, ChecksumPolicy.NONE),
        HeaderType.DATA,
        HeaderOptions.NONE,
        None,
        None,
        None,
        "00a0a1a2a30000000000000000000000",
    ),
    (
        "p2p_nos_crc16",
        PacketSchema(16, Topology.POINT_TO_POINT, SequencePolicy.NO_SEQUENCE, ChecksumPolicy.CRC16),
        HeaderType.CONTROL,
        HeaderOptions.ERROR,
        None,
        None,
        None,
        "3037d5a0a1a2a3000000000000000000",
    ),
    (
        "p2p_seq_none",
        PacketSchema(16, Topology.POINT_TO_POINT, SequencePolicy.SEQUENCED, ChecksumPolicy.NONE),
        HeaderType.DATA,
        HeaderOptions.ACK,
        42,
        None,
        None,
        "082aa0a1a2a300000000000000000000",
    ),
    (
        "p2p_seq_crc32",
        PacketSchema(24, Topology.POINT_TO_POINT, SequencePolicy.SEQUENCED, ChecksumPolicy.CRC32),
        HeaderType.DATA,
        HeaderOptions.NONE,
        7,
        None,
        None,
        "0007a2fdfe57a0a1a2a30000000000000000000000000000",
    ),
    (
        "net_nos_none",
        PacketSchema(16, Topology.NETWORK, SequencePolicy.NO_SEQUENCE, ChecksumPolicy.NONE, board_id=3),
        HeaderType.DATA,
        HeaderOptions.NONE,
        None,
        3,
        9,
        "000309a0a1a2a3000000000000000000",
    ),
    (
        "net_nos_crc8",
        PacketSchema(16, Topology.NETWORK, SequencePolicy.NO_SEQUENCE, ChecksumPolicy.CRC8, board_id=5),
        HeaderType.LOG,
        HeaderOptions.ENCRYPTED,
        None,
        5,
        255,
        "8405ff1da0a1a2a30000000000000000",
    ),
    (
        "net_seq_none",
        PacketSchema(16, Topology.NETWORK, SequencePolicy.SEQUENCED, ChecksumPolicy.NONE, board_id=1),
        HeaderType.SESSION,
        HeaderOptions.NONE,
        200,
        1,
        2,
        "60c80102a0a1a2a30000000000000000",
    ),
    (
        "net_seq_crc16",
        PacketSchema(32, Topology.NETWORK, SequencePolicy.SEQUENCED, ChecksumPolicy.CRC16, board_id=2),
        HeaderType.FIRMWARE,
        HeaderOptions.ACK,
        99,
        2,
        254,
        "a86302fe92a3a0a1a2a300000000000000000000000000000000000000000000",
    ),
]


@pytest.mark.parametrize(
    ("label", "schema", "type_", "options", "seq", "sender", "recv", "expected_hex"),
    _CPP_WIRE_VECTORS,
    ids=[v[0] for v in _CPP_WIRE_VECTORS],
)
def test_matches_cpp_wire_bytes(label, schema, type_, options, seq, sender, recv, expected_hex):
    packet = Packet(schema, type_, options)
    if seq is not None:
        packet.header.seq_num = seq
    if sender is not None:
        packet.header.sender_id = sender
    if recv is not None:
        packet.header.receiver_id = recv
    for i in range(min(4, schema.payload_size)):
        packet.payload[i] = 0xA0 + i
    seal(packet)

    assert packet.to_bytes().hex() == expected_hex


def test_default_constructor_zero_initializes():
    schema = PacketSchema(16, Topology.NETWORK, SequencePolicy.SEQUENCED, ChecksumPolicy.CRC16, board_id=7)
    header = Packet(schema).header
    assert header.raw == 0
    assert header.type is HeaderType.DATA
    assert header.options is HeaderOptions.NONE
    assert header.version == 0
    assert header.seq_num == 0
    assert header.receiver_id == 0
    assert header.fcs == 0


def test_sender_id_defaults_to_board_id_for_network_topology():
    schema = PacketSchema(16, Topology.NETWORK, board_id=42)
    header = Packet(schema).header
    assert header.sender_id == 42


def test_sender_id_is_zero_for_point_to_point():
    schema = PacketSchema(16, Topology.POINT_TO_POINT, board_id=42)
    header = Packet(schema).header
    assert header.sender_id == 0


def test_has_tests_single_flag():
    schema = PacketSchema(16)
    header = Packet(schema, HeaderType.DATA, HeaderOptions.ERROR | HeaderOptions.ACK).header
    assert header.has(HeaderOptions.ERROR)
    assert header.has(HeaderOptions.ACK)
    assert not header.has(HeaderOptions.ENCRYPTED)


def test_round_trip_through_bytes():
    schema = PacketSchema(32, Topology.NETWORK, SequencePolicy.SEQUENCED, ChecksumPolicy.CRC32, board_id=9)
    header = Packet(schema, HeaderType.AUTH, HeaderOptions.ACK).header
    header.seq_num = 123
    header.receiver_id = 55
    header.fcs = 0xDEADBEEF

    data = header.to_bytes()
    assert len(data) == schema.header_size

    decoded = type(header).from_bytes(schema, data)
    assert decoded.type is HeaderType.AUTH
    assert decoded.has(HeaderOptions.ACK)
    assert decoded.seq_num == 123
    assert decoded.sender_id == 9
    assert decoded.receiver_id == 55
    assert decoded.fcs == 0xDEADBEEF
