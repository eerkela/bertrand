from .epoch cimport Epoch


# constants
cdef dict as_ns
cdef tuple valid_units


# functions
cpdef object convert_unit(
    object val,
    str from_unit,
    str to_unit,
    str rounding = *,
    Epoch since = *
)
cpdef object round_years_to_ns(object years, Epoch since)
cpdef object round_months_to_ns(object months, Epoch since)
cdef object _convert_unit_irregular_to_regular(
    object val,
    str from_unit,
    str to_unit,
    str rounding,
    Epoch since
)
cdef object _convert_unit_regular_to_irregular(
    object val,
    str from_unit,
    str to_unit,
    str rounding,
    Epoch since
)
