from .base cimport ElementType

cdef class BaseTimedeltaType(ElementType):
    pass

cdef class TimedeltaType(BaseTimedeltaType):
    cdef readonly:
        object min
        object max

cdef class PandasTimedeltaType(BaseTimedeltaType):
    cdef readonly:
        long min
        long max

cdef class PyTimedeltaType(BaseTimedeltaType):
    cdef readonly:
        object min
        object max

cdef class NumpyTimedelta64Type(BaseTimedeltaType):
    cdef readonly:
        object min
        object max
        str unit
        unsigned long step_size
