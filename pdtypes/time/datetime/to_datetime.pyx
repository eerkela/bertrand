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
    """TODO"""
    if check_dtype(arg, pd.Timestamp):
        return arg
    return ns_to_pandas_timestamp(datetime_to_ns(arg))


def datetime_to_pydatetime(
    arg: datetime_like | np.ndarray | pd.Series
) -> datetime.datetime | np.ndarray | pd.Series:
    """TODO"""
    if check_dtype(arg, datetime.datetime):
        return arg
    return ns_to_pydatetime(datetime_to_ns(arg))


def datetime_to_numpy_datetime64(
    arg: datetime_like | np.ndarray | pd.Series,
    unit: str = None
) -> np.datetime64 | np.ndarray | pd.Series:
    """TODO"""
    if isinstance(arg, np.ndarray) and np.issubdtype(arg.dtype, "M8"):
        arg_unit, step_size = np.datetime_data(arg.dtype)
        if step_size == 1:
            if ((unit and arg_unit == unit) or
                (unit is None and arg_unit == "ns")):
                return arg
    return ns_to_numpy_datetime64(datetime_to_ns(arg), unit=unit)


def datetime_to_datetime(
    arg: datetime_like | np.ndarray | pd.Series
) -> datetime_like | np.ndarray | pd.Series:
    """TODO"""
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
