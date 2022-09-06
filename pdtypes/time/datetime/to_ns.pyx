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

from pdtypes.check import check_dtype, get_dtype
from pdtypes.util.type_hints import datetime_like

from ..date import decompose_date, date_to_days
from ..timedelta.to_ns cimport pytimedelta_to_ns_scalar
from ..timezone import is_utc, timezone, localize
from ..unit cimport as_ns


# datetime_to_ns
# ns_to_pandas_timestamp (pd.to_datetime)
# ns_to_pydatetime
# ns_to_numpy_datetime64
# string_to_pandas_timestamp (pd.to_datetime)
# string_to_pydatetime
# string_to_numpy_datetime64
# datetime_to_datetime


# pdtypes/
#     time/
#         datetime/
#             __init__.py
#             from_ns.pyx
#             from_string.pyx
#             to_datetime.pyx
#             to_ns.pyx
#           timedelta/
#             __init__.py
#             from_ns.pyx
#             from_string.pyx
#             to_timedelta.pyx
#             to_ns.pyx
#         __init__.py
#         date.pyx
#         timezone.pyx
#         unit.pyx


# TODO: include datetime.date?


#########################
####    Constants    ####
#########################


cdef set valid_datetime_types = {
    pd.Timestamp,
    datetime.datetime,
    np.datetime64
}


cdef object utc_pydatetime
utc_pydatetime = datetime.datetime.fromtimestamp(0, datetime.timezone.utc)


#######################
####    Private    ####
#######################


cdef inline long int pandas_timestamp_to_ns_scalar(object timestamp):
    """TODO"""
    # interpret naive as UTC
    if not timestamp.tzinfo:
        timestamp = timestamp.tz_localize("UTC")
    return int(timestamp.asm8)


cdef inline object pydatetime_to_ns_scalar(object pydatetime):
    """TODO"""
    # interpret naive as UTC
    if not pydatetime.tzinfo:
        pydatetime = pydatetime.replace(tzinfo=datetime.timezone.utc)

    # convert to timedelta
    pydatetime -= utc_pydatetime
    return pytimedelta_to_ns_scalar(pydatetime)


cdef inline object numpy_datetime64_to_ns_scalar(object datetime64):
    """TODO"""
    cdef str unit
    cdef int step_size

    # get integer representation, unit info from datetime64
    unit, step_size = np.datetime_data(datetime64)
    datetime64 = int(datetime64) * step_size

    # convert scaled integer repr to nanoseconds
    # TODO: use convert_unit here, after it is finalized
    raise NotImplementedError()


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

            # convert scaled integer repr to nanoseconds
            # TODO: use convert_unit here, after it is finalized
            raise NotImplementedError()
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
