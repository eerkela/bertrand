from pdtypes.util.structs cimport LRUDict

# constants
cdef unsigned short cache_size
cdef dict shared_registry
cdef LRUDict datetime64_registry
cdef LRUDict timedelta64_registry
cdef str default_string_storage


# factory function
cpdef object element_type(
    object base,
    bint is_categorical = *,
    bint is_sparse = *,
    bint is_nullable = *,
    str unit = *,
    unsigned long step_size = *,
    str storage = *
)
