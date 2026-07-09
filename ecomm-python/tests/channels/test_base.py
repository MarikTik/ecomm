"""Tests for ecomm.channels.base.Channel -- mirrors channel.hpp / channel.tpp."""

from conftest import make_linked_pair

from ecomm.channels.result import SendResult
from ecomm.protocol.checksum import ChecksumPolicy
from ecomm.protocol.config import BROADCAST_ADDRESS
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.topology import Topology


def test_send_returns_ok():
    schema = PacketSchema(16)
    a, _b = make_linked_pair(schema, schema)
    assert a.send(Packet(schema)) is SendResult.OK


def test_send_seals_the_packet():
    schema = PacketSchema(16, checksum=ChecksumPolicy.CRC16)
    a, b = make_linked_pair(schema, schema)
    packet = Packet(schema)
    packet.header.fcs = 0  # unsealed
    a.send(packet)

    received = b.try_receive()
    assert received is not None  # only valid if seal() actually ran


def test_try_receive_round_trip():
    schema = PacketSchema(16, checksum=ChecksumPolicy.CRC32)
    a, b = make_linked_pair(schema, schema)

    packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
    packet.payload[0:5] = b"hello"
    a.send(packet)

    received = b.try_receive()
    assert received is not None
    assert bytes(received.payload[0:5]) == b"hello"


def test_try_receive_returns_none_when_nothing_available():
    schema = PacketSchema(16)
    a, _b = make_linked_pair(schema, schema)
    assert a.try_receive() is None


def test_try_receive_rejects_corrupted_packet():
    schema = PacketSchema(16, checksum=ChecksumPolicy.CRC16)
    a, b = make_linked_pair(schema, schema)
    a.send(Packet(schema))

    # Corrupt one byte in transit.
    b._recv_buffer[3] ^= 0xFF
    assert b.try_receive() is None


def test_try_receive_filters_unicast_addressed_to_other_node():
    schema_sender = PacketSchema(16, Topology.NETWORK, board_id=1)
    schema_receiver = PacketSchema(16, Topology.NETWORK, board_id=2)
    a, b = make_linked_pair(schema_sender, schema_receiver)

    packet = Packet(schema_sender)
    packet.header.receiver_id = 99  # not board 2, not broadcast
    a.send(packet)

    assert b.try_receive() is None


def test_try_receive_accepts_packet_addressed_to_this_board():
    schema_sender = PacketSchema(16, Topology.NETWORK, board_id=1)
    schema_receiver = PacketSchema(16, Topology.NETWORK, board_id=2)
    a, b = make_linked_pair(schema_sender, schema_receiver)

    packet = Packet(schema_sender)
    packet.header.receiver_id = 2
    a.send(packet)

    assert b.try_receive() is not None


def test_try_receive_accepts_broadcast():
    schema_sender = PacketSchema(16, Topology.NETWORK, board_id=1)
    schema_receiver = PacketSchema(16, Topology.NETWORK, board_id=2)
    a, b = make_linked_pair(schema_sender, schema_receiver)

    packet = Packet(schema_sender)
    packet.header.receiver_id = BROADCAST_ADDRESS
    a.send(packet)

    assert b.try_receive() is not None


def test_point_to_point_ignores_addressing_entirely():
    schema = PacketSchema(16, Topology.POINT_TO_POINT)
    a, b = make_linked_pair(schema, schema)
    a.send(Packet(schema))
    assert b.try_receive() is not None


def test_context_manager_calls_close():
    schema = PacketSchema(16)
    a, _b = make_linked_pair(schema, schema)
    closed = []
    a.close = lambda: closed.append(True)
    with a:
        pass
    assert closed == [True]
