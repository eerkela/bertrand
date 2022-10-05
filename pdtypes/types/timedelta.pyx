from cpython cimport datetime
from functools import lru_cache

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType, compute_hash


##########################
####    SUPERTYPES    ####
##########################


cdef class TimedeltaType(ElementType):
    """Timedelta supertype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        super(TimedeltaType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True
        )
        self.supertype = None
        self.subtypes = (
            PandasTimedeltaType, PyTimedeltaType, NumpyTimedelta64Type
        )
        self.atomic_type = None
        self.extension_type = None
        self.slug = "timedelta"

        # min/max representable values in ns
        self.min = -291061508645168391112156800000000000
        self.max = 291061508645168391112243200000000000

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"categorical={self.categorical}, "
            f"sparse={self.sparse}"
            f")"
        )

    def __str__(self) -> str:
        cdef str result = self.slug

        # append extensions
        if self.categorical:
            result = f"categorical[{result}]"
        if self.sparse:
            result = f"sparse[{result}]"

        return result


########################
####    SUBTYPES    ####
########################


cdef class PandasTimedeltaType(TimedeltaType):
    """`pandas.Timedelta` timedelta subtype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        super(TimedeltaType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True
        )
        self.supertype = TimedeltaType
        self.subtypes = ()
        self.atomic_type = pd.Timedelta
        self.extension_type = None
        self.slug = "timedelta[pandas]"

        # min/max representable values in ns
        self.min = -2**63 + 1
        self.max = 2**63 - 1


cdef class PyTimedeltaType(TimedeltaType):
    """`datetime.timedelta` timedelta subtype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        super(TimedeltaType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True
        )
        self.supertype = TimedeltaType
        self.subtypes = ()
        self.atomic_type = datetime.timedelta
        self.extension_type = None
        self.slug = "timedelta[python]"

        # min/max representable values in ns
        self.min = -86399999913600000000000
        self.max = 86399999999999999999000


cdef class NumpyTimedelta64Type(TimedeltaType):
    """`numpy.timedelta64` timedelta subtype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False,
        str unit = None,
        unsigned long long step_size = 1
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = TimedeltaType
        self.subtypes = ()
        self.atomic_type = np.timedelta64
        self.extension_type = None

        # add unit, step size
        self.unit = unit
        if step_size < 1:
            raise ValueError(f"`step_size` must be >= 1, not {step_size}")
        self.step_size = step_size

        # compute hash
        self.hash = compute_hash(
            {
                "sparse": sparse,
                "categorical": categorical,
                "nullable": True,
                "base": self.__class__,
                "unit": unit,
                "step_size": step_size
            }
        )

        # min/max representable values in ns
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000

        # get appropriate slug
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
            f"categorical={self.categorical}, "
            f"sparse={self.sparse}, "
            f"unit={self.unit}, "
            f"step_size={self.step_size}"
            f")"
        )

