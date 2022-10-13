from pdtypes.types.base cimport ElementType

# constants
cdef dict dtype_lookup

# parsing functions
cdef object parse_typespec_dtype(
    object typespec,
    bint sparse = *,
    bint categorical = *,
    bint force_nullable = *
)

