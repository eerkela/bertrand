from .registry cimport CacheValue
from .vector cimport VectorType
from .composite cimport CompositeType


cdef class ScalarType(VectorType):
    cdef:
        type _type_def
        object _dtype
        object _itemsize
        object _is_numeric
        object _max
        object _min
        object _is_nullable
        object _na_value


cdef class AbstractType(ScalarType):
    cdef:
        CacheValue _backends
        CacheValue _subtypes
