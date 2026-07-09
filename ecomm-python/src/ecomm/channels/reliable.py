"""``ReliableChannel`` -- mirrors ``ecomm/channels/reliable_channel.hpp``.

Wraps a :class:`~ecomm.channels.base.Channel` and adds stop-and-wait
reliability:

- ``send`` stamps ``header.seq_num``, transmits the packet, then polls for
  a matching ack. If no ack arrives within the clock policy's timeout, the
  packet is retransmitted, up to ``max_retries`` times total. On
  exhaustion :attr:`~ecomm.channels.result.SendResult.TIMEOUT` is returned.

- ``try_receive`` dequeues from an internal staging buffer first, then
  polls the wrapped channel. Valid inbound data packets are automatically
  acknowledged and placed in the staging buffer (or returned directly if
  the buffer is empty). Duplicate packets (stale ``seq_num``) are re-acked
  and silently discarded. Ack packets are consumed internally and never
  surfaced to the caller.

This is a byte-for-byte port of ``reliable_channel.tpp``'s wire protocol
(``seq_num`` semantics, ack framing, staging ring) so a Python peer
interoperates with an ecomm ``reliable_channel<>`` running on firmware. The
one deliberate behavioral difference: ``send``'s inner wait loop calls
:func:`time.sleep` for :attr:`poll_interval_seconds` between poll attempts.
The C++ implementation busy-polls with no yield, which is correct on
bare-metal firmware with no OS to hand control back to, but would peg a CPU
core at 100% here -- adding a short sleep changes nothing about the wire
protocol or the timeout semantics, only how the waiting is spent.

Note:
    ``schema.sequence`` must be
    :attr:`~ecomm.protocol.sequence.SequencePolicy.SEQUENCED`, mirroring
    the ``static_assert`` in ``reliable_channel.hpp``.
"""

from __future__ import annotations

import time
from abc import ABC, abstractmethod

from ecomm._typing import beartype
from icontract import ensure, invariant, require
from ecomm.protocol.header_options import HeaderOptions
from ecomm.protocol.header_type import HeaderType
from ecomm.protocol.packet import Packet
from ecomm.protocol.schema import PacketSchema
from ecomm.protocol.sequence import SequencePolicy

from ecomm.channels.base import Channel
from ecomm.channels.result import SendResult

_SEQ_NUM_MODULUS = 0x100


class ClockPolicy(ABC):
    """Timing source for :class:`ReliableChannel`.

    Mirrors the C++ ``ClockPolicy`` compile-time contract (``tick_type``,
    ``now()``, ``timeout_ticks()``) as a runtime interface instead, since
    Python has no template parameters. Implement this to use a clock other
    than :class:`MonotonicClockPolicy` (e.g. a simulated clock in tests).
    """

    @abstractmethod
    def now(self) -> float:
        """Current time in seconds, from an arbitrary but consistent epoch."""
        raise NotImplementedError

    @property
    @abstractmethod
    def timeout_seconds(self) -> float:
        """How long ``send`` waits for an ack before retransmitting."""
        raise NotImplementedError


@beartype
@invariant(lambda self: self.timeout_seconds > 0, "timeout_seconds must stay positive")
class MonotonicClockPolicy(ClockPolicy):
    """Default :class:`ClockPolicy`, backed by :func:`time.monotonic`.

    Attributes:
        timeout_seconds: How long ``send`` waits for an ack before
            retransmitting.
    """

    # Two complementary icontract tiers here, and their placement is not
    # interchangeable:
    #   * @require on __init__ validates the *constructor argument*.
    #   * @invariant on the *class* enforces the same property for the whole
    #     object lifetime (e.g. it also catches a later
    #     ``clock._timeout_seconds = -1`` mutation on the next method call).
    # @require/@ensure are function decorators -- stacking them on a class
    # would wrap the class in a plain function, breaking
    # isinstance/issubclass. Only @invariant is class-aware. The same split
    # applies to ReliableChannel below.
    @require(lambda timeout_seconds: timeout_seconds > 0, "timeout_seconds must be positive")
    def __init__(self, timeout_seconds: float = 0.5) -> None:
        """Construct a clock policy with the given ack timeout.

        Args:
            timeout_seconds: How long ``send`` waits for an ack before
                retransmitting. Must be positive.
        """
        self._timeout_seconds = timeout_seconds

    def now(self) -> float:
        """Return :func:`time.monotonic`'s current value."""
        return time.monotonic()

    @property
    def timeout_seconds(self) -> float:
        """How long ``send`` waits for an ack before retransmitting."""
        return self._timeout_seconds


