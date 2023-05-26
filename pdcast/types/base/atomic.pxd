from .registry cimport CacheValue
from .scalar cimport ScalarType
from .composite cimport CompositeType


cdef class AtomicTypeConstructor(ScalarType):

    cdef void _init_identifier(self, type subclass)
    cdef void _init_instances(self, type subclass)


cdef class AtomicType(AtomicTypeConstructor):
    cdef readonly:
        object _dtype
        CacheValue _subtype_cache
        CacheValue _supertype_cache

    cdef public:
        AtomicType _parent  # should be ParentType
        GenericType _generic


cdef class HierarchicalType(AtomicType):
    cdef:
        AtomicType _default

    cdef readonly:
        AtomicType __wrapped__


cdef class GenericType(HierarchicalType):
    cdef:
        CompositeType _subtypes
        dict _backends
