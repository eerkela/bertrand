cimport numpy as np

# constants
cdef dict as_ns
cdef tuple valid_units

# scalar functions
cdef object round_years_to_ns(
    object years,
    object start_year,
    object start_month,
    object start_day
)
cdef object round_months_to_ns(
    object months,
    object start_year,
    object start_month,
    object start_day
)

# vectorized functions
cdef np.ndarray[object] cast_to_int_vector(
    np.ndarray arr,
    str rounding
)
cdef object cast_to_int(
    object val,
    str rounding
)
