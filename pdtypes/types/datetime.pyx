from cpython cimport datetime

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


##########################
####    SUPERTYPES    ####
##########################


cdef class DatetimeType(ElementType):
    """Datetime supertype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = None
        self.subtypes = (
            PandasTimestampType, PyDatetimeType, NumpyDatetime64Type
        )
        self.atomic_type = None
        self.extension_type = None
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000
        self.slug = "datetime"

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


cdef class PandasTimestampType(DatetimeType):
    """`pandas.Timestamp` datetime subtype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = DatetimeType
        self.subtypes = ()
        self.atomic_type = pd.Timestamp
        self.extension_type = None
        self.min = -2**63 + 1
        self.max = 2**63 - 1
        self.slug = "datetime[pandas]"


cdef class PyDatetimeType(DatetimeType):
    """`datetime.datetime` datetime subtype"""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = DatetimeType
        self.subtypes = ()
        self.atomic_type = datetime.datetime
        self.extension_type = None
        self.min = -62135596800000000000
        self.max = 253402300799999999000
        self.slug = "datetime[python]"


cdef class NumpyDatetime64Type(DatetimeType):
    """`numpy.datetime64` datetime subtype"""

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
        self.supertype = DatetimeType
        self.subtypes = ()
        self.atomic_type = np.datetime64
        self.extension_type = None
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000
        self.unit = unit

        if step_size < 1:
            raise ValueError(f"`step_size` must be >= 1, not {step_size}")
        self.step_size = step_size

        if self.unit is None:
            self.slug = "M8"
        else:
            if self.step_size == 1:
                self.slug = f"M8[{unit}]"
            else:
                self.slug = f"M8[{step_size}{unit}]"

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"categorical={self.categorical}, "
            f"sparse={self.sparse}, "
            f"unit={self.unit}, "
            f"step_size={self.step_size}"
            f")"
        )
