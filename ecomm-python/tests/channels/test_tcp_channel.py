"""Tests for ecomm.channels.tcp_channel.TcpChannel.

Uses a real TCP server bound to ``127.0.0.1:0`` (OS-assigned ephemeral
port) so the test exercises the actual socket plumbing (connect, non-blocking
poll via ``select``, internal receive buffering) rather than a double.
"""

from __future__ import annotations

import socket
import time

from ecomm.channels.tcp_channel import TcpChannel
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema


def _make_listening_socket() -> socket.socket:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    return server


def test_send_writes_exact_packet_bytes():
    schema = PacketSchema(16)
    server = _make_listening_socket()
    host, port = server.getsockname()

    channel = TcpChannel(schema, host, port)
    try:
        conn, _addr = server.accept()
        try:
            packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
            channel.send(packet)

            received = conn.recv(schema.packet_size)
            assert received == packet.to_bytes()
        finally:
            conn.close()
    finally:
        channel.close()
        server.close()


def test_try_receive_returns_none_before_full_packet_arrives():
    schema = PacketSchema(16)
    server = _make_listening_socket()
    host, port = server.getsockname()

    channel = TcpChannel(schema, host, port)
    try:
        conn, _addr = server.accept()
        try:
            conn.sendall(b"\x00" * (schema.packet_size - 1))
            time.sleep(0.05)  # give the OS a moment to deliver the bytes
            assert channel.try_receive() is None
        finally:
            conn.close()
    finally:
        channel.close()
        server.close()


def test_try_receive_returns_packet_once_bytes_arrive():
    schema = PacketSchema(16)
    server = _make_listening_socket()
    host, port = server.getsockname()

    channel = TcpChannel(schema, host, port)
    try:
        conn, _addr = server.accept()
        try:
            source = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
            source.payload[0:3] = b"xyz"
            conn.sendall(source.to_bytes())
            time.sleep(0.05)

            received = channel.try_receive()
            assert received is not None
            assert bytes(received.payload[0:3]) == b"xyz"
        finally:
            conn.close()
    finally:
        channel.close()
        server.close()


def test_context_manager_closes_socket():
    schema = PacketSchema(16)
    server = _make_listening_socket()
    host, port = server.getsockname()

    with TcpChannel(schema, host, port) as channel:
        conn, _addr = server.accept()
        conn.close()
        sock = channel.socket

    # After __exit__, the socket should be closed (fileno() raises OSError
    # on some platforms, returns -1 on others -- both indicate closure).
    try:
        fd = sock.fileno()
        assert fd == -1
    except OSError:
        pass

    server.close()
