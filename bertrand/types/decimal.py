"""This module contains all the prepackaged decimal types for the ``pdcast``
type system.
"""
import decimal

from .base import Type


# https://github.com/pandas-dev/pandas/blob/e246c3b05924ac1fe083565a765ce847fcad3d91/pandas/tests/extension/decimal/array.py


class Decimal(Type):
    """Abstract decimal type."""

    aliases = {"decimal"}


@Decimal.default
class PythonDecimal(Decimal, backend="python"):
    """Python decimal type."""

    aliases = {decimal.Decimal}
    scalar = decimal.Decimal
    missing = decimal.Decimal("nan")
