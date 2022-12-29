from pdtypes.types.base cimport ElementType

# constants
cdef tuple non_nullable_types
cdef dict type_lookup

# parsing functions
cdef ElementType parse_typespec_type(
    type typespec,
    bint sparse = *,
    bint categorical = *,
    bint force_nullable = *
)
