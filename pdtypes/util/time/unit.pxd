cimport pdtypes.util.time.epoch as epoch

# constants
cdef dict as_ns
cdef tuple valid_units

# scalar functions
cdef object round_years_to_ns(object years, epoch.Epoch since)
cdef object round_months_to_ns(object months, epoch.Epoch since)
