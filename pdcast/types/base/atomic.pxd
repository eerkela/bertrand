from .registry cimport CacheValue
from .scalar cimport ScalarType
from .composite cimport CompositeType


cdef class AtomicType(ScalarType):
    cdef:
        type _type_def
        object _dtype
        long long _itemsize
        bint _is_numeric
        object _max
        object _min
        bint _is_nullable
        object _na_value


cdef class ParentType(AtomicType):
    cdef:
        AtomicType _default
        CacheValue _backends
        CacheValue _subtypes

    cdef readonly:
        AtomicType __wrapped__
