"""Tests for ecomm.protocol.checksum -- mirrors checksum.hpp's layout-only policies."""

import pytest

from ecomm.protocol.checksum import ChecksumPolicy


def test_none_has_zero_size():
    assert ChecksumPolicy.NONE.size == 0


@pytest.mark.parametrize(
    ("policy", "expected_size"),
    [
        (ChecksumPolicy.SUM8, 1),
        (ChecksumPolicy.SUM16, 2),
        (ChecksumPolicy.SUM32, 4),
        (ChecksumPolicy.CRC8, 1),
        (ChecksumPolicy.CRC16, 2),
        (ChecksumPolicy.CRC32, 4),
        (ChecksumPolicy.CRC64, 8),
        (ChecksumPolicy.FLETCHER16, 2),
        (ChecksumPolicy.FLETCHER32, 4),
        (ChecksumPolicy.ADLER32, 4),
        (ChecksumPolicy.INTERNET16, 2),
    ],
)
def test_policy_size_matches_value_type_width(policy, expected_size):
    assert policy.size == expected_size


def test_max_value_matches_size():
    for policy in ChecksumPolicy:
        if policy is ChecksumPolicy.NONE:
            continue
        assert policy.max_value == 2 ** (8 * policy.size)
