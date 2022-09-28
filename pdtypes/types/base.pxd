cdef class ElementType:
    cdef readonly:
        bint is_categorical
        bint is_sparse
        bint is_extension
        object supertype
        tuple subtypes
        object atomic_type
        object extension_type
