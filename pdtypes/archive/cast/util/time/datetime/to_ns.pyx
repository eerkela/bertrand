"""Convert datetime objects into nanosecond offsets from the UTC epoch
('1970-01-01 00:00:00+0000').

Functions
---------
pandas_timestamp_to_ns(
    arg: pd.Timestamp | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None
) -> int | np.ndarray | pd.Series:
    Convert `pandas.Timestamp` objects into nanosecond offsets from UTC.

pydatetime_to_ns(
    arg: datetime.datetime | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None
) -> int | np.ndarray | pd.Series:
    Convert `datetime.datetime` objects into nanosecond offsets from UTC.

numpy_datetime64_to_ns(
    arg: np.datetime64 | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    Convert `numpy.datetime64` objects into nanosecond offsets from UTC.

datetime_to_ns(
    arg: datetime_like | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None
) -> int | np.ndarray | pd.Series:
    Convert arbitrary datetime objects into nanosecond offsets from UTC.
"""
import datetime
from cpython cimport datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd
import pytz

from pdtypes.check import get_dtype
from pdtypes.util.type_hints import datetime_like

from ..calendar import date_to_days
from ..timezone import is_utc, timezone
from ..timezone cimport localize_pydatetime_scalar
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


cdef inline long int pandas_timestamp_to_ns_scalar(
    object timestamp,
    datetime.tzinfo tz
):
    """Convert a scalar `pd.Timestamp` object into a nanosecond offset from the
    UTC epoch object ('1970-01-01 00:00:00+0000').
    """
    # naive
    if tz is not None and not timestamp.tzinfo:  # interpret according to `tz`
        timestamp = timestamp.tz_localize(tz)

    # aware
    return timestamp.value


