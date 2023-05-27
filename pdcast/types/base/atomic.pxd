from .registry cimport CacheValue
from .scalar cimport ScalarType
from .composite cimport CompositeType


cdef class AtomicTypeConstructor(ScalarType):

    cdef void _init_identifier(self, type subclass)
    cdef void _init_instances(self, type subclass)


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


cdef class ParentType(HierarchicalType):
    cdef:
        CompositeType _subtypes
