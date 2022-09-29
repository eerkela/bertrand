from .base cimport ElementType


cdef class TimedeltaType(ElementType):
    cdef readonly:
        object min
        object max


cdef class PandasTimedeltaType(TimedeltaType):
    pass


cdef class PyTimedeltaType(TimedeltaType):
    pass


cdef class NumpyTimedelta64Type(TimedeltaType):
    cdef readonly:
        str unit
        unsigned long step_size
