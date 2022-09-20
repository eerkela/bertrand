"""Convert datetime objects to a different datetime representation.

Functions
---------
    datetime_to_pandas_timestamp(
        arg: datetime_like | np.ndarray | pd.Series
    ) -> pd.Timestamp | np.ndarray | pd.Series:
        Convert datetime objects to `pandas.Timestamp`.

    datetime_to_pydatetime(
        arg: datetime_like | np.ndarray | pd.Series
    ) -> datetime.datetime | np.ndarray | pd.Series:
        Convert datetime objects to `datetime.datetime`.

    datetime_to_numpy_datetime64(
        arg: datetime_like | np.ndarray | pd.Series,
        unit: str = None,
        rounding: str = "down"
    ) -> np.datetime64 | np.ndarray | pd.Series:
        Convert datetime objects to `numpy.datetime64`.

    datetime_to_datetime(
        arg: datetime_like | np.ndarray | pd.Series
    ) -> datetime_like | np.ndarray | pd.Series:
        Convert datetime objects to their highest resolution representation.

Examples
--------
    >>> datetime_to_pandas_timestamp(pd.Timestamp.now())
    >>> datetime_to_pandas_timestamp(datetime.datetime.now())
    >>> datetime_to_pandas_timestamp(np.datetime64("2022-01-01"))
    >>> datetime_to_pandas_timestamp(np.array([pd.Timestamp.now(), datetime.datetime.now(), np.datetime64("2022-01-01")]))

    >>> datetime_to_pydatetime(pd.Timestamp.now())
    >>> datetime_to_pydatetime(datetime.datetime.now())
    >>> datetime_to_pydatetime(np.datetime64("2022-01-01"))
    >>> datetime_to_pydatetime(np.array([pd.Timestamp.now(), datetime.datetime.now(), np.datetime64("2022-01-01")]))

    >>> datetime_to_numpy_datetime64(pd.Timestamp.now())
    >>> datetime_to_numpy_datetime64(datetime.datetime.now())
    >>> datetime_to_numpy_datetime64(np.datetime64("2022-01-01"))
    >>> datetime_to_numpy_datetime64(pd.Timestamp.now(), unit="s")
    >>> datetime_to_numpy_datetime64(np.array([pd.Timestamp.now(), datetime.datetime.now(), np.datetime64("2022-01-01")]))

    >>> datetime_to_datetime(pd.Timestamp.now())
    >>> datetime_to_datetime(datetime.datetime.now())
    >>> datetime_to_datetime(np.datetime64("2022-01-01"))
    >>> datetime_to_datetime(datetime.datetime(3000, 1, 1))
    >>> datetime_to_datetime(np.datetime64("3000-01-01"))
    >>> datetime_to_datetime(np.datetime64("10000-01-01"))
    >>> datetime_to_datetime(np.array([pd.Timestamp.now(), datetime.datetime.now(), np.datetime64("2022-01-01")]))
"""
import datetime
from cpython cimport datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import check_dtype
from pdtypes.util.type_hints import datetime_like

from .from_ns import (
    ns_to_pandas_timestamp, ns_to_pydatetime, ns_to_numpy_datetime64,
    ns_to_datetime
)
from .to_ns import datetime_to_ns


######################
####    Public    ####
######################


def datetime_to_pandas_timestamp(
    arg: datetime_like | np.ndarray | pd.Series
) -> pd.Timestamp | np.ndarray | pd.Series:
    """Convert datetime objects into `pandas.Timestamp` representation.

    This function can even accept improperly-formatted datetime sequences with
    mixed datetime representations or dtype='O', converting them to homogenous
    `pandas.Timestamp` representation.

    Parameters
    ----------
    arg : datetime-like | array-like
        A datetime object or vector of datetime objects to convert.

    Returns
    -------
    pd.Timestamp | array-like
        The homogenous `pandas.Timestamp` equivalent of `arg`.

    Raises
    ------
    OverflowError
        If one or more datetime objects in `arg` exceed the available range of
        `pandas.Timestamp` objects ([`'1677-09-21 00:12:43.145224193'` -
        `'2262-04-11 23:47:16.854775807'`]).

    Examples
    --------
        >>> datetime_to_pandas_timestamp(pd.Timestamp.now())
        >>> datetime_to_pandas_timestamp(datetime.datetime.now())
        >>> datetime_to_pandas_timestamp(np.datetime64("2022-01-01"))

        >>> datetime_to_pandas_timestamp(np.array([pd.Timestamp.now(), datetime.datetime.now(), np.datetime64("2022-01-01")]))
    """
    # trivial case: no conversion necessary
    if check_dtype(arg, pd.Timestamp):
        if isinstance(arg, pd.Series) and pd.api.types.is_object_dtype(arg):
            return arg.infer_objects()
        return arg

    # convert from ns
    return ns_to_pandas_timestamp(datetime_to_ns(arg))


