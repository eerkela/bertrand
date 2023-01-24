import decimal

import numpy as np

from pdtypes.type_hints import numeric


cdef class Tolerance:

    def __init__(self, tol):
        self.real = decimal.Decimal(np.real(tol))
        if isinstance(tol, (complex, np.complexfloating)):
            self.imag = decimal.Decimal(np.imag(tol))
        else:
            self.imag = self.real

        if self.real < 0 or self.imag < 0:
            raise ValueError(f"`tol` must be a numeric >= 0, not {tol}")

    def __bool__(self) -> bool:
        return bool(self.real) or bool(self.imag)

    def __repr__(self) -> str:
        return f"{type(self).__name__}({self.real}+{self.imag}j)"

    def __str__(self) -> str:
        return f"{self.real}+{self.imag}j"
