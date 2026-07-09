"""Raspberry Pi (or any computer) side of the async-TCP serialization example.

Connects to an ESP32 running ``esp_async_wifi_channel`` (see
``esp32_server.cpp`` in this directory) using the asyncio-native
:class:`ecomm.channels.AsyncTcpChannel`, serializes a small ``{name, age}``
record into a packet payload, and prints the greeting the ESP32 sends back.

The point of this example is that **ecomm treats the packet payload as opaque
bytes** — the application decides how to lay it out. Here the Pi packs a
length-prefixed name string followed by a one-byte age; the ESP32 parses
those fields and replies with a length-prefixed greeting string.

Run it with the ``ecomm`` package installed (``uv add ecomm`` once published,
or from this repo per the top-level ``ecomm-python/README.md``)::

    python pi_client.py --host 192.168.1.50 --name Mark --age 22

The ``--host`` is the IP the ESP32 prints on its serial console at boot.

As always for interop, the :class:`PacketSchema` below must match the C++
``packet<...>`` template arguments in ``esp32_server.cpp`` field-for-field.
See ``README.md`` for the matching table.
"""

from __future__ import annotations

import argparse
import asyncio
import struct

from ecomm.channels import AsyncTcpChannel
from ecomm.protocol import (
    ChecksumPolicy,
    HeaderOptions,
    HeaderType,
    Packet,
    PacketSchema,
    SequencePolicy,
    Topology,
)

# --- ecomm identities (must agree with the ESP32 firmware) -----------------

ESP32_BOARD_ID = 1  # == THIS_BOARD_ID / ECOMM_BOARD_ID on the ESP32
PI_BOARD_ID = 2  # this client's id; the ESP32 replies to it

# --- Wire schema -- MUST MATCH the C++ `app_packet` typedef ----------------
#
#   packet<64, topology::network, no_sequence, none>
#
# Same 64-byte total, same network topology, no sequence field, no checksum.
SCHEMA = PacketSchema(
    packet_size=64,
    topology=Topology.NETWORK,
    sequence=SequencePolicy.NO_SEQUENCE,
    checksum=ChecksumPolicy.NONE,
    board_id=PI_BOARD_ID,
)


# --- Application payload layout (this is *our* protocol, not ecomm's) -------
#
# Request record, laid out little-endian to match ecomm's wire convention:
#
#     offset 0            : name_len   (uint8)
#     offset 1            : name       (name_len ASCII bytes)
#     offset 1 + name_len : age        (uint8)
#
# Response record:
#
#     offset 0 : greeting_len (uint8)
#     offset 1 : greeting     (greeting_len ASCII bytes)
#
# Any unused payload bytes stay zero (Packet zero-initializes them).


def encode_person(name: str, age: int) -> bytes:
    """Serialize ``{name, age}`` into the request layout above.

    Args:
        name: ASCII name. Must fit (with its prefix and the age byte) in the
            schema's payload.
        age: 0..255.

    Returns:
        The packed bytes: ``[name_len][name][age]``.
    """
    name_bytes = name.encode("ascii")
    record = struct.pack("<B", len(name_bytes)) + name_bytes + struct.pack("<B", age)
    #                    ^ a one-byte int field; a wider age would use "<H" / "<I".
    if len(record) > SCHEMA.payload_size:
        raise ValueError(f"record ({len(record)} B) exceeds payload_size ({SCHEMA.payload_size})")
    return record


def decode_greeting(payload: bytes | bytearray) -> str:
    """Parse a length-prefixed ASCII string: ``[len][bytes]``."""
    length = payload[0]
    return bytes(payload[1 : 1 + length]).decode("ascii", errors="replace")


async def run(host: str, port: int, name: str, age: int) -> None:
    """Send one ``{name, age}`` record and print the ESP32's greeting."""
    async with await AsyncTcpChannel.connect(SCHEMA, host, port) as channel:
        print(f"[ecomm async_tcp] connected to {host}:{port} as board {PI_BOARD_ID}")

        # Build the request packet addressed to the ESP32 and drop our
        # serialized record into its payload.
        request = Packet(SCHEMA, HeaderType.DATA, HeaderOptions.NONE)
        request.header.receiver_id = ESP32_BOARD_ID
        # request.header.sender_id defaults to SCHEMA.board_id (PI_BOARD_ID).
        record = encode_person(name, age)
        request.payload[: len(record)] = record

        await channel.send(request)
        print(f'[send] to board {ESP32_BOARD_ID}: name="{name}", age={age} ({len(record)} payload bytes)')

        # receive() suspends (no polling) until a full packet arrives. It
        # returns None only if that packet failed validation or was addressed
        # elsewhere -- keep waiting for the real reply.
        reply = None
        while reply is None:
            reply = await asyncio.wait_for(channel.receive(), timeout=5.0)

        greeting = decode_greeting(reply.payload)
        print(f'[recv] from board {reply.header.sender_id}: "{greeting}"')

    print("[ecomm async_tcp] done")


def main() -> None:
    parser = argparse.ArgumentParser(description="ecomm async-TCP serialization client")
    parser.add_argument("--host", required=True, help="ESP32 IP address (printed on its serial console)")
    parser.add_argument("--port", type=int, default=3333, help="TCP port the ESP32 listens on (default: 3333)")
    parser.add_argument("--name", default="Mark", help="name to send (default: Mark)")
    parser.add_argument("--age", type=int, default=22, help="age to send, 0..255 (default: 22)")
    args = parser.parse_args()

    if not 0 <= args.age <= 255:
        parser.error("--age must be in 0..255 (it is serialized as a single byte)")

    try:
        asyncio.run(run(args.host, args.port, args.name, args.age))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
