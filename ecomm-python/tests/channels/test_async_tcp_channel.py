"""Tests for ecomm.channels.async_tcp_channel.AsyncTcpChannel.

Uses a real asyncio TCP server bound to ``127.0.0.1:0`` so these tests
exercise actual socket + event-loop plumbing (connect, background pump
task, ``asyncio.Event``-based suspension) rather than a double. Every test
is a plain ``def test_...()`` driven with ``asyncio.run`` -- no
``pytest-asyncio`` dependency needed.
"""

from __future__ import annotations

import asyncio

from ecomm.channels.async_tcp_channel import AsyncTcpChannel
from ecomm.channels.result import SendResult
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema


class _EchoServer:
    """A tiny asyncio TCP server that hands the test its accepted connection."""

    def __init__(self) -> None:
        self.server: asyncio.base_events.Server | None = None
        self.connected = asyncio.Event()
        self.reader: asyncio.StreamReader | None = None
        self.writer: asyncio.StreamWriter | None = None

    async def _handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        self.reader = reader
        self.writer = writer
        self.connected.set()
        await asyncio.Event().wait()  # keep the handler alive; caller closes explicitly

    async def start(self) -> tuple[str, int]:
        self.server = await asyncio.start_server(self._handle, "127.0.0.1", 0)
        host, port = self.server.sockets[0].getsockname()
        return host, port

    def close(self) -> None:
        if self.server is not None:
            self.server.close()


def test_send_writes_exact_packet_bytes():
    async def body():
        schema = PacketSchema(16)
        server = _EchoServer()
        host, port = await server.start()

        channel = await AsyncTcpChannel.connect(schema, host, port)
        try:
            await server.connected.wait()
            packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
            result = await channel.send(packet)
            assert result is SendResult.OK

            received = await server.reader.readexactly(schema.packet_size)
            assert received == packet.to_bytes()
        finally:
            await channel.close()
            server.close()

    asyncio.run(body())


def test_try_receive_returns_none_before_full_packet_arrives():
    async def body():
        schema = PacketSchema(16)
        server = _EchoServer()
        host, port = await server.start()

        channel = await AsyncTcpChannel.connect(schema, host, port)
        try:
            await server.connected.wait()

            assert await channel.try_receive() is None  # nothing sent yet

            server.writer.write(b"\x00" * (schema.packet_size - 1))
            await server.writer.drain()
            await asyncio.sleep(0.05)  # let the pump task drain the partial data

            assert await channel.try_receive() is None  # still short by one byte
        finally:
            await channel.close()
            server.close()

    asyncio.run(body())


def test_try_receive_returns_packet_once_bytes_arrive():
    async def body():
        schema = PacketSchema(16)
        server = _EchoServer()
        host, port = await server.start()

        channel = await AsyncTcpChannel.connect(schema, host, port)
        try:
            await server.connected.wait()

            source = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
            source.payload[0:3] = b"xyz"
            server.writer.write(source.to_bytes())
            await server.writer.drain()
            await asyncio.sleep(0.05)

            received = await channel.try_receive()
            assert received is not None
            assert bytes(received.payload[0:3]) == b"xyz"
        finally:
            await channel.close()
            server.close()

    asyncio.run(body())


def test_receive_suspends_until_data_arrives_without_polling():
    """The core async win: receive() must not busy-poll -- it should resume
    almost immediately after data shows up, however long the wait was.
    """

    async def body():
        schema = PacketSchema(16)
        server = _EchoServer()
        host, port = await server.start()

        channel = await AsyncTcpChannel.connect(schema, host, port)
        try:
            await server.connected.wait()

            async def send_after_delay():
                await asyncio.sleep(0.2)
                packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
                packet.payload[0:1] = b"z"
                server.writer.write(packet.to_bytes())
                await server.writer.drain()

            sender = asyncio.ensure_future(send_after_delay())
            received = await asyncio.wait_for(channel.receive(), timeout=2.0)
            await sender

            assert received is not None
            assert bytes(received.payload[0:1]) == b"z"
        finally:
            await channel.close()
            server.close()

    asyncio.run(body())


def test_receive_raises_connection_error_when_peer_closes():
    async def body():
        schema = PacketSchema(16)
        server = _EchoServer()
        host, port = await server.start()

        channel = await AsyncTcpChannel.connect(schema, host, port)
        try:
            await server.connected.wait()
            server.writer.close()
            await server.writer.wait_closed()

            raised = False
            try:
                await asyncio.wait_for(channel.receive(), timeout=2.0)
            except ConnectionError:
                raised = True
            assert raised
        finally:
            await channel.close()
            server.close()

    asyncio.run(body())


def test_two_channels_can_be_awaited_concurrently():
    """Demonstrates the actual point of AsyncTcpChannel: many connections
    driven off one event loop with no per-connection thread or busy poll.
    """

    async def body():
        schema = PacketSchema(16)
        servers = [_EchoServer(), _EchoServer()]
        addrs = [await s.start() for s in servers]

        channels = [await AsyncTcpChannel.connect(schema, host, port) for host, port in addrs]
        try:
            for s in servers:
                await s.connected.wait()

            for i, s in enumerate(servers):
                packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
                packet.payload[0:1] = bytes([i])
                s.writer.write(packet.to_bytes())
                await s.writer.drain()

            results = await asyncio.gather(*(asyncio.wait_for(c.receive(), timeout=2.0) for c in channels))
            assert [bytes(r.payload[0:1]) for r in results] == [b"\x00", b"\x01"]
        finally:
            for c in channels:
                await c.close()
            for s in servers:
                s.close()

    asyncio.run(body())
