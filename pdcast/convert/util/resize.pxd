from pdcast cimport types


# functions
cpdef tuple boundscheck(
    object series,
    types.AtomicType dtype,
    str errors = *
)
