"""Implements a single function `datetime_to_ns()`, which takes arbitrary
datetime objects and converts them into (integer) nanosecond offsets from the
UTC epoch ("1970-01-01 00:00:00+0000").

A specialized variant of this function is also exported for every included
datetime type, as follows:
    - `pandas_timestamp_to_ns()` - for converting `pd.Timestamp` objects.
    - `pydatetime_to_ns()` - for converting `datetime.datetime` objects.
    - `numpy_datetime64_to_ns()` - for converting `np.timedelta64` objects.

These specialized variants aren't part of the standard `pdtypes` interface, but
they can be imported for a modest performance increase where the datetime type
is known ahead of time.
"""
import datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import get_dtype
from pdtypes.util.type_hints import datetime_like

from ..calendar import date_to_days
from ..timezone import timezone, localize
from ..unit cimport as_ns


#########################
####    Constants    ####
#########################


cdef set valid_datetime_types = {
    pd.Timestamp,
    datetime.datetime,
    np.datetime64
}


cdef object utc_naive_pydatetime = datetime.datetime.utcfromtimestamp(0)
cdef object utc_aware_pydatetime
utc_aware_pydatetime = datetime.datetime.fromtimestamp(0, datetime.timezone.utc)


# importing this from timedelta.to_ns causes a circular import error due to
# epoch.pyx
cdef long int[:] pytimedelta_ns_coefs = np.array(
    [
        as_ns["D"],
        as_ns["s"],
        as_ns["us"]
    ]
)


#######################
####    Private    ####
#######################


cdef inline long int pandas_timestamp_to_ns_scalar(object timestamp):
    """TODO"""
    # interpret naive as UTC
    if not timestamp.tzinfo:
        timestamp = timestamp.tz_localize("UTC")
    return timestamp.value


cdef inline object pydatetime_to_ns_scalar(object pydatetime):
    """TODO"""
    # convert to timedelta
    if not pydatetime.tzinfo:
        pydatetime = pydatetime - utc_naive_pydatetime
    else:
        pydatetime = pydatetime - utc_aware_pydatetime

    # convert timedelta to ns
    # importing this from timedelta.to_ns causes a circular import error
    cdef np.ndarray[object] components = np.array(
        [
            pydatetime.days,
            pydatetime.seconds,
            pydatetime.microseconds
        ],
        dtype="O"
    )
    return np.dot(components, pytimedelta_ns_coefs)


cdef inline object numpy_datetime64_to_ns_scalar(object datetime64):
    """TODO"""
    cdef str unit
    cdef int step_size

    # get integer representation, unit info from datetime64
    unit, step_size = np.datetime_data(datetime64)
    datetime64 = int(np.int64(datetime64)) * step_size

    # scale to nanoseconds
    if unit == "ns":
        return datetime64
    if unit in as_ns:
        return datetime64 * as_ns[unit]
    if unit == "M":
        return date_to_days(1970, 1 + datetime64, 1) * as_ns["D"]
    return date_to_days(1970 + datetime64, 1, 1) * as_ns["D"]


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[long int] pandas_timestamp_to_ns_vector(np.ndarray[object] arr):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[long int] result = np.empty(arr_length, dtype="i8")

    for i in range(arr_length):
        result[i] = pandas_timestamp_to_ns_scalar(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] pydatetime_to_ns_vector(np.ndarray[object] arr):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = pydatetime_to_ns_scalar(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] numpy_datetime64_to_ns_vector(np.ndarray[object] arr):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = numpy_datetime64_to_ns_scalar(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] mixed_datetime_to_ns_vector(np.ndarray[object] arr):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object element
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        element = arr[i]
        if isinstance(element, pd.Timestamp):
            result[i] = pandas_timestamp_to_ns_scalar(element)
        elif isinstance(element, datetime.datetime):
            result[i] = pydatetime_to_ns_scalar(element)
        else:
            result[i] = numpy_datetime64_to_ns_scalar(element)

    return result


######################
####    Public    ####
######################


def pandas_timestamp_to_ns(
    arg: pd.Timestamp | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """TODO"""
    # np.ndarray
    if isinstance(arg, np.ndarray):
        return pandas_timestamp_to_ns_vector(arg)

    # pd.Series
    if isinstance(arg, pd.Series):
        if pd.api.types.is_datetime64_ns_dtype(arg):  # use `.dt` namespace
            if not arg.dt.tz:  # series is naive, assume UTC
                arg = arg.dt.tz_localize("UTC")
            return arg.astype(np.int64)

        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = pandas_timestamp_to_ns_vector(arg)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return pandas_timestamp_to_ns_scalar(arg)


def pydatetime_to_ns(
    arg: datetime.datetime | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """TODO"""
    # np.ndarray
    if isinstance(arg, np.ndarray):
        return pydatetime_to_ns_vector(arg)

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = pydatetime_to_ns_vector(arg)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return pydatetime_to_ns_scalar(arg)


def numpy_datetime64_to_ns(
    arg: np.datetime64 | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """TODO"""
    # np.ndarray
    if isinstance(arg, np.ndarray):
        if np.issubdtype(arg.dtype, "M8"):
            # get integer representation, unit info from datetime64
            unit, step_size = np.datetime_data(arg.dtype)
            arg = arg.astype(np.int64).astype("O") * step_size

            # scale to nanoseconds
            if unit == "ns":
                return arg
            if unit in as_ns:
                return arg * as_ns[unit]
            if unit == "M":
                return date_to_days(1970, 1 + arg, 1) * as_ns["D"]
            return date_to_days(1970 + arg, 1, 1) * as_ns["D"]

        return numpy_datetime64_to_ns_vector(arg)

    # pd.Series:
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = numpy_datetime64_to_ns_vector(arg)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return numpy_datetime64_to_ns_scalar(arg)


def datetime_to_ns(
    arg: datetime_like | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """Convert generic datetime objects to nanoseconds offsets from UTC.
    """
    # get exact element type(s) and ensure datetime-like
    dtype = get_dtype(arg)
    if isinstance(dtype, set) and dtype - valid_datetime_types:
        raise TypeError(f"`datetime_like` must contain only datetime-like "
                        f"elements, not {dtype}")

    # pd.Timestamp
    if dtype == pd.Timestamp:
        return pandas_timestamp_to_ns(arg)

    # datetime.datetime
    if dtype == datetime.datetime:
        return pydatetime_to_ns(arg)

    # np.datetime64
    if dtype == np.datetime64:
        return numpy_datetime64_to_ns(arg)

    # mixed element types
    if isinstance(dtype, set):
        # pd.Series
        if isinstance(arg, pd.Series):
            index = arg.index
            arg = arg.to_numpy(dtype="O")
            arg = mixed_datetime_to_ns_vector(arg)
            return pd.Series(arg, index=index, copy=False)

        # np.ndarray
        return mixed_datetime_to_ns_vector(arg)

    # unrecognized element type
    raise TypeError(f"could not parse datetime of type {dtype}")
