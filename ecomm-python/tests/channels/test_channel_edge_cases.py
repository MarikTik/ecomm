"""Edge-case / robustness tests for the channels layer.

Covers the schema-match preconditions added to ``send`` on every channel
type, plus :class:`ReliableChannel`'s constructor guards. Uses the same
in-memory ``QueueChannel`` doubles as the other channel tests.
"""

import icontract
import pytest
from conftest import make_linked_pair

from ecomm.channels.reliable import MonotonicClockPolicy, ReliableChannel
from ecomm.protocol.checksum import ChecksumPolicy
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.sequence import SequencePolicy


def test_channel_send_rejects_foreign_schema_packet():
    channel_schema = PacketSchema(16)
    foreign_schema = PacketSchema(24, checksum=ChecksumPolicy.CRC16)
    a, _b = make_linked_pair(channel_schema, channel_schema)

    foreign_packet = Packet(foreign_schema)
    with pytest.raises(icontract.ViolationError):
        a.send(foreign_packet)


def test_channel_send_accepts_equal_value_schema():
    # Two schemas equal by value are wire-compatible; send must accept them.
    a, _b = make_linked_pair(PacketSchema(16), PacketSchema(16))
    packet = Packet(PacketSchema(16))  # distinct object, equal value
    a.send(packet)  # must not raise


def test_reliable_channel_rejects_negative_poll_interval():
    schema = PacketSchema(16, sequence=SequencePolicy.SEQUENCED)
    a, _b = make_linked_pair(schema, schema)
    with pytest.raises(icontract.ViolationError):
        ReliableChannel(a, poll_interval_seconds=-0.001)


def test_reliable_channel_rejects_zero_max_retries():
    schema = PacketSchema(16, sequence=SequencePolicy.SEQUENCED)
    a, _b = make_linked_pair(schema, schema)
    with pytest.raises(icontract.ViolationError):
        ReliableChannel(a, max_retries=0)


def test_reliable_channel_send_rejects_foreign_schema_packet():
    schema = PacketSchema(16, sequence=SequencePolicy.SEQUENCED)
    foreign = PacketSchema(24, sequence=SequencePolicy.SEQUENCED, checksum=ChecksumPolicy.CRC16)
    a, _b = make_linked_pair(schema, schema)
    rc = ReliableChannel(a, poll_interval_seconds=0)

    with pytest.raises(icontract.ViolationError):
        rc.send(Packet(foreign))


def test_monotonic_clock_rejects_non_positive_timeout():
    with pytest.raises(icontract.ViolationError):
        MonotonicClockPolicy(timeout_seconds=0)
    with pytest.raises(icontract.ViolationError):
        MonotonicClockPolicy(timeout_seconds=-1.0)


def test_reliable_channel_context_manager_closes_wrapped_channel():
    schema = PacketSchema(16, sequence=SequencePolicy.SEQUENCED)
    a, _b = make_linked_pair(schema, schema)

    closed = []
    a.close = lambda: closed.append(True)

    with ReliableChannel(a, poll_interval_seconds=0) as rc:
        assert isinstance(rc, ReliableChannel)
        assert closed == []  # not closed until the block exits

    assert closed == [True]  # __exit__ delegated close() to the wrapped channel


# --------------------------------------------------------------------------
# Class @invariant enforcement (fires on the next public method after the
# object's state is corrupted -- the ongoing-state guarantee that a
# constructor-only @require cannot provide).
# --------------------------------------------------------------------------


def _reliable(**kwargs):
    schema = PacketSchema(16, sequence=SequencePolicy.SEQUENCED)
    a, _b = make_linked_pair(schema, schema)
    return ReliableChannel(a, poll_interval_seconds=0, **kwargs)


def test_invariant_fires_when_config_is_corrupted_post_construction():
    rc = _reliable()
    rc.max_retries = 0  # violates the class invariant
    with pytest.raises(icontract.ViolationError):
        rc.try_receive()  # invariant checked before the method body runs


def test_invariant_fires_when_tx_counter_leaves_byte_range():
    rc = _reliable()
    rc._tx_seq = 999  # counter must stay within one byte
    with pytest.raises(icontract.ViolationError):
        rc.try_receive()


def test_invariant_fires_when_staging_ring_overflows():
    rc = _reliable(buffer_depth=1)
    schema = rc.schema
    rc._stage = [Packet(schema), Packet(schema)]  # exceeds buffer_depth
    with pytest.raises(icontract.ViolationError):
        rc.try_receive()


def test_monotonic_clock_invariant_fires_on_corrupted_timeout():
    clock = MonotonicClockPolicy(timeout_seconds=0.5)
    clock._timeout_seconds = -1  # violates the class invariant
    with pytest.raises(icontract.ViolationError):
        clock.now()  # invariant checked around every public method
