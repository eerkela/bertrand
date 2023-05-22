from . cimport scalar


cdef class AdapterType(scalar.ScalarType):
    cdef:
        scalar.ScalarType _wrapped


cdef class DecoratorSorter:
    cdef:
        type base_class

    cdef dict copy_parameters(self, AdapterType wrapper, dict kwargs)


cdef class PrioritySorter(DecoratorSorter):
    cdef:
        int priority
