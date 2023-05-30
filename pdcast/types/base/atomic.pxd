from .registry cimport CacheValue
from .scalar cimport ScalarType
from .composite cimport CompositeType


cdef class AtomicTypeConstructor(ScalarType):

    cdef void _init_encoder(self)
    cdef void _init_instances(self)


cdef class AtomicType(AtomicTypeConstructor):
    cdef:
        object _dtype


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
        CompositeType _subtypes
