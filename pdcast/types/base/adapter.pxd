from .scalar cimport ScalarType


cdef class AdapterType(ScalarType):
    cdef:
        ScalarType _wrapped


cdef class DecoratorSorter:
    cdef:
        type base_class

    cdef dict copy_parameters(self, AdapterType wrapper, dict kwargs)


cdef class PrioritySorter(DecoratorSorter):
    cdef:
        int priority
