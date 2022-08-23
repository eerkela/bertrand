cdef dict atomic_to_supertype
cdef dict default_supertypes
cdef dict custom_supertypes
cdef dict custom_to_default_supertype
cdef list supertype_only

cdef object _supertype(object dtype)
cdef set _subtypes(object dtype, bint exact = *)
