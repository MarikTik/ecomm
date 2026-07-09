"""Tests for ecomm.protocol.error -- mirrors error.hpp / error.tpp.

The wire-vector test reproduces bytes captured from the real
``error_envelope<PayloadSize>::write`` / ``as_error`` in ``error.hpp``,
compiled and run against a 27-byte payload with
``ECOMM_MAX_ERROR_MESSAGE_LENGTH=20`` (chosen to fit the length field in a
single byte, matching how the C++ test suite shrinks the macro for small
test packets -- see ``tests/CMakeLists.txt``'s ``test_error`` target).
"""

import icontract

from ecomm.protocol.checksum import ChecksumPolicy
from ecomm.protocol.error import ErrorCode, is_user_error_code, read_error, write_error
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema


def _make_error_packet():
    schema = PacketSchema(32, checksum=ChecksumPolicy.NONE)
    return schema, Packet(schema, HeaderType.DATA, HeaderOptions.ERROR)


def test_matches_cpp_wire_bytes():
    schema, packet = _make_error_packet()
    n = write_error(packet, ErrorCode.CHECKSUM_MISMATCH, b"bad crc", max_message_length=20)
    assert n == 10
    assert bytes(packet.payload).hex() == "02000762616420637263000000000000000000000000000000000000000000"


def test_round_trip_with_message():
    schema, packet = _make_error_packet()
    write_error(packet, ErrorCode.TRANSPORT_TIMEOUT, b"no ack", max_message_length=20)

    view = read_error(packet, max_message_length=20)
    assert view is not None
    assert view.code is ErrorCode.TRANSPORT_TIMEOUT
    assert view.message == b"no ack"
    assert view.length == 6


def test_round_trip_without_message():
    schema, packet = _make_error_packet()
    n = write_error(packet, ErrorCode.UNKNOWN_HANDLER_ID, max_message_length=20)
    assert n == 3  # 2 (code) + 1 (length byte) + 0 (message)

    view = read_error(packet, max_message_length=20)
    assert view is not None
    assert view.code is ErrorCode.UNKNOWN_HANDLER_ID
    assert view.message == b""
    assert view.length == 0


def test_read_error_requires_error_flag_by_default():
    schema = PacketSchema(32, checksum=ChecksumPolicy.NONE)
    packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)  # no ERROR flag
    try:
        read_error(packet, max_message_length=20)
        assert False, "expected icontract.ViolationError"
    except icontract.ViolationError:
        pass


def test_read_error_unchecked_skips_the_flag_check():
    schema = PacketSchema(32, checksum=ChecksumPolicy.NONE)
    packet = Packet(schema, HeaderType.DATA, HeaderOptions.NONE)  # no ERROR flag
    write_error(packet, ErrorCode.OK, max_message_length=20)
    view = read_error(packet, max_message_length=20, require_error_flag=False)
    assert view is not None
    assert view.code is ErrorCode.OK


def test_malformed_declared_length_returns_none():
    schema, packet = _make_error_packet()
    write_error(packet, ErrorCode.OK, max_message_length=20)
    # Corrupt the length byte to claim more bytes than the payload can hold.
    packet.payload[2] = 255
    assert read_error(packet, max_message_length=20) is None


def test_is_user_error_code():
    assert not is_user_error_code(ErrorCode.CHECKSUM_MISMATCH)
    assert is_user_error_code(ErrorCode.USER_RANGE_BEGIN)
    assert is_user_error_code(0x5000)


def test_unknown_code_passes_through_as_int():
    schema, packet = _make_error_packet()
    write_error(packet, 0x9999, max_message_length=20)
    view = read_error(packet, max_message_length=20)
    assert view is not None
    assert view.code == 0x9999
    assert isinstance(view.code, int)
