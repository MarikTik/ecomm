# ecomm-python

Python client for the [ecomm](../README.md) wire protocol. Byte-exact
packet headers, checksums, and channels, so a regular computer (laptop, CI
runner, Raspberry Pi) can talk to ecomm firmware over serial or TCP/Wi-Fi.

See `../project/guidelines.md` for the contribution and style guide shared
with the C++ side; the Python-specific conventions (contracts via
`icontract`, typing via `beartype`) are documented in each module's
docstring.

## Installation

```bash
uv sync
```

## Quick start

```python
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
```

### Async TCP (many concurrent boards, no polling)

`TcpChannel` polls with `select()`; `AsyncTcpChannel` suspends on
`asyncio.Event` instead, so a caller can `await` several boards
concurrently off a single event loop without a thread or busy-poll per
connection:

```python
import asyncio
from ecomm.protocol import PacketSchema, Topology, ChecksumPolicy, HeaderType, HeaderOptions, Packet
from ecomm.channels import AsyncTcpChannel

async def main():
    schema = PacketSchema(32, Topology.NETWORK, checksum=ChecksumPolicy.NONE, board_id=2)
    async with await AsyncTcpChannel.connect(schema, "192.168.1.42", 8080) as ch:
        packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
        packet.header.receiver_id = 1
        await ch.send(packet)
        reply = await ch.receive()  # suspends until a full packet arrives, no polling

asyncio.run(main())
```

For several boards at once, `await`ing multiple `AsyncTcpChannel.receive()` calls via `asyncio.gather` drives them all off one thread -- see
`tests/channels/test_async_tcp_channel.py::test_two_channels_can_be_awaited_concurrently`
for a worked example.

## Testing

```bash
uv run pytest
```

## Using this from another project

The distribution is named **`ecomm`** (same as the import). It is not
published to PyPI yet, so once it is, installation will be simply:

```bash
uv add ecomm
```

Until then, `uv` can add it straight from this repo or a local checkout.
Note that while the *package* is named `ecomm`, it lives in the
`ecomm-python/` subdirectory of the monorepo (the repo root's `ecomm/`
folder holds the C++ headers), so Git installs point at that subdirectory:

**From a local checkout** (e.g. developing this and a consumer project
side by side):

```bash
uv add --editable /path/to/ecomm/ecomm-python
```

**Straight from Git**, no local checkout needed:

```bash
uv add "ecomm @ git+https://github.com/MarikTik/ecomm.git#subdirectory=ecomm-python"
```

Pin to a tag or commit for reproducibility by appending `@<ref>` to the
Git URL (before the `#subdirectory=...` fragment), e.g.
`git+https://github.com/MarikTik/ecomm.git@v0.1.0#subdirectory=ecomm-python`.

Either way the import is the same:

```python
import ecomm
```
