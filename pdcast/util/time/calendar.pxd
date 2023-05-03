cimport numpy as np

# constants
cdef unsigned int days_per_400_years
cdef unsigned short days_per_100_years
cdef unsigned short days_per_4_years
cdef unsigned short days_per_year
cdef np.ndarray days_per_month


# functions
cpdef object date_to_days(object year, object month, object day)
cpdef object days_in_month(object month, object year = *)
cpdef dict days_to_date(object days)
cpdef object is_leap_year(object year)
cpdef object leaps_between(object lower, object upper)
