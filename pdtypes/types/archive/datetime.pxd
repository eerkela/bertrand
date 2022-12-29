from .base cimport ElementType


cdef class DatetimeType(ElementType):
    cdef readonly:
        object min
        object max


cdef class PandasTimestampType(DatetimeType):
    pass


cdef class PyDatetimeType(DatetimeType):
    pass


cdef class NumpyDatetime64Type(DatetimeType):
    cdef readonly:
        str unit
        unsigned long long step_size
