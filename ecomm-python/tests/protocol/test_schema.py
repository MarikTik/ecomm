"""Tests for ecomm.protocol.schema.PacketSchema."""

import icontract
import pytest

from ecomm.protocol.checksum import ChecksumPolicy
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.sequence import SequencePolicy
from ecomm.protocol.topology import Topology


def test_header_size_point_to_point_no_sequence_no_checksum():
    schema = PacketSchema(16)
    assert schema.header_size == 1


def test_header_size_network_sequenced_crc16():
    schema = PacketSchema(32, Topology.NETWORK, SequencePolicy.SEQUENCED, ChecksumPolicy.CRC16)
    # 1 (proto byte) + 1 (seq_num) + 2 (node ids) + 2 (crc16) == 6
    assert schema.header_size == 6


def test_payload_size_is_packet_size_minus_header_size():
    schema = PacketSchema(32, Topology.NETWORK, SequencePolicy.SEQUENCED, ChecksumPolicy.CRC16)
    assert schema.payload_size == 32 - 6


def test_default_board_id_is_one():
    assert PacketSchema(16).board_id == 1


@pytest.mark.parametrize("board_id", [0, 255, -1, 300])
def test_invalid_board_id_rejected(board_id):
    with pytest.raises(icontract.ViolationError):
        PacketSchema(16, board_id=board_id)


def test_packet_size_too_small_for_header_rejected():
    # header is 1 byte for the default schema; packet_size must exceed it.
    with pytest.raises(icontract.ViolationError):
        PacketSchema(1)


def test_packet_size_equal_to_header_size_rejected():
    schema_header_size = PacketSchema(16).header_size
    with pytest.raises(icontract.ViolationError):
        PacketSchema(schema_header_size)


def test_schema_is_frozen():
    schema = PacketSchema(16)
    with pytest.raises(Exception):
        schema.packet_size = 32
