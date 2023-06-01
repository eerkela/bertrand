from .registry cimport CacheValue
from .scalar cimport ScalarType
from .composite cimport CompositeType


cdef class AtomicTypeConstructor(ScalarType):

    cdef void _init_encoder(self)
    cdef void _init_instances(self)


cdef class AtomicType(AtomicTypeConstructor):
    cdef:
        type _type_def
        object _dtype
        long long _itemsize
        bint _is_numeric
        object _max
        object _min
        bint _is_nullable
        object _na_value


cdef class HierarchicalType(AtomicType):
    cdef:
        AtomicType _default

    cdef readonly:
        AtomicType __wrapped__


cdef class GenericType(HierarchicalType):
    cdef:
        CacheValue _backends


cdef class SuperType(HierarchicalType):
    cdef:
        CacheValue _subtypes
