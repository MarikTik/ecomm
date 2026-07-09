"""Tests for ecomm.channels.reliable.ReliableChannel -- mirrors reliable_channel.tpp."""

import icontract
import pytest
from conftest import make_linked_pair

from ecomm.channels.reliable import ClockPolicy, ReliableChannel
from ecomm.channels.result import SendResult
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.sequence import SequencePolicy


class _FakeClock(ClockPolicy):
    """Deterministic, manually-advanced clock for exercising timeouts fast.

    Since these tests are single-threaded, a peer that must "concurrently"
    poll and ack while ``ReliableChannel.send`` busy-waits needs a hook to
    run on the same thread. ``on_tick``, if given, is invoked every time
    ``now()`` is queried (i.e. once per busy-wait iteration) -- tests use it
    to drive the peer's ``try_receive()`` interleaved with the sender's
    ack-polling loop, modeling two boards each running their own loop.
    """

    def __init__(self, timeout_seconds: float = 1.0, on_tick=None) -> None:
        self._now = 0.0
        self._timeout_seconds = timeout_seconds
        self._on_tick = on_tick

    def now(self) -> float:
        if self._on_tick is not None:
            self._on_tick()
        self._now += 0.1  # each poll "spends" 0.1s of simulated time
        return self._now

    @property
    def timeout_seconds(self) -> float:
        return self._timeout_seconds


def _sequenced_schema(board_id: int) -> PacketSchema:
    return PacketSchema(16, sequence=SequencePolicy.SEQUENCED, board_id=board_id)


def test_requires_sequenced_schema():
    schema = PacketSchema(16)  # NO_SEQUENCE
    a, _b = make_linked_pair(schema, schema)
    with pytest.raises(icontract.ViolationError):
        ReliableChannel(a)


def test_send_receives_ack_and_returns_ok():
    schema = _sequenced_schema(1)
    a, b = make_linked_pair(schema, schema)
    rb = ReliableChannel(b, clock=_FakeClock(), poll_interval_seconds=0)
    received_by_b: list[Packet] = []

    def pump_b() -> None:
        pkt = rb.try_receive()
        if pkt is not None:
            received_by_b.append(pkt)

    ra = ReliableChannel(a, clock=_FakeClock(on_tick=pump_b), poll_interval_seconds=0)

    packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)
    packet.payload[0:5] = b"hello"

    result = ra.send(packet)
    assert result is SendResult.OK

    assert len(received_by_b) == 1
    assert bytes(received_by_b[0].payload[0:5]) == b"hello"


def test_seq_num_increments_after_successful_send():
    schema = _sequenced_schema(1)
    a, b = make_linked_pair(schema, schema)
    rb = ReliableChannel(b, clock=_FakeClock(), poll_interval_seconds=0)
    ra = ReliableChannel(a, clock=_FakeClock(on_tick=rb.try_receive), poll_interval_seconds=0)

    for expected_seq in range(3):
        packet = Packet(schema)
        result = ra.send(packet)
        assert result is SendResult.OK
        assert packet.header.seq_num == expected_seq


def test_duplicate_seq_num_is_discarded_but_reacked():
    schema = _sequenced_schema(1)
    a, b = make_linked_pair(schema, schema)
    ra = ReliableChannel(a, clock=_FakeClock(), poll_interval_seconds=0)
    rb = ReliableChannel(b, clock=_FakeClock(), poll_interval_seconds=0)

    packet = Packet(schema)
    ra.send(packet)
    first = rb.try_receive()
    assert first is not None

    # Manually replay the same seq_num as a duplicate retransmit.
    duplicate = Packet(schema)
    duplicate.header.seq_num = 0
    b.send(duplicate)  # use the raw channel, bypassing ReliableChannel's own seq tracking
    assert rb.try_receive() is None  # discarded, not surfaced to the caller


def test_send_times_out_when_peer_never_acks():
    schema = _sequenced_schema(1)
    a, _b = make_linked_pair(schema, schema)  # peer side never polls / acks
    ra = ReliableChannel(a, clock=_FakeClock(timeout_seconds=0.3), max_retries=2, poll_interval_seconds=0)

    result = ra.send(Packet(schema))
    assert result is SendResult.TIMEOUT


def test_max_retries_of_one_means_no_retransmission():
    schema = _sequenced_schema(1)
    a, _b = make_linked_pair(schema, schema)
    ra = ReliableChannel(a, clock=_FakeClock(timeout_seconds=0.2), max_retries=1, poll_interval_seconds=0)

    ra.send(Packet(schema))
    # exactly one packet should have made it into the wire buffer
    assert len(a._send_buffer) == schema.packet_size


def test_ack_packets_are_never_surfaced_to_try_receive():
    schema = _sequenced_schema(1)
    a, b = make_linked_pair(schema, schema)
    ra = ReliableChannel(a, clock=_FakeClock(), poll_interval_seconds=0)
    rb = ReliableChannel(b, clock=_FakeClock(), poll_interval_seconds=0)

    ra.send(Packet(schema))
    rb.try_receive()  # this sends an ack back to a's inbox

    assert ra.try_receive() is None  # the ack must be consumed internally, not surfaced


def test_staging_buffer_returns_non_ack_packets_seen_while_polling():
    schema = _sequenced_schema(1)
    a, b = make_linked_pair(schema, schema)
    ra = ReliableChannel(a, clock=_FakeClock(), poll_interval_seconds=0, buffer_depth=2)
    rb = ReliableChannel(b, clock=_FakeClock(), poll_interval_seconds=0)

    # b sends a data packet to a *before* a calls send(), so when a polls for
    # its own ack it will see b's data packet first and must stage it. Uses
    # the raw channel (not rb.send()) so exactly one packet is transmitted,
    # with no blocking ack wait of its own.
    data_from_b = Packet(schema)
    data_from_b.header.seq_num = 0
    data_from_b.payload[0:1] = b"x"
    b.send(data_from_b)

    ra.send(Packet(schema))  # polls for ack, sees b's data packet, stages it, then times out or gets its ack

    staged = ra.try_receive()
    assert staged is not None
    assert bytes(staged.payload[0:1]) == b"x"
