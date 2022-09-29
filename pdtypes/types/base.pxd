cdef class ElementType:
    cdef readonly:
        bint is_categorical
        bint is_sparse
        bint is_nullable
        object supertype
        tuple subtypes
        object atomic_type
        object extension_type
        long hash
        str slug
