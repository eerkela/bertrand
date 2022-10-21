"""Convert timedelta objects to a different timedelta representation.

Functions
---------
    timedelta_to_pandas_timedelta(
        arg: timedelta_like | np.ndarray | pd.Series,
        since: str | datetime_like = "2001-01-01 00:00:00"
    ) -> pd.Timedelta | np.ndarray | pd.Series:
        Convert timedelta objects into `pandas.Timedelta` representation.

    timedelta_to_pytimedelta(
        arg: timedelta_like | np.ndarray | pd.Series,
        since: str | datetime_like = "2001-01-01 00:00:00"
    ) -> datetime.timedelta | np.ndarray | pd.Series:
        Convert timedelta objects into `datetime.timedelta` representation.

    timedelta_to_numpy_timedelta64(
        arg: timedelta_like | np.ndarray | pd.Series,
        since: str | datetime_like = "2001-01-01 00:00:00",
        unit: str = None,
        rounding: str = "down"
    ) -> np.timedelta64 | np.ndarray | pd.Series:
        Convert timedelta objects into `numpy.timedelta64` representation.

    timedelta_to_timedelta(
        arg: timedelta_like | np.ndarray | pd.Series,
        since: str | datetime_like = "2001-01-01 00:00:00"
    ) -> timedelta_like | np.ndarray | pd.Series:
        Convert timedelta objects into the highest available resolution
        representation.

Examples
--------
    >>> timedelta_to_pandas_timedelta(pd.Timedelta(nanoseconds=123))
    >>> timedelta_to_pandas_timedelta(datetime.timedelta(microseconds=123))
    >>> timedelta_to_pandas_timedelta(np.timedelta64(123, "ns"))
    >>> timedelta_to_pandas_timedelta(np.array([pd.Timedelta(nanoseconds=1), datetime.timedelta(microseconds=2), np.timedelta64(3, "ns")]))

    >>> timedelta_to_pytimedelta(pd.Timedelta(microseconds=123))
    >>> timedelta_to_pytimedelta(datetime.timedelta(microseconds=123))
    >>> timedelta_to_pytimedelta(np.timedelta64(123, "us"))
    >>> timedelta_to_pytimedelta(np.array([pd.Timedelta(microseconds=1), datetime.timedelta(microseconds=2), np.timedelta64(3, "us")]))

    >>> timedelta_to_numpy_timedelta64(pd.Timedelta(nanoseconds=123))
    >>> timedelta_to_numpy_timedelta64(datetime.timedelta(microseconds=123))
    >>> timedelta_to_numpy_timedelta64(np.timedelta64(123, "ns"))
    >>> timedelta_to_numpy_timedelta64(np.array([pd.Timedelta(nanoseconds=1), datetime.timedelta(microseconds=2), np.timedelta64(3, "ns")]))
    >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="ns")
    >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="us")
    >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="ms")
    >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="s")
    >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="m")
    >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="h")
    >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="D")
    >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="W")
    >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="M")
    >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="Y")

    >>> timedelta_to_timedelta(pd.Timedelta(nanoseconds=123))
    >>> timedelta_to_timedelta(datetime.timedelta(microseconds=123))
    >>> timedelta_to_timedelta(np.timedelta64(123, "ns"))
    >>> timedelta_to_timedelta(np.array([pd.Timedelta(nanoseconds=1), datetime.timedelta(microseconds=2), np.timedelta64(3, "ns")]))
    >>> timedelta_to_timedelta(pd.Timedelta.max)
    >>> timedelta_to_timedelta(datetime.timedelta.max)
    >>> timedelta_to_timedelta(np.timedelta64(2**63 - 1, "s"))
"""
import datetime
from cpython cimport datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import check_dtype
from pdtypes.util.type_hints import datetime_like, timedelta_like

from .from_ns import (
    ns_to_pandas_timedelta, ns_to_pytimedelta, ns_to_numpy_timedelta64,
    ns_to_timedelta
)
from .to_ns import timedelta_to_ns


######################
####    Public    ####
######################


