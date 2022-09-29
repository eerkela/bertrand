from cpython cimport datetime
from functools import lru_cache

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


##########################
####    SUPERTYPES    ####
##########################


cdef class TimedeltaType(ElementType):
    """Timedelta supertype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = True
        self.supertype = None
        self.subtypes = (
            PandasTimedeltaType, PyTimedeltaType, NumpyTimedelta64Type
        )
        self.atomic_type = None
        self.extension_type = None
        self.min = -291061508645168391112156800000000000
        self.max = 291061508645168391112243200000000000
        self.slug = "timedelta"

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"is_categorical={self.is_categorical}, "
            f"is_sparse={self.is_sparse}"
            f")"
        )

    def __str__(self) -> str:
        cdef str result = self.slug

        # append extensions
        if self.is_categorical:
            result = f"categorical[{result}]"
        if self.is_sparse:
            result = f"sparse[{result}]"

        return result


########################
####    SUBTYPES    ####
########################


cdef class PandasTimedeltaType(TimedeltaType):
    """`pandas.Timedelta` timedelta subtype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = True
        self.supertype = TimedeltaType
        self.subtypes = ()
        self.atomic_type = pd.Timedelta
        self.extension_type = None
        self.min = -2**63 + 1
        self.max = 2**63 - 1
        self.slug = "timedelta[pandas]"


cdef class PyTimedeltaType(TimedeltaType):
    """`datetime.timedelta` timedelta subtype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = True
        self.supertype = TimedeltaType
        self.subtypes = ()
        self.atomic_type = datetime.timedelta
        self.extension_type = None
        self.min = -86399999913600000000000
        self.max = 86399999999999999999000
        self.slug = "timedelta[python]"


cdef class NumpyTimedelta64Type(TimedeltaType):
    """`numpy.timedelta64` timedelta subtype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        str unit = None,
        unsigned long step_size = 1
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = True
        self.supertype = TimedeltaType
        self.subtypes = ()
        self.atomic_type = np.timedelta64
        self.extension_type = None
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000
        self.unit = unit

        if step_size < 1:
            raise ValueError(f"`step_size` must be >= 1, not {step_size}")
        self.step_size = step_size

        if self.unit is None:
            self.slug = "m8"
        else:
            if self.step_size == 1:
                self.slug = f"m8[{unit}]"
            else:
                self.slug = f"m8[{step_size}{unit}]"

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"is_categorical={self.is_categorical}, "
            f"is_sparse={self.is_sparse}, "
            f"unit={self.unit}, "
            f"step_size={self.step_size}"
            f")"
        )

