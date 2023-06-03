from .registry cimport CacheValue
from .vector cimport VectorType
from .composite cimport CompositeType


cdef class ScalarType(VectorType):
    cdef:
        type _type_def
        object _dtype
        long long _itemsize
        bint _is_numeric
        object _max
        object _min
        bint _is_nullable
        object _na_value


cdef class AbstractType(ScalarType):
    cdef:
        CacheValue _backends
        CacheValue _subtypes
