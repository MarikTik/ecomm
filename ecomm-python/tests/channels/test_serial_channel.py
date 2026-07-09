"""Tests for ecomm.channels.serial_channel.SerialChannel.

Uses a minimal in-memory double for :class:`serial.Serial` (matching the
``in_waiting`` / ``read`` / ``write`` / ``close`` surface
:class:`~ecomm.channels.serial_channel.SerialChannel` actually calls) rather
than opening a real device, mirroring how the C++ test suite mocks
``HardwareSerial`` in ``tests/ecomm/channels/helpers/arduino/``.
"""

from __future__ import annotations

from ecomm.channels.serial_channel import SerialChannel
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema


class _FakeSerialPort:
    """Minimal double for :class:`serial.Serial`."""

    def __init__(self) -> None:
        self.written = bytearray()
        self._rx_buffer = bytearray()
        self.closed = False

    def feed(self, data: bytes) -> None:
        """Simulate bytes arriving on the wire."""
        self._rx_buffer += data

    @property
    def in_waiting(self) -> int:
        return len(self._rx_buffer)

    def read(self, size: int) -> bytes:
        out = bytes(self._rx_buffer[:size])
        del self._rx_buffer[:size]
        return out

    def write(self, data: bytes) -> None:
        self.written += data

    def close(self) -> None:
        self.closed = True


def _make_channel_with_fake_port(schema: PacketSchema) -> tuple[SerialChannel, _FakeSerialPort]:
    channel = SerialChannel.__new__(SerialChannel)
    channel.schema = schema
    channel.port = _FakeSerialPort()
    return channel, channel.port


def test_send_writes_exact_packet_bytes():
    schema = PacketSchema(16)
    channel, fake_port = _make_channel_with_fake_port(schema)

    packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
    channel.send(packet)

    assert bytes(fake_port.written) == packet.to_bytes()


def test_try_receive_returns_none_below_packet_size():
    schema = PacketSchema(16)
    channel, fake_port = _make_channel_with_fake_port(schema)
    fake_port.feed(b"\x00" * (schema.packet_size - 1))

    assert channel.try_receive() is None


def test_try_receive_returns_packet_once_enough_bytes_buffered():
    schema = PacketSchema(16)
    channel, fake_port = _make_channel_with_fake_port(schema)

    source = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
    source.payload[0:3] = b"abc"
    fake_port.feed(source.to_bytes())

    received = channel.try_receive()
    assert received is not None
    assert bytes(received.payload[0:3]) == b"abc"


def test_close_closes_the_underlying_port():
    schema = PacketSchema(16)
    channel, fake_port = _make_channel_with_fake_port(schema)
    channel.close()
    assert fake_port.closed
