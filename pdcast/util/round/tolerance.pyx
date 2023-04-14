"""This module describes a ``Tolerance`` object, which stores a positive
complex value as two separate ``Decimal`` values.
"""
import decimal

import numpy as np

from pdcast.util.type_hints import numeric


cdef class Tolerance:
    """A simple struct describing a complex tolerance for precision loss
    operations.

    Parameters
    ----------
    tol : numeric
        A positive numeric to convert into a
        :class:`Decimal <python:decimal.Decimal>`\-based :class:`Tolerance`
        object.  If this is a complex value, then its real and imaginary
        components will be considered separately.

    Returns
    -------
    Tolerance
        A complex value that stores its real and imaginary components as
        :class:`Decimal <python:decimal.Decimal>` objects.

    Raises
    ------
    TypeError
        if ``tol`` could not be coerced to a scalar
        :class:`Decimal <python:decimal.Decimal>`.
    ValueError
        If the real or imaginary component of ``tol`` is not positive.
    """

    def __init__(self, tol: numeric):
        # extract real, imaginary components
        real = np.real(tol)
        imag = np.imag(tol) if hasattr(tol, "imag") else real

        # coerce to decimal
        try:
            self.real = decimal.Decimal(real)
            self.imag = decimal.Decimal(imag)
        except TypeError:
            raise TypeError(f"value cannot be coerced to decimal: {tol}")

        # check positive
        if self.real < 0 or self.imag < 0:
            raise ValueError(f"`tol` must be a numeric >= 0, not {tol}")

    def __bool__(self) -> bool:
        return bool(self.real) or bool(self.imag)

    def __int__(self) -> int:
        return int(self.real)

    def __float__(self) -> float:
        return float(self.real)

    def __complex__(self) -> complex:
        return complex(self.real, self.imag)

    def __repr__(self) -> str:
        return f"{type(self).__name__}({self.real}+{self.imag}j)"

    def __str__(self) -> str:
        return f"{self.real}+{self.imag}j"