def datetime_to_pydatetime(
    arg: datetime_like | np.ndarray | pd.Series
) -> datetime.datetime | np.ndarray | pd.Series:
    """Convert datetime objects into `datetime.datetime` representation.

    This function can even accept improperly-formatted datetime sequences with
    mixed datetime representations or dtype='O', converting them to homogenous
    `datetime.datetime` representation.

    Parameters
    ----------
    arg : datetime-like | array-like
        A datetime object or vector of datetime objects to convert.

    Returns
    -------
    datetime.datetime | array-like
        The homogenous `datetime.datetime` equivalent of `arg`.

    Raises
    ------
    OverflowError
        If one or more datetime objects in `arg` exceed the available range of
        `datetime.datetime` objects ([`'0001-01-01 00:00:00'` -
        `'9999-12-31 23:59:59.999999'`]).

    Examples
    --------
        >>> datetime_to_pydatetime(pd.Timestamp.now())
        >>> datetime_to_pydatetime(datetime.datetime.now())
        >>> datetime_to_pydatetime(np.datetime64("2022-01-01"))

        >>> datetime_to_pydatetime(np.array([pd.Timestamp.now(), datetime.datetime.now(), np.datetime64("2022-01-01")]))
    """
    # trivial case: no conversion necessary
    if check_dtype(arg, datetime.datetime):
        return arg

    # convert from ns
    return ns_to_pydatetime(datetime_to_ns(arg))


def datetime_to_numpy_datetime64(
    arg: datetime_like | np.ndarray | pd.Series,
    unit: str = None,
    rounding: str = "down"
) -> np.datetime64 | np.ndarray | pd.Series:
    """Convert datetime objects into `datetime.datetime` representation.

    This function can even accept improperly-formatted datetime sequences with
    mixed datetime representations or dtype='O', converting them to homogenous
    `datetime.datetime` representation.

    Parameters
    ----------
    arg : datetime-like | array-like
        A datetime object or vector of datetime objects to convert.
    unit : {'ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y'}, default None
        The unit to use for the returned datetime64 objects.  If `None`, this
        will attempt to automatically find the highest resolution unit that
        can fully represent all of the given datetimes.  This unit promotion is
        overflow-safe.
    rounding : {'floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
    'half_down', 'half_up', 'half_even'}, default 'down'
        The rounding rule to use when one or more datetimes contain precision
        below `unit`.  This applies equally in the case of unit promotion with
        respect to the final chosen unit.

    Returns
    -------
    np.datetime64 | array-like
        The homogenous `numpy.datetime64` equivalent of `arg`.

    Raises
    ------
    OverflowError
        If one or more datetime objects in `arg` exceed the available range of
        `numpy.datetime64` objects with the given `unit` (up to
        [`'-9223372036854773837-01-01 00:00:00'` -
        `'9223372036854775807-01-01 00:00:00'`]).

    Examples
    --------
        >>> datetime_to_numpy_datetime64(pd.Timestamp.now())
        >>> datetime_to_numpy_datetime64(datetime.datetime.now())
        >>> datetime_to_numpy_datetime64(np.datetime64("2022-01-01"))

        >>> datetime_to_numpy_datetime64(pd.Timestamp.now(), unit="s")
    """
    # trivial case: no conversion necessary
    if isinstance(arg, np.ndarray) and np.issubdtype(arg.dtype, "M8"):
        arg_unit, step_size = np.datetime_data(arg.dtype)
        if step_size == 1:
            if ((unit and arg_unit == unit) or
                (unit is None and arg_unit == "ns")):
                return arg

    # convert from ns
    return ns_to_numpy_datetime64(
        datetime_to_ns(arg),
        unit=unit,
        rounding=rounding
    )


def datetime_to_datetime(
    arg: datetime_like | np.ndarray | pd.Series
) -> datetime_like | np.ndarray | pd.Series:
    """Convert datetime objects into the highest available resolution
    representation.

    This function can even accept improperly-formatted datetime sequences with
    mixed datetime representations or dtype='O', converting them to a
    standardized, high-resolution format.

    Parameters
    ----------
    arg : datetime-like | array-like
        A datetime object or vector of datetime objects to convert.

    Returns
    -------
    datetime.datetime | array-like
        The datetime equivalent of `arg` with the highest possible resolution
        homogenous element types.

    Raises
    ------
    OverflowError
        If one or more datetime objects in `arg` exceed the available range of
        `datetime.datetime` objects ([`'0001-01-01 00:00:00'` -
        `'9999-12-31 23:59:59.999999'`]).

    Examples
    --------
        >>> datetime_to_datetime(pd.Timestamp.now())
        >>> datetime_to_datetime(datetime.datetime.now())
        >>> datetime_to_datetime(np.datetime64("2022-01-01"))

        >>> datetime_to_datetime(datetime.datetime(3000, 1, 1))
        >>> datetime_to_datetime(np.datetime64("3000-01-01"))

        >>> datetime_to_datetime(np.datetime64("10000-01-01"))
    """
    # np.ndarray
    if isinstance(arg, np.ndarray) and np.issubdtype(arg.dtype, "M8"):
        unit, step_size = np.datetime_data(arg.dtype)
        if unit == "ns" and step_size == 1:
            return arg

    # pd.Series
    if isinstance(arg, pd.Series) and pd.api.types.is_datetime64_ns_dtype(arg):
        return arg

    # scalar
    if isinstance(arg, pd.Timestamp):
        return arg

    # attempt to find a more precise, homogenous element type
    return ns_to_datetime(datetime_to_ns(arg))
