"""Tests for ecomm.protocol.header_type -- mirrors header_type.hpp's wire values."""

from ecomm.protocol.header_type import HeaderType


def test_wire_values_are_stable():
    """Each member's numeric value must match the C++ enum exactly."""
    assert HeaderType.DATA == 0x0
    assert HeaderType.CONTROL == 0x1
    assert HeaderType.AUTH == 0x2
    assert HeaderType.SESSION == 0x3
    assert HeaderType.LOG == 0x4
    assert HeaderType.FIRMWARE == 0x5


def test_fits_in_three_bits():
    """Every defined member must fit in the 3-bit type field (0..7)."""
    for member in HeaderType:
        assert 0 <= member.value <= 0x7
