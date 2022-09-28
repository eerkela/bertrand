from .base cimport ElementType

cdef class BaseDatetimeType(ElementType):
    pass

cdef class DatetimeType(BaseDatetimeType):
    cdef readonly:
        object min
        object max

cdef class PandasTimestampType(BaseDatetimeType):
    cdef readonly:
        long min
        long max

cdef class PyDatetimeType(BaseDatetimeType):
    cdef readonly:
        object min
        object max

cdef class NumpyDatetime64Type(BaseDatetimeType):
    cdef readonly:
        object min
        object max
        str unit
        unsigned long step_size
