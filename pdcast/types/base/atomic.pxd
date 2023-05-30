from .registry cimport CacheValue
from .scalar cimport ScalarType
from .composite cimport CompositeType


cdef class AtomicTypeConstructor(ScalarType):

    cdef void _init_identifier(self)
    cdef void _init_instances(self)
    cdef void _init_aliases(self)


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
        dict _backends


cdef class SuperType(HierarchicalType):
    cdef:
        CompositeType _subtypes