def timedelta_to_pandas_timedelta(
    arg: timedelta_like | np.ndarray | pd.Series,
    since: str | datetime_like = "2001-01-01 00:00:00"
) -> pd.Timedelta | np.ndarray | pd.Series:
    """Convert timedelta objects into `pandas.Timedelta` representation.

    This function can even accept improperly-formatted timedelta sequences with
    mixed timedelta representations or dtype='O', converting them to homogenous
    `pandas.Timedelta` representation.

    Parameters
    ----------
    arg : timedelta-like | array-like
        A timedelta object or a vector of such objects.  Irregular timedelta64
        units 'M' and 'Y' are supported through the optional `since` argument.
    since : str | datetime-like, default '2001-01-01 00:00:00+0000'
        The date from which to begin counting.  This is only used for
        `numpy.timedelta64` objects with unit 'M' or 'Y', in order to
        accurately account for leap days and unequal month lengths.  Only the
        `year`, `month`, and `day` components are used.  Defaults to
        '2001-01-01 00:00:00+0000', which represents the start of a 400-year
        Gregorian calendar cycle.

    Returns
    -------
    pd.Timedelta | array-like
        The homogenous `pandas.Timedelta` equivalent of `arg`.

    Raises
    ------
    OverflowError
        If one or more timedelta objects in `arg` exceed the available range of
        `pandas.Timedelta` objects ([`'-106752 days +00:12:43.145224193'` -
        `'106751 days 23:47:16.854775807'`]).

    Examples
    --------
        >>> timedelta_to_pandas_timedelta(pd.Timedelta(nanoseconds=123))
        >>> timedelta_to_pandas_timedelta(datetime.timedelta(microseconds=123))
        >>> timedelta_to_pandas_timedelta(np.timedelta64(123, "ns"))

        >>> timedelta_to_pandas_timedelta(np.array([pd.Timedelta(nanoseconds=1), datetime.timedelta(microseconds=2), np.timedelta64(3, "ns")]))
    """
    # trivial case: no conversion necessary
    if check_dtype(arg, pd.Timedelta):
        if isinstance(arg, pd.Series) and pd.api.types.is_object_dtype(arg):
            return arg.infer_objects()
        return arg

    # convert from ns
    return ns_to_pandas_timedelta(timedelta_to_ns(arg, since=since))


def timedelta_to_pytimedelta(
    arg: timedelta_like | np.ndarray | pd.Series,
    since: str | datetime_like = "2001-01-01 00:00:00"
) -> datetime.timedelta | np.ndarray | pd.Series:
    """Convert timedelta objects into `datetime.timedelta` representation.

    This function can even accept improperly-formatted timedelta sequences with
    mixed timedelta representations or dtype='O', converting them to homogenous
    `datetime.timedelta` representation.

    Parameters
    ----------
    arg : timedelta-like | array-like
        A timedelta object or a vector of such objects.  Irregular timedelta64
        units 'M' and 'Y' are supported through the optional `since` argument.
    since : str | datetime-like, default '2001-01-01 00:00:00+0000'
        The date from which to begin counting.  This is only used for
        `numpy.timedelta64` objects with unit 'M' or 'Y', in order to
        accurately account for leap days and unequal month lengths.  Only the
        `year`, `month`, and `day` components are used.  Defaults to
        '2001-01-01 00:00:00+0000', which represents the start of a 400-year
        Gregorian calendar cycle.

    Returns
    -------
    datetime.timedelta | array-like
        The homogenous `datetime.timedelta` equivalent of `arg`.

    Raises
    ------
    OverflowError
        If one or more timedelta objects in `arg` exceed the available range of
        `pandas.Timedelta` objects ([`'-999999999 days, 0:00:00'` -
        `'999999999 days, 23:59:59.999999'`]).

    Examples
    --------
        >>> timedelta_to_pytimedelta(pd.Timedelta(microseconds=123))
        >>> timedelta_to_pytimedelta(datetime.timedelta(microseconds=123))
        >>> timedelta_to_pytimedelta(np.timedelta64(123, "us"))

        >>> timedelta_to_pytimedelta(np.array([pd.Timedelta(microseconds=1), datetime.timedelta(microseconds=2), np.timedelta64(3, "us")]))
    """
    # trivial case: no conversion necessary
    if check_dtype(arg, datetime.timedelta):
        return arg

    # convert from ns
    return ns_to_pytimedelta(timedelta_to_ns(arg, since=since))


