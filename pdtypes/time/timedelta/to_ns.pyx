import datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import get_dtype
from pdtypes.util.type_hints import datetime_like, timedelta_like

from ..calendar import date_to_days
from ..epoch import epoch_date
from ..unit cimport as_ns


#########################
####    Constants    ####
#########################


cdef set valid_timedelta_types = {
    pd.Timedelta,
    datetime.timedelta,
    np.timedelta64
}


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


cdef inline long int pandas_timedelta_to_ns_scalar(object timedelta):
    """Internal C-style interface for pandas_timedelta_to_ns when applied to
    `pd.Timedelta` scalars.
    """
    return timedelta.value


cdef inline object pytimedelta_to_ns_scalar(object pytimedelta):
    """Internal C-style interface for pytimedelta_to_ns when applied to
    `datetime.timedelta` scalars.
    """
    cdef np.ndarray[object] components = np.array(
        [
            pytimedelta.days,
            pytimedelta.seconds,
            pytimedelta.microseconds
        ],
        dtype="O"
    )
    return np.dot(components, pytimedelta_ns_coefs)


cdef inline object numpy_timedelta64_to_ns_scalar(
    object timedelta64,
    object start_year,
    object start_month,
    object start_day
):
    """Internal C-style interface for numpy_timedelta64_to_ns when applied to
    `np.timedelta64` scalars.  Also works for 'm8'-dtyped numpy arrays.
    """
    cdef str unit
    cdef int step_size
    cdef object int_repr

    # convert to integer representation in base units (works for M8 arrays)
    unit, step_size = np.datetime_data(timedelta64.dtype)
    int_repr = step_size * timedelta64.view(np.int64).astype("O")

    # convert to ns
    if unit == "ns":  # no conversion needed
        return int_repr
    if unit in as_ns:  # unit is regular
        return int_repr * as_ns[unit]

    # unit is irregular ('M', 'Y')
    cdef object start = date_to_days(start_year, start_month, start_day)
    cdef object end

    if unit == "Y":  # convert years to days, accounting for leap years
        end = date_to_days(start_year + int_repr, start_month, start_day)
    elif unit == "M":  # convert months to days, accounting for unequal lengths
        end = date_to_days(start_year, start_month + int_repr, start_day)
    else:
        raise ValueError(f"could not interpret timedelta64 with unit: {unit}")

    return (end - start) * as_ns["D"]


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[long int] pandas_timedelta_to_ns_vector(
    np.ndarray[object] arr
):
    """Internal C-style interface for pandas_timedelta_to_ns when applied to
    object arrays that exclusively contain `pd.Timedelta` elements.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[long int] result = np.empty(arr_length, dtype="i8")

    for i in range(arr_length):
        result[i] = pandas_timedelta_to_ns_scalar(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] pytimedelta_to_ns_vector(np.ndarray[object] arr):
    """Internal C-style interface for pytimedelta_to_ns when applied to object
    arrays that exclusively contain `datetime.timedelta` elements.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = pytimedelta_to_ns_scalar(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] numpy_timedelta64_to_ns_vector(
    np.ndarray[object] arr,
    object start_year,
    object start_month,
    object start_day
):
    """Internal C-style interface for numpy_timedelta64_to_ns when applied to
    object arrays that exclusively contain `numpy.timedelta64` elements.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = numpy_timedelta64_to_ns_scalar(
            arr[i],
            start_year=start_year,
            start_month=start_month,
            start_day=start_day
        )

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] mixed_timedelta_to_ns_vector(
    np.ndarray[object] arr,
    object start_year,
    object start_month,
    object start_day
):
    """Internal C-style interface for timedelta_to_ns when applied to object
    arrays that contain mixed and inconsistent timedelta types.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object element
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        element = arr[i]
        if isinstance(element, pd.Timedelta):
            result[i] = pandas_timedelta_to_ns_scalar(element)
        elif isinstance(element, datetime.timedelta):
            result[i] = pytimedelta_to_ns_scalar(element)
        else:
            result[i] = numpy_timedelta64_to_ns_scalar(
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
    """Convert `pd.Timedelta` objects into an equivalent number of nanoseconds.
    
    Parameters
    ----------
    arg (pd.Timedelta | np.ndarray | pd.Series):
        Timedelta to convert.  Must be a scalar of type `pd.Timedelta` or an
        array-like containing exclusively `pd.Timedelta` objects.

    Returns
    -------
    int | np.ndarray | pd.Series:
        The integer nanosecond equivalent for each `pd.Timedelta` stored in
        `arg`.  Series inputs preserve the original index.
    """
    # np.array
    if isinstance(arg, np.ndarray):
        return pandas_timedelta_to_ns_vector(arg)

    # pd.Series
    if isinstance(arg, pd.Series):
        if pd.api.types.is_timedelta64_ns_dtype(arg):
            return arg.astype(np.int64)

        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = pandas_timedelta_to_ns_vector(arg)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return pandas_timedelta_to_ns_scalar(arg)


def pytimedelta_to_ns(
    arg: datetime.timedelta | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """Convert `datetime.timedelta` objects into an equivalent number of
    nanoseconds.
    
    Parameters
    ----------
    arg (datetime.timedelta | np.ndarray | pd.Series):
        Timedelta to convert.  Must be a scalar of type `datetime.timedelta` or
        an array-like containing exclusively `datetime.timedelta` objects.

    Returns
    -------
    int | np.ndarray | pd.Series:
        The integer nanosecond equivalent for each `datetime.timedelta` stored
        in `arg`.  Series inputs preserve the original index.
    """
    # np.array
    if isinstance(arg, np.ndarray):
        return pytimedelta_to_ns_vector(arg)

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = pytimedelta_to_ns_vector(arg)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return pytimedelta_to_ns_scalar(arg)


def numpy_timedelta64_to_ns(
    arg: np.timedelta64 | np.ndarray | pd.Series,
    since: str | datetime_like = "2001-01-01 00:00:00+0000"
) -> int | np.ndarray | pd.Series:
    """Convert `np.timedelta64` objects into an equivalent number of
    nanoseconds.
    
    Parameters
    ----------
    arg (np.timedelta64 | np.ndarray | pd.Series):
        Timedelta to convert.  Must be a scalar of type `np.timedelta64` or an
        array-like containing exclusively `np.timedelta64` objects.  Irregular
        units 'M' and 'Y' are supported through the optional `epoch` argument.
    since (str | datetime_like):
        The epoch to use for irregular timedelta64 units 'M' and 'Y', which
        need a specified starting point to maintain accuracy.  Custom epochs
        can be specified using direct `(year, month, day)` integer tuples,
        string specifiers ('utc', etc.), or date-like objects (anything that
        can be decomposed into `(year, month, day)`).  Defaults to
        `(2001, 1, 1)`, which represents the start of a 400-year Gregorian
        cycle and puts leap years at the end of each 4-year period.

    Returns
    -------
    int | np.ndarray | pd.Series:
        The integer nanosecond equivalent for each `np.timedelta64` stored in
        `arg`.  Series inputs preserve the original index.
    """
    # resolve `since` date and split into year, month, day
    start_year, start_month, start_day = epoch_date(since).values()

    # np.array
    if isinstance(arg, np.ndarray):
        if np.issubdtype(arg.dtype, "m8"):
            return numpy_timedelta64_to_ns_scalar(
                arg,
                start_year=start_year,
                start_month=start_month,
                start_day=start_day
            )
        return numpy_timedelta64_to_ns_vector(
            arg,
            start_year=start_year,
            start_month=start_month,
            start_day=start_day
        )

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = numpy_timedelta64_to_ns_vector(
            arg,
            start_year=start_year,
            start_month=start_month,
            start_day=start_day
        )
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return numpy_timedelta64_to_ns_scalar(
        arg,
        start_year=start_year,
        start_month=start_month,
        start_day=start_day
    )


def timedelta_to_ns(
    arg: timedelta_like | np.ndarray | pd.Series,
    since: str | datetime_like = "2001-01-01"
) -> int | np.ndarray | pd.Series:
    """Convert arbitrary timedeltas into an equivalent number of nanoseconds.

    Parameters
    ----------
    arg (timedelta_like | np.ndarray | pd.Series):
        Timedelta to convert.  If `np.timedelta64` objects with irregular units
        'M' and 'Y' are encountered, they are interpreted with respect to the
        given `epoch`.
    since (tuple | str | date_like):
        The epoch to use for irregular timedelta64 units 'M' and 'Y', which
        need a specified starting point to maintain accuracy.  Custom epochs
        can be specified using direct `(year, month, day)` integer tuples,
        string specifiers ('utc', etc.), or date-like objects (anything that
        can be decomposed into `(year, month, day)`).  Defaults to
        `(2001, 1, 1)`, which represents the start of a 400-year Gregorian
        cycle and puts leap years at the end of each 4-year period.

    Returns
    -------
    int | np.ndarray | pd.Series:
        The integer nanosecond equivalent for each timedelta stored in `arg`.
        Series inputs preserve the original index.
    """
    # get exact element type(s) and ensure timedelta-like
    dtype = get_dtype(arg)
    if isinstance(dtype, set) and dtype - valid_timedelta_types:
        raise TypeError(f"`arg` must contain only timedelta-like "
                        f"elements, not {dtype}")

    # pd.Timedelta
    if dtype == pd.Timedelta:
        return pandas_timedelta_to_ns(arg)

    # datetime.timedelta
    if dtype == datetime.timedelta:
        return pytimedelta_to_ns(arg)

    # np.timedelta64
    if dtype == np.timedelta64:
        return numpy_timedelta64_to_ns(arg, since)

    # mixed element types
    if isinstance(dtype, set):
        # resolve `since` date and split into year, month, day
        start_year, start_month, start_day = epoch_date(since).values()

        # pd.Series
        if isinstance(arg, pd.Series):
            index = arg.index
            arg = arg.to_numpy(dtype="O")
            arg = mixed_timedelta_to_ns_vector(
                arg,
                start_year=start_year,
                start_month=start_month,
                start_day=start_day
            )
            return pd.Series(arg, index=index, copy=False)

        # np.array
        return mixed_timedelta_to_ns_vector(
            arg,
            start_year=start_year,
            start_month=start_month,
            start_day=start_day
        )

    # unrecognized element type
    raise TypeError(f"could not parse timedelta of type {dtype}")
