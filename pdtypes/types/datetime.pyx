from cpython cimport datetime
from functools import lru_cache

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


cdef class BaseDatetimeType(ElementType):
    """Base class for datetime types."""
    pass


##########################
####    SUPERTYPES    ####
##########################


cdef class DatetimeType(BaseDatetimeType):
    """Float supertype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("datetimes have no valid extension type")
        self.supertype = None
        self.subtypes = (
            PandasTimestampType, PyDatetimeType, NumpyDatetime64Type
        )
        self.atomic_type = None
        self.extension_type = None
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000


########################
####    SUBTYPES    ####
########################


cdef class PandasTimestampType(BaseDatetimeType):
    """`pandas.Timestamp` datetime subtype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("datetimes have no valid extension type")
        self.supertype = DatetimeType
        self.subtypes = ()
        self.atomic_type = pd.Timestamp
        self.extension_type = None
        self.min = -2**63 + 1
        self.max = 2**63 - 1


cdef class PyDatetimeType(BaseDatetimeType):
    """`datetime.datetime` datetime subtype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("datetimes have no valid extension type")
        self.supertype = DatetimeType
        self.subtypes = ()
        self.atomic_type = datetime.datetime
        self.extension_type = None
        self.min = -62135596800000000000
        self.max = 253402300799999999000


cdef class NumpyDatetime64Type(BaseDatetimeType):
    """`numpy.datetime64` datetime subtype"""

    def __cinit__(
        self,
        str unit,
        unsigned long step_size
    ):
        if self.is_extension:
            raise ValueError("datetimes have no valid extension type")
        self.supertype = DatetimeType
        self.subtypes = ()
        self.atomic_type = np.datetime64
        self.extension_type = None
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000
        self.unit = unit
        self.step_size = step_size

    @lru_cache(maxsize=64)
    @classmethod
    def instance(cls, **kwargs) -> NumpyDatetime64Type:
        """Singleton constructor.  Can store up to 64 NumpyTime objects based
        on unit/step_size + args passed to
        """
        return cls(**kwargs)

    def __hash__(self) -> int:
        props = (
            self.__class__,
            self.is_categorical,
            self.is_sparse,
            self.is_extension,
            self.unit,
            self.step_size
        )
        return hash(props)
