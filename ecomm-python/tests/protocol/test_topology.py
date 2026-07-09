"""Tests for ecomm.protocol.topology -- mirrors topology.hpp."""

from ecomm.protocol.topology import Topology


def test_wire_values_are_stable():
    assert Topology.POINT_TO_POINT == 0
    assert Topology.NETWORK == 1
