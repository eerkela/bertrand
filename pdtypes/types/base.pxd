cimport numpy as np

from pdtypes.util.structs cimport LRUDict


# constants
cdef unsigned short cache_size
cdef dict shared_registry
cdef LRUDict datetime64_registry
cdef LRUDict timedelta64_registry
cdef dict defaults


# factory functions
cpdef object get_dtype(object example)
cpdef ElementType resolve_dtype(object typespec)


# helpers
cdef long long compute_hash(dict kwargs)
cdef ElementType element_type_from_kwargs(dict kwargs)
cdef ElementType parse_example_scalar(object typespec, bint force_nullable = *)
cdef object parse_example_vector(
    np.ndarray[object] arr,
    bint as_index = *,
    bint force_nullable = *
)


# classes
cdef class ElementType:
    cdef readonly:
        bint sparse
        bint categorical
        bint nullable
        type supertype
        tuple subtypes
        type atomic_type
        object extension_type
        long long hash
        str slug
