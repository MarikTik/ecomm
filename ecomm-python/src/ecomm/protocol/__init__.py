"""ecomm.protocol -- byte-exact wire format, mirroring ``ecomm/protocol/``.

Every symbol here corresponds 1:1 to a type or function in the C++
``ecomm::protocol`` namespace. See the module docstring of each submodule
for the specific C++ header it mirrors.
"""

from ecomm.protocol.checksum import ChecksumPolicy
from ecomm.protocol.compute import compute_checksum
from ecomm.protocol.error import ErrorCode, ErrorView, read_error, write_error
from ecomm.protocol.header import PacketHeader
from ecomm.protocol.header_options import HEADER_OPTIONS_MASK, HeaderOptions
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.sequence import SequencePolicy
from ecomm.protocol.topology import Topology
from ecomm.protocol.validator import is_valid, seal

__all__ = [
    "HEADER_OPTIONS_MASK",
    "ChecksumPolicy",
    "ErrorCode",
    "ErrorView",
    "HeaderOptions",
    "HeaderType",
    "Packet",
    "PacketHeader",
    "PacketSchema",
    "SequencePolicy",
    "Topology",
    "compute_checksum",
    "is_valid",
    "read_error",
    "seal",
    "write_error",
]
