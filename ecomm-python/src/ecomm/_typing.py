"""Shared :mod:`beartype` configuration for the ecomm package.

Every module imports :data:`beartype` from here rather than from the
``beartype`` package directly, so the whole library shares one runtime
type-checking policy.

The only non-default setting is ``is_pep484_tower=True``, which enables
PEP 484's *implicit numeric tower*: an :class:`int` is accepted wherever a
:class:`float` is annotated (and an ``int``/``float`` wherever ``complex``
is). Without it, beartype is strict and rejects ``timeout_seconds=1`` or
``poll_interval_seconds=0`` -- a papercut, since passing an integer literal
for a duration is idiomatic Python. Turning the tower on makes those calls
Just Work while still rejecting genuinely wrong types (``str``, ``None``,
...).
"""

from __future__ import annotations

from beartype import BeartypeConf
from beartype import beartype as _beartype

#: Package-wide :func:`beartype.beartype` decorator, preconfigured with the
#: PEP 484 numeric tower enabled. Use as a bare ``@beartype`` exactly like
#: the stock decorator.
beartype = _beartype(conf=BeartypeConf(is_pep484_tower=True))
