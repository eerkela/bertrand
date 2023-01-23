import decimal

import numpy as np

from pdtypes.type_hints import numeric


cdef class Tolerance:

    def __init__(self, tol):
        if not isinstance(tol, numeric):
            raise TypeError(f"`tol` must be a scalar numeric, not {type(tol)}")

        self.real = decimal.Decimal(np.real(tol))
        if isinstance(tol, (complex, np.complexfloating)):
            self.imag = decimal.Decimal(np.imag(tol))
        else:
            self.imag = self.real

        if self.real < 0 or self.imag < 0:
            raise ValueError(f"`tol` must be a numeric >= 0, not {tol}")
