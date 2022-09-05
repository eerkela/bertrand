import datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import check_dtype, get_dtype
from pdtypes.util.type_hints import date_like, timedelta_like

from ..date import date_to_days, decompose_date
from ..timezone import is_utc, timezone, localize
from ..unit cimport as_ns


#########################
####    Constants    ####
#########################


cdef set _valid_timedelta_types = {
    pd.Timedelta,
    datetime.timedelta,
    np.timedelta64
}


cdef long int[:] _pytimedelta_ns_coefs = np.array(
    [
        as_ns["D"],
        as_ns["s"],
        as_ns["us"]
    ]
)


#######################
####    Private    ####
#######################


cdef inline long int _pandas_timedelta_to_ns(object timedelta):
    """TODO"""
    return int(timedelta.asm8)


cdef inline object _pytimedelta_to_ns(object pytimedelta):
    """TODO"""
    cdef np.ndarray[object] components = np.array(
        [
            pytimedelta.days,
            pytimedelta.seconds,
            pytimedelta.microseconds
        ],
        dtype="O"
    )
    return np.dot(components, _pytimedelta_ns_coefs)


cdef inline object _numpy_timedelta64_to_ns(
    object timedelta64,
    object start_year,
    object start_month,
    object start_day
):
    """TODO"""
    cdef str unit
    cdef int step_size
    cdef object int_repr

    # convert to integer representation in base units
    unit, step_size = np.datetime_data(timedelta64)
    int_repr = step_size * int(timedelta64)

    # convert to ns
    if unit == "ns":  # no conversion needed
        return int_repr
    if unit in as_ns:  # unit is regular
        return int_repr * as_ns[unit]

    # unit is irregular ('M', 'Y')
    cdef object start
    cdef object end

    if unit == "Y":  # convert years to days, accounting for leap years
        start = date_to_days(start_year, start_month, start_day)
        end = date_to_days(start_year + int_repr, start_month, start_day)
    elif unit == "M":  # convert months to days, accounting for unequal lengths
        start = date_to_days(start_year, start_month, start_day)
        end = date_to_days(start_year, start_month + int_repr, start_day)
    else:
        raise ValueError(f"could not interpret timedelta64 with unit: {unit}")

    return (end - start) * as_ns["D"]


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[long int] _pandas_timedelta_objects_to_ns(
    np.ndarray[object] arr
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[long int] result = np.empty(arr_length, dtype="i8")

    for i in range(arr_length):
        result[i] = pandas_timedelta_to_ns(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] _pytimedelta_objects_to_ns(
    np.ndarray[object] arr
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = pytimedelta_to_ns(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] _numpy_timedelta64_objects_to_ns(
    np.ndarray[object] arr,
    object start_year,
    object start_month,
    object start_day
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = numpy_timedelta64_to_ns(
            arr[i],
            start_year=start_year,
            start_month=start_month,
            start_day=start_day
        )

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] _mixed_timedelta_objects_to_ns(
    np.ndarray[object] arr,
    object start_year,
    object start_month,
    object start_day
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object element
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        element = arr[i]
        if isinstance(element, pd.Timedelta):
            result[i] = pandas_timedelta_to_ns(element)
        elif isinstance(element, datetime.timedelta):
            result[i] = pytimedelta_to_ns(element)
        else:
            result[i] = numpy_timedelta64_to_ns(
                element,
                start_year=start_year,
                start_month=start_month,
                start_day=start_day
            )

    return result


######################
####    Public    ####
######################


def pandas_timedelta_to_ns(
    arg: pd.Timedelta | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """TODO"""
    # np.array
    if isinstance(arg, np.ndarray):
        return _pandas_timedelta_objects_to_ns(arg)

    # pd.Series
    if isinstance(arg, pd.Series):
        if pd.api.types.is_timedelta64_ns_dtype(arg):
            raise NotImplementedError()

        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = _pandas_timedelta_objects_to_ns(arg)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return _pandas_timedelta_to_ns(arg)


def pytimedelta_to_ns(
    arg: datetime.timedelta | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """TODO"""
    # np.array
    if isinstance(arg, np.ndarray):
        return _pytimedelta_objects_to_ns(arg)

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = _pytimedelta_objects_to_ns(arg)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return _pytimedelta_to_ns(arg)


def numpy_timedelta64_to_ns(
    arg: np.timedelta64 | np.ndarray | pd.Series,
    start_year: int,
    start_month: int,
    start_day: int
) -> int | np.ndarray | pd.Series:
    cdef dict start = {
        "start_year": start_year,
        "start_month": start_month,
        "start_day": start_day
    }

    # np.array
    if isinstance(arg, np.ndarray):
        if np.issubdtype(arg, "m8"):
            raise NotImplementedError()
        return _numpy_timedelta64_objects_to_ns(
            arg,
            start_year=start_year,
            start_month=start_month,
            start_day=start_day
        )

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = _numpy_timedelta64_objects_to_ns(
            arg,
            start_year=start_year,
            start_month=start_month,
            start_day=start_day
        )
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return _numpy_timedelta64_to_ns(
        arg,
        start_year=start_year,
        start_month=start_month,
        start_day=start_day
    )


def timedelta_to_ns(
    arg: timedelta_like | np.ndarray | pd.Series,
    epoch: str | tuple | date_like = (2000, 1, 1)
) -> int | np.ndarray | pd.Series:
    """TODO"""
    # get exact element type(s) and ensure timedelta-like
    dtype = get_dtype(arg)
    if isinstance(dtype, set) and dtype - _valid_timedelta_types:
        raise TypeError(f"`arg` must contain only timedelta-like "
                        f"elements, not {dtype}")

    # split epoch into year, month, day for timedelta64 units 'M' and 'Y'
    # TODO: force scalar
    # split into separate helper function, _split_epoch_date
    if isinstance(epoch, str):
        # lookup table in date.pyx
        raise NotImplementedError()
    elif (isinstance(epoch, tuple) and
          len(epoch) == 3 and
          check_dtype(epoch, int)):
        start_year, start_month, start_day = epoch
    elif check_dtype(epoch, ("datetime", datetime.date)):
        components = decompose_date(epoch)
        start_year, start_month, start_day = components.values()
    else:
        raise NotImplementedError()

    # pd.Timedelta
    if dtype == pd.Timedelta:
        return pandas_timedelta_to_ns(arg)

    # datetime.timedelta
    if dtype == datetime.timedelta:
        return pytimedelta_to_ns(arg)

    # np.timedelta64
    if dtype == np.timedelta64:
        return numpy_timedelta64_to_ns(
            arg,
            start_year=start_year,
            start_month=start_month,
            start_day=start_day
        )

    # pd.Series (mixed element types)
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = _mixed_timedelta_objects_to_ns(
            arg,
            start_year=start_year,
            start_month=start_month,
            start_day=start_day
        )
        return pd.Series(arg, index=index, copy=False)

    # np.ndarray (mixed element types)
    return _mixed_timedelta_objects_to_ns(
        arg,
        start_year=start_year,
        start_month=start_month,
        start_day=start_day
    )
