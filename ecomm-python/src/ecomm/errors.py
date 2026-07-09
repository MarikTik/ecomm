"""Exception hierarchy for ecomm.

On the C++ / embedded side, ecomm never throws -- errors are handled at the
earliest tier (``static_assert``, ``assert``, or an error code carried in the
packet) precisely because exceptions cost too much on a microcontroller.
This Python client runs on a PC or Raspberry Pi, where exceptions are cheap
and idiomatic, so it uses them freely for the one class of problem that has
no compile-time or contract equivalent: **malformed data arriving off the
wire from an untrusted peer.**

The split, by intent:

- **Contracts** (:mod:`icontract` ``@require`` / ``@invariant``) guard
  *programmer errors* -- calling an API with arguments that violate its
  documented preconditions (a too-small packet, an out-of-range id). These
  raise :class:`icontract.ViolationError`, not the exceptions here.
- **Exceptions in this module** signal *runtime wire conditions* -- bytes
  that were received and decoded but do not form a structurally valid
  packet. These are not the caller's fault and can happen on any real link,
  so they are catchable as a family via :class:`EcommError`.

Malformed *error envelopes* remain a deliberate exception to the exception
rule: :func:`ecomm.protocol.error.read_error` reports them via a ``None``
return, mirroring ``as_error``'s ``std::optional`` on the C++ side, because
"there is no valid envelope here" is an expected, routine outcome rather
than an error.
"""

from __future__ import annotations


class EcommError(Exception):
    """Base class for every ecomm-specific exception.

    Catch this to handle any error raised deliberately by the library
    (as opposed to an :class:`icontract.ViolationError` from a violated
    precondition, or a builtin like :class:`ConnectionError` from the
    transport).
    """


class MalformedPacketError(EcommError):
    """Received bytes were decoded but do not form a valid packet.

    Raised for wire conditions that only a *received* packet can exhibit --
    e.g. a header whose 3-bit type field holds one of the reserved
    encodings (``0x6`` / ``0x7``) that "must not appear on the wire until
    assigned" (see ``header_type.hpp``). A packet constructed in-process
    cannot reach these states, so this never fires on the send path.

    Note:
        When the schema uses a checksum, a corrupted packet is normally
        rejected by :func:`ecomm.protocol.validator.is_valid` before any
        field is interpreted, so this is most relevant to
        :attr:`~ecomm.protocol.checksum.ChecksumPolicy.NONE` links or to
        genuinely reserved-but-uncorrupted values sent by a newer peer.
    """