def timedelta_to_numpy_timedelta64(
    arg: timedelta_like | np.ndarray | pd.Series,
    since: str | datetime_like = "2001-01-01 00:00:00",
    unit: str = None,
    rounding: str = "down"
) -> np.timedelta64 | np.ndarray | pd.Series:
    """Convert timedelta objects into `numpy.timedelta64` representation.

    This function can even accept improperly-formatted timedelta sequences with
    mixed timedelta representations or dtype='O', converting them to homogenous
    `numpy.timedelta64` representation.

    Parameters
    ----------
    arg : timedelta-like | array-like
        A timedelta object or a vector of such objects.  Irregular timedelta64
        units 'M' and 'Y' are supported through the optional `since` argument.
    since : str | datetime-like, default '2001-01-01 00:00:00+0000'
        The date from which to begin counting.  This is only used for
        `numpy.timedelta64` objects with unit 'M' or 'Y', in order to
        accurately account for leap days and unequal month lengths.  Only the
        `year`, `month`, and `day` components are used.  Defaults to
        '2001-01-01 00:00:00+0000', which represents the start of a 400-year
        Gregorian calendar cycle.
    unit : str, default None
        The unit to use for the returned timedelta.  If `None`, choose the
        highest resolution unit that can fully represent `arg`.
    rounding : str, default 'down'
        The rounding strategy to use when a value underflows beyond the
        resolution of `unit`.  Defaults to `'down'` (round toward zero).

    Returns
    -------
    numpy.timedelta64 | array-like
        The homogenous `numpy.timedelta64` equivalent of `arg`.

    Raises
    ------
    OverflowError
        If the range of `arg` exceeds the representable range of
        `numpy.timedelta64` objects (up to [`'-9223372036854775807 years'` -
        `'9223372036854775807 years'`]) with the given `unit`.

    Examples
    --------
        >>> timedelta_to_numpy_timedelta64(pd.Timedelta(nanoseconds=123))
        >>> timedelta_to_numpy_timedelta64(datetime.timedelta(microseconds=123))
        >>> timedelta_to_numpy_timedelta64(np.timedelta64(123, "ns"))

        >>> timedelta_to_numpy_timedelta64(np.array([pd.Timedelta(nanoseconds=1), datetime.timedelta(microseconds=2), np.timedelta64(3, "ns")]))

        >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="ns")
        >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="us")
        >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="ms")
        >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="s")
        >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="m")
        >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="h")
        >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="D")
        >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="W")
        >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="M")
        >>> timedelta_to_numpy_timedelta64(pd.Timedelta.max, unit="Y")
    """
    # trivial case: no conversion necessary
    if isinstance(arg, np.ndarray) and np.issubdtype(arg.dtype, "m8"):
        arg_unit, step_size = np.datetime_data(arg.dtype)
        if step_size == 1:
            if ((unit and arg_unit == unit) or
                (unit is None and arg_unit == "ns")):
                return arg

    # convert from ns
    return ns_to_numpy_timedelta64(
        timedelta_to_ns(arg, since=since),
        since=since,
        unit=unit,
        rounding=rounding
    )


def timedelta_to_timedelta(
    arg: timedelta_like | np.ndarray | pd.Series,
    since: str | datetime_like = "2001-01-01 00:00:00"
) -> timedelta_like | np.ndarray | pd.Series:
    """Convert timedelta objects into the highest available resolution
    representation.

    This function can even accept improperly-formatted timedelta sequences with
    mixed timedelta representations or dtype='O', converting them to a
    standardized, high-resolution format.

    Parameters
    ----------
    arg : timedelta-like | array-like
        A timedelta object or a vector of such objects.  Irregular timedelta64
        units 'M' and 'Y' are supported through the optional `since` argument.
    since : str | datetime-like, default '2001-01-01 00:00:00+0000'
        The date from which to begin counting.  This is only used for
        `numpy.timedelta64` objects with unit 'M' or 'Y', in order to
        accurately account for leap days and unequal month lengths.  Only the
        `year`, `month`, and `day` components are used.  Defaults to
        '2001-01-01 00:00:00+0000', which represents the start of a 400-year
        Gregorian calendar cycle.

    Returns
    -------
    timedelta-like | array-like
        The timedelta equivalent of `arg` with the highest possible resolution
        homogenous element types.

    Raises
    ------
    OverflowError
        If the range of `arg` exceeds the representable range of
        `numpy.timedelta64` objects (up to [`'-9223372036854775807 years'` -
        `'9223372036854775807 years'`]).

    Examples
    --------
        >>> timedelta_to_timedelta(pd.Timedelta(nanoseconds=123))
        >>> timedelta_to_timedelta(datetime.timedelta(microseconds=123))
        >>> timedelta_to_timedelta(np.timedelta64(123, "ns"))

        >>> timedelta_to_timedelta(np.array([pd.Timedelta(nanoseconds=1), datetime.timedelta(microseconds=2), np.timedelta64(3, "ns")]))

        >>> timedelta_to_timedelta(pd.Timedelta.max)
        >>> timedelta_to_timedelta(datetime.timedelta.max)
        >>> timedelta_to_timedelta(np.timedelta64(2**63 - 1, "s"))
    """
    # np.ndarray
    if isinstance(arg, np.ndarray) and np.issubdtype(arg.dtype, "m8"):
        unit, step_size = np.datetime_data(arg.dtype)
        if unit == "ns" and step_size == 1:
            return arg

    # pd.Series
    if isinstance(arg, pd.Series) and pd.api.types.is_timedelta64_ns_dtype(arg):
        return arg

    # scalar
    if isinstance(arg, pd.Timestamp):
        return arg

    # attempt to find a more precise/homogenous element type
    arg = timedelta_to_ns(arg, since=since)
    return ns_to_timedelta(arg, since=since)
