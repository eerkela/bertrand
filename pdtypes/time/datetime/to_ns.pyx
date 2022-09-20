"""Convert datetime objects to nanosecond offsets from the UTC epoch
('1970-01-01 00:00:00+0000').

Functions
---------
    pandas_timestamp_to_ns(
        arg: pd.Timestamp | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Convert `pandas.Timestamp` objects into ns offsets from UTC.

    pydatetime_to_ns(
        arg: datetime.datetime | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Convert `datetime.datetime` objects into ns offsets from UTC.

    numpy_datetime64_to_ns(
        arg: np.datetime64 | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Convert `numpy.datetime64` objects into ns offsets from UTC.

    datetime_to_ns(
        arg: datetime_like | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Convert arbitrary datetime objects into ns offsets from UTC.

Examples
--------
    >>> pandas_timestamp_to_ns(pd.Timestamp.now())
    >>> pandas_timestamp_to_ns(pd.Series([1, 2, 3], dtype="M8[ns]"))

    >>> pydatetime_to_ns(datetime.datetime.now())
    >>> pydatetime_to_ns(np.array([datetime.datetime.utcfromtimestamp(i) for i in range(3)]))

    >>> numpy_datetime64_to_ns(np.datetime64("2022-10-15 12:34:56"))
    >>> numpy_datetime64_to_ns(np.arange(0, 3, dtype="M8[s]"))

    >>> datetime_to_ns(pd.Timestamp.now())
    >>> datetime_to_ns(datetime.datetime.now())
    >>> datetime_to_ns(np.datetime64("2022-10-15 12:34:56"))
    >>> datetime_to_ns(pd.Series([1, 2, 3], dtype="M8[ns]"))
    >>> datetime_to_ns(np.array([datetime.datetime.utcfromtimestamp(i) for i in range(3)]))
    >>> datetime_to_ns(np.arange(0, 3, dtype="M8[s]"))
    >>> datetime_to_ns(np.array([pd.Timestamp.now(), datetime.datetime.now(), np.datetime64("2022-10-15")]))
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


# convertible datetime types
cdef set valid_datetime_types = {
    pd.Timestamp,
    datetime.datetime,
    np.datetime64
}


# UTC epoch (aware/naive) as datetime.datetime
cdef object utc_naive_pydatetime = datetime.datetime.utcfromtimestamp(0)
cdef object utc_aware_pydatetime
utc_aware_pydatetime = datetime.datetime.fromtimestamp(0, datetime.timezone.utc)


# nanosecond coefficients for datetime.timedelta
# Note: importing this from timedelta.to_ns causes a circular import error due
# to epoch.pyx
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
    """Convert a scalar `pd.Timestamp` object into a nanosecond offset from the
    UTC epoch object ('1970-01-01 00:00:00+0000').
    """
    # interpret naive as UTC
    if not timestamp.tzinfo:
        timestamp = timestamp.tz_localize("UTC")
    return timestamp.value


cdef inline object pydatetime_to_ns_scalar(object pydatetime):
    """Convert a scalar `datetime.datetime` object into a nanosecond offset
    from the UTC epoch object ('1970-01-01 00:00:00+0000').
    """
    # convert to timedelta.
    if not pydatetime.tzinfo:
        pydatetime = pydatetime - utc_naive_pydatetime
    else:
        pydatetime = pydatetime - utc_aware_pydatetime

    # convert timedelta to ns.  Note: importing this from timedelta.to_ns
    # causes a circular import error due to epoch.pyx
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
    """Convert a scalar `numpy.datetime64` object into a nanosecond offset from
    the UTC epoch object ('1970-01-01 00:00:00+0000').
    """
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
    """Convert an array of `pandas.Timestamp` objects into nanosecond offsets
    from the utc epoch ('1970-01-01 00:00:00+0000').
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[long int] result = np.empty(arr_length, dtype="i8")

    for i in range(arr_length):
        result[i] = pandas_timestamp_to_ns_scalar(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] pydatetime_to_ns_vector(np.ndarray[object] arr):
    """Convert an array of `datetime.datetime` objects into nanosecond offsets
    from the utc epoch ('1970-01-01 00:00:00+0000').
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = pydatetime_to_ns_scalar(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] numpy_datetime64_to_ns_vector(np.ndarray[object] arr):
    """Convert an array of `numpy.datetime64` objects into nanosecond offsets
    from the utc epoch ('1970-01-01 00:00:00+0000').
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = numpy_datetime64_to_ns_scalar(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] mixed_datetime_to_ns_vector(np.ndarray[object] arr):
    """Convert an array of mixed datetime objects into nanosecond offsets
    from the utc epoch ('1970-01-01 00:00:00+0000').
    """
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
    """Convert `pandas.Timestamp` objects into nanosecond offsets from the UTC
    epoch ('1970-01-01 00:00:00+0000').

    Parameters
    ----------
    arg : pd.Timestamp | array-like
        A `pandas.Timestamp` object or vector of such objects.

    Returns
    -------
    int | array-like
        The integer nanosecond offset from the UTC epoch
        ('1970-01-01 00:00:00+0000') that corresponds to `arg`.  This is always
        measured in UTC time.

    Examples
    --------
        >>> pandas_timestamp_to_ns(pd.Timestamp.now())
        >>> pandas_timestamp_to_ns(pd.Series([1, 2, 3], dtype="M8[ns]"))
    """
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
    """Convert `datetime.datetime` objects into nanosecond offsets from the UTC
    epoch ('1970-01-01 00:00:00+0000').

    Parameters
    ----------
    arg : datetime.datetime | array-like
        A `datetime.datetime` object or vector of such objects.

    Returns
    -------
    int | array-like
        The integer nanosecond offset from the UTC epoch
        ('1970-01-01 00:00:00+0000') that corresponds to `arg`.  This is always
        measured in UTC time.

    Examples
    --------
        >>> pydatetime_to_ns(datetime.datetime.now())
        >>> pydatetime_to_ns(np.array([datetime.datetime.utcfromtimestamp(i) for i in range(3)]))
    """
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
    """Convert `numpy.datetime64` objects into nanosecond offsets from the UTC
    epoch ('1970-01-01 00:00:00+0000').

    Parameters
    ----------
    arg : numpy.datetime64 | array-like
        A `numpy.datetime64` object or vector of such objects.

    Returns
    -------
    int | array-like
        The integer nanosecond offset from the UTC epoch
        ('1970-01-01 00:00:00+0000') that corresponds to `arg`.  This is always
        measured in UTC time.

    Examples
    --------
        >>> numpy_datetime64_to_ns(np.datetime64("2022-10-15 12:34:56"))
        >>> numpy_datetime64_to_ns(np.arange(0, 3, dtype="M8[s]"))
    """
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
    """Convert arbitrary datetime objects into nanosecond offsets from the UTC
    epoch ('1970-01-01 00:00:00+0000').

    Parameters
    ----------
    arg : datetime-like | array-like
        A datetime object or vector of such objects.  Can contain any
        combination of `pandas.Timestamp`, `datetime.datetime`, and/or
        `numpy.datetime64` objects.

    Returns
    -------
    int | array-like
        The integer nanosecond offset from the UTC epoch
        ('1970-01-01 00:00:00+0000') that corresponds to `arg`.  This is always
        measured in UTC time.

    Examples
    --------
        >>> datetime_to_ns(pd.Timestamp.now())
        >>> datetime_to_ns(datetime.datetime.now())
        >>> datetime_to_ns(np.datetime64("2022-10-15 12:34:56"))

        >>> datetime_to_ns(pd.Series([1, 2, 3], dtype="M8[ns]"))
        >>> datetime_to_ns(np.array([datetime.datetime.utcfromtimestamp(i) for i in range(3)]))
        >>> datetime_to_ns(np.arange(0, 3, dtype="M8[s]"))

        >>> datetime_to_ns(np.array([pd.Timestamp.now(), datetime.datetime.now(), np.datetime64("2022-10-15")]))
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
