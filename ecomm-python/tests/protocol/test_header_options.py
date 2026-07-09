"""Tests for ecomm.protocol.header_options -- mirrors header_options.hpp."""

from ecomm.protocol.header_options import HEADER_OPTIONS_MASK, HeaderOptions


def test_wire_values_are_stable():
    """Each member's numeric value must already be shifted to its final bit position."""
    assert HeaderOptions.NONE == 0
    assert HeaderOptions.ERROR == 1 << 4
    assert HeaderOptions.ACK == 1 << 3
    assert HeaderOptions.ENCRYPTED == 1 << 2


def test_mask_spans_bits_4_to_2():
    assert HEADER_OPTIONS_MASK == 0b0001_1100


def test_flags_combine_with_bitwise_or():
    combined = HeaderOptions.ERROR | HeaderOptions.ENCRYPTED
    assert combined & HeaderOptions.ERROR
    assert combined & HeaderOptions.ENCRYPTED
    assert not (combined & HeaderOptions.ACK)


def test_all_members_fit_the_mask():
    for member in HeaderOptions:
        assert (member.value & ~HEADER_OPTIONS_MASK) == 0