@beartype
@invariant(
    lambda self: self.channel.schema.sequence is SequencePolicy.SEQUENCED,
    "the wrapped channel must stay sequenced",
)
@invariant(lambda self: self.max_retries >= 1, "max_retries must stay >= 1")
@invariant(lambda self: self.buffer_depth >= 1, "buffer_depth must stay >= 1")
@invariant(lambda self: self.poll_interval_seconds >= 0, "poll_interval_seconds must stay non-negative")
@invariant(lambda self: 0 <= self._tx_seq <= 0xFF, "the tx sequence counter must stay within one byte")
@invariant(lambda self: 0 <= self._rx_seq <= 0xFF, "the rx sequence counter must stay within one byte")
@invariant(lambda self: len(self._stage) <= self.buffer_depth, "the staging ring must never exceed buffer_depth")
class ReliableChannel:
    """Stop-and-wait reliable wrapper around a :class:`~ecomm.channels.base.Channel`.

    Exposes the same ``send`` / ``try_receive`` surface as
    :class:`~ecomm.channels.base.Channel` but adds acknowledgement,
    retransmission, and duplicate filtering.

    The class-level ``@invariant``\\ s above hold before and after every
    public method call (icontract skips underscore-private helpers, so the
    ack-poll loop is not re-checked mid-flight): the two sequence counters
    stay within a byte, the staging ring never overflows its depth, the
    configuration stays in range, and the wrapped channel stays sequenced.

    Attributes:
        channel: The wrapped unreliable channel.
        clock: Timing source for the ack timeout.
        max_retries: Total transmission attempts before returning
            :attr:`~ecomm.channels.result.SendResult.TIMEOUT`.
        buffer_depth: Number of inbound packet slots in the staging ring.
        poll_interval_seconds: Sleep duration between ack-poll attempts
            inside ``send``'s wait loop. See the module docstring for why
            this exists (it has no C++ equivalent).
    """

    # Constructor-argument preconditions go on __init__, not the class -- see
    # the note on MonotonicClockPolicy above for why (class-level @require
    # would turn ReliableChannel into a function, breaking isinstance).
    @require(
        lambda channel: channel.schema.sequence is SequencePolicy.SEQUENCED,
        "ReliableChannel requires schema.sequence == SequencePolicy.SEQUENCED",
    )
    @require(lambda max_retries: max_retries >= 1, "max_retries must be at least 1")
    @require(lambda buffer_depth: buffer_depth >= 1, "buffer_depth must be at least 1")
    @require(lambda poll_interval_seconds: poll_interval_seconds >= 0, "poll_interval_seconds must be non-negative")
    def __init__(
        self,
        channel: Channel,
        clock: ClockPolicy | None = None,
        max_retries: int = 3,
        buffer_depth: int = 1,
        poll_interval_seconds: float = 0.001,
    ) -> None:
        """Wrap ``channel`` with stop-and-wait reliability.

        Args:
            channel: The channel to wrap. Its ``schema.sequence`` must be
                :attr:`~ecomm.protocol.sequence.SequencePolicy.SEQUENCED`.
            clock: Timing source. Defaults to
                :class:`MonotonicClockPolicy` with a 0.5 second timeout.
            max_retries: Total transmission attempts (initial + retries)
                before giving up. A value of 1 means no retransmission.
            buffer_depth: Number of slots in the inbound staging ring.
                Depth 1 is sufficient for stop-and-wait; increase it if
                the caller's ``try_receive`` rate lags behind the
                remote's send rate.
            poll_interval_seconds: Sleep duration between ack-poll
                attempts; keeps ``send``'s wait loop from spinning at
                100% CPU. Has no effect on wire behavior.
        """
        self.channel = channel
        self.clock = clock if clock is not None else MonotonicClockPolicy()
        self.max_retries = max_retries
        self.buffer_depth = buffer_depth
        self.poll_interval_seconds = poll_interval_seconds

        self._tx_seq = 0
        self._rx_seq = 0
        self._stage: list[Packet] = []

    @property
    def schema(self) -> PacketSchema:
        """The wrapped channel's :class:`~ecomm.protocol.schema.PacketSchema`."""
        return self.channel.schema

    @require(
        lambda self, packet: packet.schema == self.schema,
        "packet.schema must equal the wrapped channel's schema",
    )
    def send(self, packet: Packet) -> SendResult:
        """Send a packet with stop-and-wait acknowledgement.

        **This call blocks** until an ack is received or the retry
        budget is exhausted. Worst-case hold time is
        ``max_retries * clock.timeout_seconds`` seconds.

        Stamps ``packet.header.seq_num`` with the current outbound
        counter, transmits via the wrapped channel, then polls for a
        matching ack. If no ack arrives within
        ``clock.timeout_seconds`` the packet is retransmitted. At most
        ``max_retries`` transmission attempts are made.

        On success the outbound sequence counter is incremented
        (wrapping at 255). The FCS field is recomputed on each
        transmission attempt (by the wrapped channel's own ``send``).

        Args:
            packet: Packet to send. Must have been built from a schema
                equal to the wrapped channel's ``schema``.
                ``header.seq_num`` is overwritten.

        Returns:
            :attr:`~ecomm.channels.result.SendResult.OK` if an ack was
            received within the retry budget, else
            :attr:`~ecomm.channels.result.SendResult.TIMEOUT`.
        """
        packet.header.seq_num = self._tx_seq

        for _attempt in range(self.max_retries):
            self.channel.send(packet)

            start = self.clock.now()
            while (self.clock.now() - start) < self.clock.timeout_seconds:
                if self._poll_ack(self._tx_seq):
                    self._tx_seq = (self._tx_seq + 1) % _SEQ_NUM_MODULUS
                    return SendResult.OK
                time.sleep(self.poll_interval_seconds)

        return SendResult.TIMEOUT

    @ensure(
        lambda self, result: result is None or result.schema == self.schema,
        "a returned packet must belong to this channel's schema",
    )
    def try_receive(self) -> Packet | None:
        """Attempt to receive a data packet.

        Checks the internal staging buffer first, then polls the wrapped
        channel. Inbound data packets (ack bit clear) whose ``seq_num``
        matches the expected inbound counter are accepted: an ack is
        sent back automatically and the packet is returned. Packets with
        a stale ``seq_num`` (duplicate retransmit from the remote) are
        re-acked and discarded. Ack packets are consumed internally.

        Returns:
            The received data packet, or ``None`` if nothing was
            available.
        """
        staged = self._stage_pop()
        if staged is not None:
            return staged

        incoming = self.channel.try_receive()
        if incoming is None:
            return None

        if incoming.header.has(HeaderOptions.ACK):
            return None

        seq = incoming.header.seq_num
        if seq == self._rx_seq:
            self._send_ack(seq)
            self._rx_seq = (self._rx_seq + 1) % _SEQ_NUM_MODULUS
            return incoming

        self._send_ack(seq)
        return None

    def close(self) -> None:
        """Close the wrapped channel, releasing its transport resource.

        Delegates to the wrapped :class:`~ecomm.channels.base.Channel`'s
        ``close()``. The staging ring and sequence counters are plain
        in-object state and need no teardown.
        """
        self.channel.close()

    def __enter__(self) -> "ReliableChannel":
        """Enter a ``with`` block, returning this channel unchanged.

        The wrapped channel is already connected at construction time
        (e.g. :class:`~ecomm.channels.serial_channel.SerialChannel` opens
        its port in ``__init__``), so entry does no work; :meth:`close`
        on exit is what the context manager exists for.
        """
        return self

    def __exit__(self, *exc_info: object) -> None:
        """Exit a ``with`` block, closing the wrapped channel."""
        self.close()

    def _send_ack(self, seq: int) -> None:
        """Send an ack for the packet with the given ``seq_num``."""
        ack = Packet(self.schema, HeaderType.DATA, HeaderOptions.ACK)
        ack.header.seq_num = seq
        self.channel.send(ack)

    def _poll_ack(self, seq: int) -> bool:
        """Poll the wrapped channel once for an ack matching ``seq``.

        Non-ack packets received while polling are pushed onto the
        staging ring rather than discarded, exactly as
        ``reliable_channel.tpp``'s ``poll_ack`` does.

        Returns:
            ``True`` iff a matching ack was received.
        """
        incoming = self.channel.try_receive()
        if incoming is None:
            return False

        if not incoming.header.has(HeaderOptions.ACK):
            self._stage_push(incoming)
            return False

        return incoming.header.seq_num == seq

    def _stage_push(self, packet: Packet) -> bool:
        """Push a packet into the staging ring.

        Returns:
            ``True`` if there was room; ``False`` if the ring was full
            (packet silently dropped, matching ``stage_push`` in
            ``reliable_channel.tpp``).
        """
        if len(self._stage) >= self.buffer_depth:
            return False
        self._stage.append(packet)
        return True

    def _stage_pop(self) -> Packet | None:
        """Pop the oldest packet from the staging ring, if any."""
        if not self._stage:
            return None
        return self._stage.pop(0)
