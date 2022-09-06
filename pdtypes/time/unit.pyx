cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.round import round_div

from .date import date_to_days, days_to_date


#########################
####    Constants    ####
#########################


cdef dict as_ns = {  # TODO: add a bunch of synonyms for maximum flexibility
    # "as": 1e-9,
    # "fs": 1e-6,
    # "ps": 1e-3,
    "ns": 1,
    "us": 10**3,
    "ms": 10**6,
    "s": 10**9,
    "m": 60 * 10**9,
    "h": 60 * 60 * 10**9,
    "D": 24 * 60 * 60 * 10**9,
    "W": 7 * 24 * 60 * 60 * 10**9,
}


cdef tuple valid_units = tuple(as_ns) + ("M", "Y")


#######################
####    Private    ####
#######################


######################
####    Public    ####
######################


# TODO: does not apply correct rounding rule for days -> month/year
# TODO: overflow is common
# TODO: split into helpers, like timedelta.to_ns?


def convert_unit(
    arg: int | np.ndarray | pd.Series,
    before: str,
    after: str,
    start_year: int = 2001,
    start_month: int = 1,
    start_day: int = 1,
    rounding: str = "down"
) -> int | np.ndarray | pd.Series:
    """TODO"""
    # ensure units are valid
    if before not in valid_units:
        err_msg = f"`before` unit must be one of {valid_units}, not {before}"
        raise ValueError(err_msg)
    if after not in valid_units:
        err_msg = f"`after` unit must be one of {valid_units}, not {after}"
        raise ValueError(err_msg)

    # convert to python ints to prevent overflow


    # trivial case - no conversion necessary
    if before == after:
        return arg

    # regular -> regular ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W') 
    if before in as_ns and after in as_ns:
        return round_div(arg * as_ns[before], as_ns[after], rule=rounding)

    # irregular -> irregular ('M', 'Y')
    if before not in as_ns and after not in as_ns:
        # month -> year
        if before == "M" and after == "Y":
            return round_div(before, 12, rule=rounding)

        # year -> month
        return 12 * arg

    # conversion crosses regular/irregular boundary
    cdef object start
    cdef object end

    # regular -> irregular
    if before in as_ns:
         # convert to days
        arg = round_div(arg * as_ns[before], as_ns["D"], rule=rounding)

        # use days_to_date to get final date
        start = date_to_days(start_year, start_month, start_day)
        end = days_to_date(start + arg)

        # day -> year
        if after == "Y":
            return end["year"] - start_year

        # day -> month
        return 12 * (end["year"] - start_year) + (end["month"] - start_month)

    # irregular -> regular
    start = date_to_days(start_year, start_month, start_day)
    if before == "M":
        end = date_to_days(start_year, start_month + arg, start_day)
    else:  # before == "Y"
        end = date_to_days(start_year + arg, start_month, start_day)
    arg = end - start

    # day -> week
    if after == "W":
        return round_div(arg, 7, rule=rounding)

    # day -> day
    if after == "D":
        return arg

    # day -> ns, us, ms, s, m, h
    return round_div(arg * as_ns["D"], as_ns[after], rule=rounding)
