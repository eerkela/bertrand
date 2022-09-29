from pdtypes.util.structs cimport LRUDict
from .base cimport ElementType

# constants
cdef unsigned short cache_size
cdef dict shared_registry
cdef LRUDict datetime64_registry
cdef LRUDict timedelta64_registry
cdef str default_string_storage


# factory function
cpdef ElementType factory(
    type base,
    bint is_categorical = *,
    bint is_sparse = *,
    bint is_nullable = *,
    str unit = *,
    unsigned long long step_size = *,
    str storage = *
)
