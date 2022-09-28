from cpython cimport datetime
from functools import lru_cache

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


cdef class BaseTimedeltaType(ElementType):
    """Base class for timedelta types."""
    pass


##########################
####    SUPERTYPES    ####
##########################


cdef class TimedeltaType(BaseTimedeltaType):
    """Timedelta supertype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("timedeltas have no valid extension type")
        self.supertype = None
        self.subtypes = (
            PandasTimedeltaType, PyTimedeltaType, NumpyTimedelta64Type
        )
        self.atomic_type = None
        self.extension_type = None
        self.min = -291061508645168391112156800000000000
        self.max = 291061508645168391112243200000000000


########################
####    SUBTYPES    ####
########################


cdef class PandasTimedeltaType(BaseTimedeltaType):
    """`pandas.Timedelta` timedelta subtype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("timedeltas have no valid extension type")
        self.supertype = TimedeltaType
        self.subtypes = ()
        self.atomic_type = pd.Timedelta
        self.extension_type = None
        self.min = -2**63 + 1
        self.max = 2**63 - 1


cdef class PyTimedeltaType(BaseTimedeltaType):
    """`datetime.timedelta` timedelta subtype"""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("timedeltas have no valid extension type")
        self.supertype = TimedeltaType
        self.subtypes = ()
        self.atomic_type = datetime.timedelta
        self.extension_type = None
        self.min = -86399999913600000000000
        self.max = 86399999999999999999000


cdef class NumpyTimedelta64Type(BaseTimedeltaType):
    """`numpy.timedelta64` timedelta subtype"""

    def __cinit__(
        self,
        str unit,
        unsigned long step_size
    ):
        if self.is_extension:
            raise ValueError("timedeltas have no valid extension type")
        self.supertype = TimedeltaType
        self.subtypes = ()
        self.atomic_type = np.timedelta64
        self.extension_type = None
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000
        self.unit = unit
        self.step_size = step_size

    @lru_cache(maxsize=64)
    @classmethod
    def instance(cls, **kwargs) -> NumpyTimedelta64Type:
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
