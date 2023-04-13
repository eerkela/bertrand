"""This module describes a ``Tolerance`` object, which stores a positive
complex value as two separate ``Decimal`` values.
"""
import decimal

import numpy as np


cdef class Tolerance:

    def __init__(self, tol):
        if isinstance(tol, Tolerance):  # copy attributes
            self.real = tol.real
            self.imag = tol.imag
        else:  # parse numeric
            self.real = decimal.Decimal(np.real(tol))
            if isinstance(tol, (complex, np.complexfloating)):
                self.imag = decimal.Decimal(np.imag(tol))
            else:
                self.imag = self.real
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
