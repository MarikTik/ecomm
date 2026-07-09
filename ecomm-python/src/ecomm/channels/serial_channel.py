"""``SerialChannel`` -- mirrors ``ecomm/channels/arduino_serial_channel.hpp``.

Reads and writes fixed-size packets as raw binary blobs over a
:mod:`serial` (pyserial) port, exactly as ``arduino_serial_channel`` does
over Arduino's ``HardwareSerial``: no framing bytes, no length prefix, no
sync markers -- just ``schema.packet_size`` bytes back to back. Validation
and sealing are handled transparently by :class:`~ecomm.channels.base.Channel`.
"""

from __future__ import annotations

from typing import Any

from ecomm._typing import beartype

import serial

from ecomm.protocol.schema import PacketSchema

from ecomm.channels.base import Channel


@beartype
class SerialChannel(Channel):
    """UART channel for talking to ecomm firmware over a serial port.

    Attributes:
        schema: Inherited from :class:`~ecomm.channels.base.Channel`.
        port: The underlying :class:`serial.Serial` instance.
    """

    def __init__(self, schema: PacketSchema, port: str, baudrate: int = 115200, **serial_kwargs: Any) -> None:
        """Open a serial port and bind it to ``schema``.

        Args:
            schema: The packet schema this channel operates on.
            port: OS device path (e.g. ``"/dev/ttyUSB0"``, ``"COM3"``).
            baudrate: Serial baud rate. Must match the firmware's
                configured baud rate exactly.
            **serial_kwargs: Additional keyword arguments forwarded to
                :class:`serial.Serial` (e.g. ``timeout``, ``parity``).
        """
        super().__init__(schema)
        self.port = serial.Serial(port=port, baudrate=baudrate, **serial_kwargs)

    def _do_send(self, data: bytes) -> None:
        """Write ``data`` to the serial port. Mirrors ``do_send``."""
        self.port.write(data)

    def _do_try_receive(self, size: int) -> bytes | None:
        """Read ``size`` bytes if that many are already buffered.

        Mirrors ``do_try_receive``'s
        ``_serial.available() < sizeof(Packet)`` early-out: never blocks
        waiting for more bytes to arrive.

        Args:
            size: Number of bytes to read.

        Returns:
            Exactly ``size`` bytes, or ``None`` if fewer are currently
            buffered.
        """
        if self.port.in_waiting < size:
            return None
        return self.port.read(size)

    def close(self) -> None:
        """Close the underlying serial port."""
        self.port.close()
