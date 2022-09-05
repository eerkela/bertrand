import datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import check_dtype, get_dtype
from pdtypes.util.type_hints import datetime_like

from ..date import decompose_date, date_to_days
from ..timezone import is_utc, timezone, localize
from ..unit import as_ns


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


cdef dict epoch_bias = {
    "julian": 2440588 * as_ns["D"],
    "gregorian": 141428 * as_ns["D"],
    "reduced_julian": 40588 * as_ns["D"],
    "lotus": 25569 * as_ns["D"],
    "sas": 3653 * as_ns["D"],
    "utc": 0,
    "gps": -3657 * as_ns["D"],
    "cocoa": -11323 * as_ns["D"]
}


#######################
####    Private    ####
#######################


cdef object pandas_timestamp_to_ns(object timestamp):
    """TODO"""
    # interpret naive as UTC
    if not timestamp.tzinfo:
        timestamp = timestamp.tz_localize("UTC")
    return int(timestamp.asm8)


cdef object pydatetime_to_ns(object pydatetime):
    """TODO"""
    # interpret naive as UTC
    if not pydatetime.tzinfo:
        pydatetime = pydatetime.replace(tzinfo=datetime.timezone.utc)

    # convert to timedelta
    pydatetime -= utc_pydatetime
    raise NotImplementedError()


cdef object numpy_datetime64_to_ns(object datetime64):
    """TODO"""
    cdef str unit
    cdef int step_size

    # get integer representation, unit info from datetime64
    unit, step_size = np.datetime_date(datetime64)
    datetime64 = int(datetime64) * step_size

    # convert scaled integer repr to nanoseconds
    raise NotImplementedError()


cdef np.ndarray[object] pandas_timestamp_objects_to_ns(np.ndarray[object] arr):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = pandas_timestamp_to_ns(arr[i])

    return result


cdef np.ndarray[object] pydatetime_objects_to_ns(np.ndarray[object] arr):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = pydatetime_to_ns(arr[i])

    return result


cdef np.ndarray[object] numpy_datetime64_objects_to_ns(np.ndarray[object] arr):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = numpy_datetime64_to_ns(arr[i])

    return result


cdef np.ndarray[object] mixed_datetime_objects_to_ns(np.ndarray[object] arr):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object element
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        element = arr[i]
        if isinstance(element, pd.Timestamp):
            result[i] = pandas_timestamp_to_ns(element)
        elif isinstance(element, datetime.datetime):
            result[i] = pydatetime_to_ns(element)
        else:
            result[i] = numpy_datetime64_to_ns(element)

    return result


######################
####    Public    ####
######################


def datetime_to_ns(
    arg: datetime_like | np.ndarray | pd.Series,
    epoch: str = "utc"
) -> int | np.ndarray | pd.Series:
    """convert generic datetime scalars, arrays, and sequences into nanosecond
    offsets from the given epoch.
    """
    # get exact element type(s) and ensure datetime-like
    dtype = get_dtype(arg)
    if isinstance(dtype, set) and dtype - valid_datetime_types:
        raise TypeError(f"`datetime_like` must contain only datetime-like "
                        f"elements, not {dtype}")

    # get nanosecond bias for given epoch (from utc)
    # TODO: force scalar
    if isinstance(epoch, str):
        ns_bias = epoch_bias[epoch]
    elif (isinstance(epoch, tuple) and
          len(epoch) == 3 and
          check_dtype(epoch, int)):
        ns_bias = date_to_days(*epoch) * as_ns["D"]
    elif check_dtype(epoch, ("datetime", datetime.date)):
        ns_bias = date_to_days(**decompose_date(epoch)) * as_ns["D"]
    else:
        raise NotImplementedError()

    # remember to -= ns_bias

    # pd.Timestamp
    if dtype == pd.Timestamp:
        # np.array
        if isinstance(arg, np.ndarray):
            return pandas_timestamp_objects_to_ns(arg) - ns_bias

        # pd.Series
        if isinstance(arg, pd.Series):
            if pd.api.types.is_datetime64_ns_dtype(arg):
                raise NotImplementedError()

            index = arg.index
            arg = arg.to_numpy(dtype="O")
            arg = pandas_timestamp_objects_to_ns(arg) - ns_bias
            return pd.Series(arg, index=index, copy=False)

        # scalar
        return pandas_timestamp_to_ns(arg) - ns_bias

    # datetime.datetime
    if dtype == datetime.datetime:
        # np.array
        if isinstance(arg, np.ndarray):
            return pydatetime_objects_to_ns(arg) - ns_bias

        # pd.Series
        if isinstance(arg, pd.Series):
            index = arg.index
            arg = arg.to_numpy(dtype="O")
            arg = pydatetime_objects_to_ns(arg) - ns_bias
            return pd.Series(arg, index=index, copy=False)

        # scalar
        return pydatetime_to_ns(arg) - ns_bias

    # np.datetime64
    if dtype == np.datetime64:
        # np.array
        if isinstance(arg, np.ndarray):
            if np.issubdtype(arg.dtype, "M8"):
                raise NotImplementedError()
            return numpy_datetime64_objects_to_ns(arg) - ns_bias

        # pd.Series:
        if isinstance(arg, pd.Series):
            index = arg.index
            arg = arg.to_numpy(dtype="O")
            arg = numpy_datetime64_objects_to_ns(arg) - ns_bias
            return pd.Series(arg, index=index, copy=False)

        # scalar
        return numpy_datetime64_to_ns(arg) - ns_bias

    # pd.Series (mixed element types)
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = mixed_datetime_objects_to_ns(arg) - ns_bias
        return pd.Series(arg, index=index, copy=False)

    # np.ndarray (mixed element types)
    return mixed_datetime_objects_to_ns(arg)
