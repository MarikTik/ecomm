"""Edge-case / robustness tests for the protocol layer.

Every case here exercises a boundary the library now guards deliberately --
via an :mod:`icontract` precondition/invariant, an ``assert``, or a raised
:class:`ecomm.errors.EcommError`. The point is that no malformed input or
misuse fails *silently* or with an *opaque* builtin error (a bare
``struct.error`` or the enum's ``ValueError``); each surfaces a clear,
catchable signal.
"""

import icontract
import pytest

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
from ecomm.protocol.error import ErrorCode, read_error, write_error


# --------------------------------------------------------------------------
# Reserved header-type decoding
# --------------------------------------------------------------------------


@pytest.mark.parametrize("reserved_bits", [0x6, 0x7])
def test_reserved_header_type_raises_clear_error(reserved_bits):
    schema = PacketSchema(16)
    # Put the reserved value into the 3-bit type field (bits 7..5).
    header = PacketHeader.from_bytes(schema, bytes([reserved_bits << 5]))
    with pytest.raises(MalformedPacketError) as exc:
        _ = header.type
    # Message names the offending raw value and points at the escape hatch.
    assert f"0x{reserved_bits:X}" in str(exc.value)
    assert "raw_type" in str(exc.value)


def test_malformed_packet_error_is_an_ecomm_error():
    assert issubclass(MalformedPacketError, EcommError)


@pytest.mark.parametrize("reserved_bits", [0x6, 0x7])
def test_raw_type_never_raises_on_reserved(reserved_bits):
    schema = PacketSchema(16)
    header = PacketHeader.from_bytes(schema, bytes([reserved_bits << 5]))
    # raw_type is the non-raising inspection path.
    assert header.raw_type == reserved_bits


def test_raw_type_matches_type_for_assigned_values():
    schema = PacketSchema(16)
    header = PacketHeader(schema, HeaderType.FIRMWARE, HeaderOptions.NONE)
    assert header.raw_type == int(HeaderType.FIRMWARE)
    assert header.type is HeaderType.FIRMWARE


# --------------------------------------------------------------------------
# Header field range invariants (caught on next method call)
# --------------------------------------------------------------------------


def test_out_of_range_seq_num_is_caught_on_serialize():
    schema = PacketSchema(16, sequence=SequencePolicy.SEQUENCED)
    header = PacketHeader(schema)
    header.seq_num = 999  # invalid; > one byte
    with pytest.raises(icontract.ViolationError):
        header.to_bytes()


def test_negative_receiver_id_is_caught_on_serialize():
    schema = PacketSchema(16, Topology.NETWORK)
    header = PacketHeader(schema)
    header.receiver_id = -1
    with pytest.raises(icontract.ViolationError):
        header.to_bytes()


def test_fcs_exceeding_checksum_width_is_caught():
    schema = PacketSchema(16, checksum=ChecksumPolicy.CRC16)
    header = PacketHeader(schema)
    header.fcs = 0x1_0000  # one past uint16
    with pytest.raises(icontract.ViolationError):
        header.to_bytes()


# --------------------------------------------------------------------------
# Packet consistency
# --------------------------------------------------------------------------


def test_payload_length_change_is_caught_on_serialize():
    schema = PacketSchema(16)
    packet = Packet(schema)
    packet.payload[0:2] = b"grows"  # bytearray slice assignment can change length
    assert len(packet.payload) != schema.payload_size
    with pytest.raises(icontract.ViolationError):
        packet.to_bytes()


def test_header_from_foreign_schema_is_caught():
    schema_a = PacketSchema(16)
    schema_b = PacketSchema(24, checksum=ChecksumPolicy.CRC16)
    packet = Packet(schema_a)
    packet.header = PacketHeader(schema_b)  # mismatched schema
    with pytest.raises(icontract.ViolationError):
        packet.to_bytes()


# --------------------------------------------------------------------------
# Error envelope contracts
# --------------------------------------------------------------------------


@pytest.mark.parametrize("bad_code", [-1, 0x1_0000, 70000])
def test_write_error_rejects_out_of_range_code(bad_code):
    schema = PacketSchema(32, checksum=ChecksumPolicy.NONE)
    packet = Packet(schema, HeaderType.DATA, HeaderOptions.ERROR)
    with pytest.raises(icontract.ViolationError):
        write_error(packet, bad_code, max_message_length=20)


@pytest.mark.parametrize("bad_max", [0, -1, 0x1_0000_0000])
def test_write_error_rejects_bad_max_message_length(bad_max):
    schema = PacketSchema(32, checksum=ChecksumPolicy.NONE)
    packet = Packet(schema, HeaderType.DATA, HeaderOptions.ERROR)
    with pytest.raises(icontract.ViolationError):
        write_error(packet, ErrorCode.OK, max_message_length=bad_max)


@pytest.mark.parametrize("bad_max", [0, -1, 0x1_0000_0000])
def test_read_error_rejects_bad_max_message_length(bad_max):
    schema = PacketSchema(32, checksum=ChecksumPolicy.NONE)
    packet = Packet(schema, HeaderType.DATA, HeaderOptions.ERROR)
    with pytest.raises(icontract.ViolationError):
        read_error(packet, max_message_length=bad_max)


def test_write_error_accepts_user_defined_code_in_range():
    schema = PacketSchema(32, checksum=ChecksumPolicy.NONE)
    packet = Packet(schema, HeaderType.DATA, HeaderOptions.ERROR)
    # A user code with no ErrorCode member, but valid uint16 -> allowed.
    write_error(packet, 0x9999, b"x", max_message_length=20)
    view = read_error(packet, max_message_length=20)
    assert view is not None
    assert view.code == 0x9999


# --------------------------------------------------------------------------
# Postcondition (@ensure) enforcement
# --------------------------------------------------------------------------


def test_from_bytes_binds_result_to_schema():
    # Normal path: the @ensure postconditions hold on a valid round-trip.
    schema = PacketSchema(24, Topology.NETWORK, checksum=ChecksumPolicy.CRC16)
    decoded = Packet.from_bytes(schema, bytes(schema.packet_size))
    assert decoded.schema == schema
    assert len(decoded.payload) == schema.payload_size


def test_compute_checksum_postcondition_fires_if_algorithm_misbehaves(monkeypatch):
    """Prove the ``@ensure`` on ``compute_checksum`` is wired: if an algorithm
    ever returned a value outside the policy's wire width, the postcondition
    must catch it rather than let a too-wide FCS reach the packet.
    """
    from ecomm.protocol import compute

    # Force the CRC16 algorithm to return an out-of-range value.
    monkeypatch.setitem(compute._ALGORITHMS, ChecksumPolicy.CRC16, lambda data: 0x1_0000)
    with pytest.raises(icontract.ViolationError):
        compute.compute_checksum(ChecksumPolicy.CRC16, b"payload")
