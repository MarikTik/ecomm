"""Tests for ecomm.protocol.sequence -- mirrors sequence.hpp."""

from ecomm.protocol.sequence import SequencePolicy


def test_no_sequence_adds_zero_bytes():
    assert SequencePolicy.NO_SEQUENCE.size == 0


def test_sequenced_adds_one_byte():
    assert SequencePolicy.SEQUENCED.size == 1
