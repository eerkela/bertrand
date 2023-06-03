from .vector cimport VectorType


cdef class DecoratorType(VectorType):
    cdef:
        VectorType _wrapped
        DecoratorSorter _insort


cdef class DecoratorSorter:
    cdef:
        type base_class

    cdef dict copy_parameters(self, DecoratorType wrapper, dict kwargs)


cdef class PrioritySorter(DecoratorSorter):
    cdef:
        int priority
