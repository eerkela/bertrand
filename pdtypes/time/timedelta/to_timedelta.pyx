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
    since: str | datetime_like = np.datetime64("2001-01-01 00:00:00")
) -> pd.Timedelta | np.ndarray | pd.Series:
    """TODO"""
    if check_dtype(arg, pd.Timedelta):
        return arg
    arg = timedelta_to_ns(arg, since=since)
    return ns_to_pandas_timedelta(arg)


def timedelta_to_pytimedelta(
    arg: timedelta_like | np.ndarray | pd.Series,
    since: str | datetime_like = np.datetime64("2001-01-01 00:00:00")
) -> datetime.timedelta | np.ndarray | pd.Series:
    """TODO"""
    if check_dtype(arg, datetime.timedelta):
        return arg
    arg = timedelta_to_ns(arg, since=since)
    return ns_to_pytimedelta(arg)


def timedelta_to_numpy_timedelta64(
    arg: timedelta_like | np.ndarray | pd.Series,
    since: str | datetime_like = np.datetime64("2001-01-01 00:00:00"),
    unit: str = None
) -> np.timedelta64 | np.ndarray | pd.Series:
    """TODO"""
    arg = timedelta_to_ns(arg, since=since)
    return ns_to_numpy_timedelta64(arg, since=since, unit=unit)


def timedelta_to_timedelta(
    arg: timedelta_like | np.ndarray | pd.Series,
    since: str | datetime_like = np.datetime64("2001-01-01 00:00:00")
) -> timedelta_like | np.ndarray | pd.Series:
    """TODO"""
    # np.ndarray
    if isinstance(arg, np.ndarray) and np.issubdtype(arg.dtype, "m8"):
        unit, step_size = np.datetime_data(arg.dtype)
        if unit == "ns" and step_size == 1:
            return arg

    # pd.Series
    if isinstance(arg, pd.Series) and pd.api.types.is_datetime64_ns_dtype(arg):
        return arg

    # scalar
    if isinstance(arg, pd.Timestamp):
        return arg

    # attempt to find a more precise/homogenous element type
    arg = timedelta_to_ns(arg, since=since)
    return ns_to_timedelta(arg, since=since)