cdef inline object pydatetime_to_ns_scalar(
    datetime.datetime pydatetime,
    datetime.tzinfo tz
):
    """Convert a scalar `datetime.datetime` object into a nanosecond offset
    from the UTC epoch object ('1970-01-01 00:00:00+0000').
    """
    cdef datetime.timedelta delta

    # convert to timedelta
    if pydatetime.tzinfo is None:  # datetime is naive
        # interpret according to `tz`
        if tz is None or is_utc(tz):
            delta = pydatetime - utc_naive_pydatetime
        else:
            if isinstance(tz, pytz.BaseTzInfo):  # use .localize()
                delta = tz.localize(pydatetime) - utc_aware_pydatetime
            else:  # replace tzinfo directly
                delta = pydatetime.replace(tzinfo=tz) - utc_aware_pydatetime
    else:  # datetime is aware
        delta = pydatetime - utc_aware_pydatetime

    # convert timedelta to ns.  Note: importing this from timedelta.to_ns
    # causes a circular import error due to epoch.pyx
    cdef np.ndarray[object] components = np.array(
        [
            delta.days,
            delta.seconds,
            delta.microseconds
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
cdef np.ndarray[long int] pandas_timestamp_to_ns_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz
):
    """Convert an array of `pandas.Timestamp` objects into nanosecond offsets
    from the utc epoch ('1970-01-01 00:00:00+0000').
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[long int] result = np.empty(arr_length, dtype="i8")

    for i in range(arr_length):
        result[i] = pandas_timestamp_to_ns_scalar(arr[i], tz=tz)

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] pydatetime_to_ns_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz
):
    """Convert an array of `datetime.datetime` objects into nanosecond offsets
    from the utc epoch ('1970-01-01 00:00:00+0000').
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = pydatetime_to_ns_scalar(arr[i], tz=tz)

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
cdef np.ndarray[object] mixed_datetime_to_ns_vector(
    np.ndarray[object] arr,
    datetime.tzinfo tz
):
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
            result[i] = pandas_timestamp_to_ns_scalar(element, tz=tz)
        elif isinstance(element, datetime.datetime):
            result[i] = pydatetime_to_ns_scalar(element, tz=tz)
        else:
            result[i] = numpy_datetime64_to_ns_scalar(element)

    return result


######################
####    Public    ####
######################


def pandas_timestamp_to_ns(
    arg: pd.Timestamp | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None
) -> int | np.ndarray | pd.Series:
    """Convert `pandas.Timestamp` objects into nanosecond offsets from the UTC
    epoch ('1970-01-01 00:00:00+0000').

    Parameters
    ----------
    arg : pd.Timestamp | array-like
        A `pandas.Timestamp` object or vector of such objects.
    tz : str | datetime.tzinfo, default None
        The timezone to use for naive datetime inputs.  This can be `None`
        (indicating UTC), an instance of `datetime.tzinfo` or one of its
        derivatives (from `pytz`, `zoneinfo`, etc.), or an IANA timezone
        database string ('US/Eastern', 'UTC', etc.).  The special value
        `'local'` is also accepted, referencing the system's local time zone.

    Returns
    -------
    int | array-like
        The integer nanosecond offset from the UTC epoch
        ('1970-01-01 00:00:00+0000') that corresponds to `arg`.  This is always
        measured in UTC time.

    Examples
    --------
    Inputs can be timezone-naive or aware:

    >>> pandas_timestamp_to_ns(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789")
    ... )
    123456789
    >>> pandas_timestamp_to_ns(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789", tz="UTC")
    ... )
    123456789
    >>> pandas_timestamp_to_ns(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789", tz="US/Pacific")
    ... )
    28800123456789

    Note that by default, naive inputs are treated identically to UTC aware
    ones.  This behavior can be customized using the optional `tz` argument:

    >>> pandas_timestamp_to_ns(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     tz="US/Pacific"
    ... )
    28800123456789

    Input datetimes can also be vectorized:

    >>> timestamps = pd.to_datetime(pd.Series([1, 2, 3]), unit="D")
    >>> timestamps
    0   1970-01-02
    1   1970-01-03
    2   1970-01-04
    dtype: datetime64[ns]
    >>> pandas_timestamp_to_ns(timestamps)
    0     86400000000000
    1    172800000000000
    2    259200000000000
    dtype: int64
    >>> pandas_timestamp_to_ns(timestamps.to_numpy(dtype="O"))
    array([ 86400000000000, 172800000000000, 259200000000000])

    With the same rules for `tz`:

    >>> pandas_timestamp_to_ns(timestamps, tz="US/Pacific")
    0    115200000000000
    1    201600000000000
    2    288000000000000
    dtype: int64
    >>> pandas_timestamp_to_ns(timestamps.to_numpy(dtype="O"), tz="US/Pacific")
    array([115200000000000, 201600000000000, 288000000000000])

    Inputs can even contain mixed aware/naive and/or mixed timezone datetimes:

    >>> mixed = [
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789", tz="US/Pacific"),
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789", tz="Europe/Berlin")
    ... ]
    >>> pandas_timestamp_to_ns(pd.Series(mixed, dtype="O"))
    0         123456789
    1    28800123456789
    2    -3599876543211
    dtype: int64
    >>> pandas_timestamp_to_ns(np.array(mixed))
    array([     123456789, 28800123456789, -3599876543211])
    """
    # resolve timezone
    tz = timezone(tz)

    # np.ndarray
    if isinstance(arg, np.ndarray):
        return pandas_timestamp_to_ns_vector(arg, tz=tz)

    # pd.Series
    if isinstance(arg, pd.Series):
        if pd.api.types.is_datetime64_ns_dtype(arg):  # use `.dt` namespace
            if not arg.dt.tz:  # series is naive, interpret according to `tz`
                if tz is not None and not is_utc(tz):  # non-UTC `tz`
                    arg = arg.dt.tz_localize(tz)
            return arg.astype(np.int64)

        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = pandas_timestamp_to_ns_vector(arg, tz=tz)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return pandas_timestamp_to_ns_scalar(arg, tz=tz)


def pydatetime_to_ns(
    arg: datetime.datetime | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None
) -> int | np.ndarray | pd.Series:
    """Convert `datetime.datetime` objects into nanosecond offsets from the UTC
    epoch ('1970-01-01 00:00:00+0000').

    Parameters
    ----------
    arg : datetime.datetime | array-like
        A `datetime.datetime` object or vector of such objects.
    tz : str | datetime.tzinfo, default None
        The timezone to use for naive datetime inputs.  This can be `None`
        (indicating UTC), an instance of `datetime.tzinfo` or one of its
        derivatives (from `pytz`, `zoneinfo`, etc.), or an IANA timezone
        database string ('US/Eastern', 'UTC', etc.).  The special value
        `'local'` is also accepted, referencing the system's local time zone.

    Returns
    -------
    int | array-like
        The integer nanosecond offset from the UTC epoch
        ('1970-01-01 00:00:00+0000') that corresponds to `arg`.  This is always
        measured in UTC time.

    Examples
    --------
    Inputs can be timezone-naive or aware:

    >>> import pytz
    >>> pydatetime_to_ns(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456")
    ... )
    123456000
    >>> pydatetime_to_ns(
    ...     pytz.timezone("UTC").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456")
    ...     )
    ... )
    123456000
    >>> pydatetime_to_ns(
    ...     pytz.timezone("US/Pacific").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456")
    ...     )
    ... )
    28800123456000

    Note that by default, naive inputs are treated identically to UTC aware
    ones.  This behavior can be customized using the optional `tz` argument:

    >>> pydatetime_to_ns(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456"),
    ...     tz="US/Pacific"
    ... )
    28800123456000

    Input datetimes can also be vectorized:

    >>> pydatetimes = [datetime.datetime.fromisoformat(f"1970-01-0{i}")
    ...                for i in range(2, 5)]
    >>> pydatetimes
    [datetime.datetime(1970, 1, 2, 0, 0), datetime.datetime(1970, 1, 3, 0, 0), datetime.datetime(1970, 1, 4, 0, 0)]
    >>> pydatetime_to_ns(pd.Series(pydatetimes, dtype="O"))
    0     86400000000000
    1    172800000000000
    2    259200000000000
    dtype: object
    >>> pydatetime_to_ns(np.array(pydatetimes))
    array([86400000000000, 172800000000000, 259200000000000], dtype=object)

    With the same rules for `tz`:

    >>> pydatetimes = [datetime.datetime.fromisoformat(f"1970-01-0{i}")
    ...                for i in range(2, 5)]
    >>> pydatetime_to_ns(pd.Series(pydatetimes, dtype="O"), tz="US/Pacific")
    0    115200000000000
    1    201600000000000
    2    288000000000000
    dtype: object
    >>> pydatetime_to_ns(np.array(pydatetimes), tz="US/Pacific")
    array([115200000000000, 201600000000000, 288000000000000], dtype=object)

    Inputs can even contain mixed aware/naive and/or mixed timezone datetimes:

    >>> import pytz
    >>> mixed = [
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456"),
    ...     pytz.timezone("US/Pacific").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456")
    ...     ),
    ...     pytz.timezone("Europe/Berlin").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456")
    ...     )
    ... ]
    >>> pydatetime_to_ns(pd.Series(mixed, dtype="O"))
    0         123456000
    1    28800123456000
    2    -3599876544000
    dtype: object
    >>> pydatetime_to_ns(np.array(mixed))
    array([123456000, 28800123456000, -3599876544000], dtype=object)
    """
    # resolve timezone
    tz = timezone(tz)

    # np.ndarray
    if isinstance(arg, np.ndarray):
        return pydatetime_to_ns_vector(arg, tz=tz)

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = pydatetime_to_ns_vector(arg, tz=tz)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return pydatetime_to_ns_scalar(arg, tz=tz)


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

    Notes
    -----
    `numpy.datetime64` objects do not carry timezone information, and are thus
    always treated as UTC times.

    Examples
    --------
    Inputs can have any unit:

    >>> numpy_datetime64_to_ns(np.datetime64(1, "ns"))
    1
    >>> numpy_datetime64_to_ns(np.datetime64(1, "us"))
    1000
    >>> numpy_datetime64_to_ns(np.datetime64(1, "ms"))
    1000000
    >>> numpy_datetime64_to_ns(np.datetime64(1, "s"))
    1000000000
    >>> numpy_datetime64_to_ns(np.datetime64(1, "m"))
    60000000000
    >>> numpy_datetime64_to_ns(np.datetime64(1, "h"))
    3600000000000
    >>> numpy_datetime64_to_ns(np.datetime64(1, "D"))
    86400000000000
    >>> numpy_datetime64_to_ns(np.datetime64(1, "W"))
    604800000000000
    >>> numpy_datetime64_to_ns(np.datetime64(1, "M"))
    2678400000000000
    >>> numpy_datetime64_to_ns(np.datetime64(1, "Y"))
    31536000000000000

    With irregular lengths in units 'M' and 'Y' accounted for:

    >>> (numpy_datetime64_to_ns(np.datetime64("1970-03")) -
    ...  numpy_datetime64_to_ns(np.datetime64("1970-02")))
    2419200000000000
    >>> (numpy_datetime64_to_ns(np.datetime64("1973")) -
    ...  numpy_datetime64_to_ns(np.datetime64("1972"))
    31622400000000000

    Inputs can also be vectorized:

    >>> datetime64_objects = np.arange(1, 3, dtype="M8[D]")
    >>> datetime64_objects
    array(['1970-01-02', '1970-01-03', '1970-01-04'], dtype='datetime64[D]')
    >>> numpy_datetime64_to_ns(pd.Series(list(datetime64_objects), dtype="O"))
    0     86400000000000
    1    172800000000000
    2    259200000000000
    dtype: object
    >>> numpy_datetime64_to_ns(datetime64_objects)
    array([86400000000000, 172800000000000, 259200000000000], dtype=object)

    With potentially mixed units:

    >>> mixed = [
    ...     np.datetime64(123456789, "ns"),
    ...     np.datetime64(123456, "us"),
    ...     np.datetime64(123, "ms")
    ... ]
    >>> numpy_datetime64_to_ns(pd.Series(mixed, dtype="O"))
    0    123456789
    1    123456000
    2    123000000
    dtype: object
    >>> numpy_datetime64_to_ns(np.array(mixed))
    array([123456789, 123456000, 123000000], dtype=object)
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
    arg: datetime_like | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None
) -> int | np.ndarray | pd.Series:
    """Convert arbitrary datetime objects into nanosecond offsets from the UTC
    epoch ('1970-01-01 00:00:00+0000').

    Parameters
    ----------
    arg : datetime-like | array-like
        A datetime object or vector of such objects.  Can contain any
        combination of `pandas.Timestamp`, `datetime.datetime`, and/or
        `numpy.datetime64` objects.
    tz : str | datetime.tzinfo, default None
        The timezone to use for naive datetime inputs.  This can be `None`
        (indicating UTC), an instance of `datetime.tzinfo` or one of its
        derivatives (from `pytz`, `zoneinfo`, etc.), or an IANA timezone
        database string ('US/Eastern', 'UTC', etc.).  The special value
        `'local'` is also accepted, referencing the system's local time zone.

    Returns
    -------
    int | array-like
        The integer nanosecond offset from the UTC epoch
        ('1970-01-01 00:00:00+0000') that corresponds to `arg`.  This is always
        measured in UTC time.

    Examples
    --------
    Inputs can be in any datetime format:

    >>> datetime_to_ns(pd.Timestamp("1970-01-01 00:00:00.123456789"))
    123456789
    >>> datetime_to_ns(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456")
    ... )
    123456000
    >>> datetime_to_ns(np.datetime64("1970-01-01 00:00:00.123456789"))
    123456789

    And can be timezone-aware:

    >>> import pytz
    >>> datetime_to_ns(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789", tz="UTC")
    ... )
    123456789
    >>> datetime_to_ns(
    ...     datetime.datetime.fromtimestamp(0.123456, datetime.timezone.utc)
    ... )
    123456000
    >>> datetime_to_ns(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789", tz="US/Pacific")
    ... )
    28800123456789
    >>> datetime_to_ns(
    ...     pytz.timezone("US/Pacific").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456")
    ...     )
    ... )
    28800123456000

    Note that by default, naive inputs are treated identically to UTC aware
    ones.  This behavior can be customized using the optional `tz` argument:

    >>> datetime_to_ns(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     tz="US/Pacific"
    ... )
    28800123456789
    >>> datetime_to_ns(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456"),
    ...     tz="US/Pacific"
    .. )
    28800123456000

    Input datetimes can also be vectorized:

    >>> timestamps = pd.to_datetime(pd.Series([1, 2, 3]), unit="D")
    >>> timestamps
    0   1970-01-02
    1   1970-01-03
    2   1970-01-04
    dtype: datetime64[ns]
    >>> datetime_to_ns(timestamps)
    0     86400000000000
    1    172800000000000
    2    259200000000000
    dtype: int64
    >>> datetime_to_ns(timestamps.to_numpy(dtype="O"))
    array([ 86400000000000, 172800000000000, 259200000000000])

    With the same rules for `tz`:

    >>> timestamps = pd.to_datetime(pd.Series([1, 2, 3]), unit="D")
    >>> datetime_to_ns(timestamps, tz="US/Pacific")
    0    115200000000000
    1    201600000000000
    2    288000000000000
    dtype: int64
    >>> datetime_to_ns(timestamps.to_numpy(dtype="O"), tz="US/Pacific")
    array([115200000000000, 201600000000000, 288000000000000])

    And with potentially mixed datetime representations, including mixed
    aware/naive and/or mixed timezone input:

    >>> import pytz
    >>> mixed = [
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456"),
    ...     np.datetime64("1970-01-01 00:00:00.123456789"),
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789", tz="US/Pacific"),
    ...     pytz.timezone("Europe/Berlin").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00.123456")
    ...     )
    ... ]
    >>> datetime_to_ns(pd.Series(mixed, dtype="O"))
    0         123456789
    1         123456000
    2         123456789
    3    28800123456789
    4    -3599876544000
    dtype: object
    >>> datetime_to_ns(np.array(mixed))
    array([123456789, 123456000, 123456789, 28800123456789, -3599876544000],
      dtype=object)
    """
    # get exact element type(s) and ensure datetime-like
    dtype = get_dtype(arg)
    if isinstance(dtype, set) and dtype - valid_datetime_types:
        raise TypeError(f"`datetime_like` must contain only datetime-like "
                        f"elements, not {dtype}")

    # resolve timezone
    tz = timezone(tz)

    # pd.Timestamp
    if dtype == pd.Timestamp:
        return pandas_timestamp_to_ns(arg, tz=tz)

    # datetime.datetime
    if dtype == datetime.datetime:
        return pydatetime_to_ns(arg, tz=tz)

    # np.datetime64
    if dtype == np.datetime64:
        return numpy_datetime64_to_ns(arg)

    # mixed element types
    if isinstance(dtype, set):
        # pd.Series
        if isinstance(arg, pd.Series):
            index = arg.index
            arg = arg.to_numpy(dtype="O")
            arg = mixed_datetime_to_ns_vector(arg, tz=tz)
            return pd.Series(arg, index=index, copy=False)

        # np.ndarray
        return mixed_datetime_to_ns_vector(arg, tz=tz)

    # unrecognized element type
    raise TypeError(f"could not parse datetime of type {dtype}")
