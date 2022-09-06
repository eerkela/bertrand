cimport numpy as np

cdef unsigned int days_per_400_years
cdef unsigned short days_per_100_years
cdef unsigned short days_per_4_years
cdef unsigned short days_per_year
cdef np.ndarray days_per_month
cdef dict epoch_date

cdef dict _decompose_datelike_objects(
    np.ndarray[object] arr
)