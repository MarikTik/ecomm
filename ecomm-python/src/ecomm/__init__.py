"""ecomm: Python client for the ecomm wire protocol.

This package is a byte-exact re-implementation of the on-wire surface of the
``ecomm`` C++ communication library (see the sibling ``ecomm/`` C++ source
tree at the repository root). It lets a regular computer -- a laptop, a
Raspberry Pi, a CI runner -- speak the same packet format as ecomm firmware
running on an ESP32/ESP8266/Arduino-class microcontroller, over a serial
link or a TCP/Wi-Fi link.

Nothing in this package is a "reimagining" of the protocol: every struct
layout, bit position, checksum algorithm, and channel framing rule here is
transcribed from the corresponding C++ header so that bytes produced by one
side decode correctly on the other. Where the C++ side uses compile-time
template parameters (``packet_header<Topology, SequencePolicy,
ChecksumPolicy>``), this package uses a runtime configuration object,
:class:`ecomm.protocol.schema.PacketSchema`, because Python has no
compile-time monomorphization -- the schema plays the same role the
template arguments play in C++, just resolved at object-construction time
instead of at compile time.

Typical usage::

    from ecomm.protocol import PacketSchema, Topology, SequencePolicy, ChecksumPolicy
    from ecomm.protocol import HeaderType, HeaderOptions, Packet
    from ecomm.channels import SerialChannel

    schema = PacketSchema(
        packet_size=32,
        topology=Topology.NETWORK,
        sequence=SequencePolicy.SEQUENCED,
        checksum=ChecksumPolicy.CRC16,
        board_id=2,
    )
    packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
    packet.header.receiver_id = 1
    packet.payload[0:5] = b"hello"

    with SerialChannel(schema, port="/dev/ttyUSB0", baudrate=115200) as ch:
        ch.send(packet)
        reply = ch.try_receive()
"""

from ecomm.errors import EcommError, MalformedPacketError
from ecomm.protocol import (
    ChecksumPolicy,
    HeaderOptions,
    HeaderType,
    Packet,
    PacketHeader,
    PacketSchema,
    SequencePolicy,
    Topology,
)

__all__ = [
    "ChecksumPolicy",
    "EcommError",
    "HeaderOptions",
    "HeaderType",
    "MalformedPacketError",
    "Packet",
    "PacketHeader",
    "PacketSchema",
    "SequencePolicy",
    "Topology",
]

__version__ = "0.1.0"
