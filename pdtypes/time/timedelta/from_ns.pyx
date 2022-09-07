import datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import check_dtype, get_dtype
from pdtypes.util.array import is_scalar
from pdtypes.util.type_hints import date_like, timedelta_like

from ..date import date_to_days, decompose_date
from ..unit cimport as_ns


#########################
####    Constants    ####
#########################


cdef inline object ns_to_pandas_timedelta_scalar(long int ns):
    """TODO"""
    return pd.Timedelta(nanoseconds=ns)


# @cython.cdivision(True)
cdef inline object ns_to_pytimedelta_scalar(object ns):
    """TODO"""
    return datetime.timedelta(microseconds=ns // as_ns["us"])


cdef inline object ns_to_numpy_timedelta64_scalar(object ns):
    """TODO"""
    # just use np.timedelta64 constructor directly
    raise NotImplementedError()
    # for unit, scale_factor in as_ns.items():


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] ns_to_pandas_timedelta_vector(
    np.ndarray[long int] arr
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = ns_to_pandas_timedelta_scalar(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] ns_to_pytimedelta_vector(
    np.ndarray[object] arr
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = ns_to_pytimedelta_scalar(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] ns_to_numpy_timedelta64_vector(
    np.ndarray[object] arr
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[long int] result = np.empty(arr_length, dtype="i8")

    # choose the highest-resolution unit that can represent array
    cdef object max_ns = arr.max()
    cdef object min_ns = arr.min()
    cdef str unit
    cdef long int scale_factor
    cdef str choice = None

    for unit, scale_factor in as_ns.items():
        if (min_ns // scale_factor >= -2**63 + 1 and
            max_ns // scale_factor <= 2**63 - 1):
            choice = unit
            break

    # unit choice is regular, convert directly
    if choice:
        # convert ns to timedeltas with the chosen unit
        for i in range(arr_length):
            result[i] = np.timedelta64(arr[i] // scale_factor, choice)
        return result

    # regular unit search failed; convert to days and try units 'M', 'Y'
    cdef object max_days = max_ns // as_ns["D"]
    cdef object min_days = min_ns // as_ns["D"]

    raise NotImplementedError()


######################
####    Public    ####
######################


def ns_to_pandas_timedelta(
    arg: int | np.ndarray | pd.Series
) -> pd.Timedelta | np.ndarray | pd.Series:
    """TODO"""
    # reject -2**63 (reserved for pd.NaT)

    # np.array
    if isinstance(arg, np.ndarray):
        arg = arg.astype("i8", copy=False)
        return ns_to_pandas_timedelta_vector(arg)

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype=np.int64)
        arg = ns_to_pandas_timedelta_vector(arg)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return ns_to_pandas_timedelta_scalar(arg)


def ns_to_pytimedelta(
    arg: int | np.ndarray | pd.Series
) -> datetime.timedelta | np.ndarray | pd.Series:
    """TODO"""
    # np.array
    if isinstance(arg, np.ndarray):
        arg = arg.astype("O", copy=False)
        return ns_to_pytimedelta_vector(arg)

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy(dtype="O")
        arg = ns_to_pytimedelta_vector(arg)
        return pd.Series(arg, index=index, copy=False)

    # scalar
    return ns_to_pytimedelta_scalar(arg)


def ns_to_numpy_timedelta64(
    arg: int | np.ndarray | pd.Series,
    start_year: int = 2001,
    start_month: int = 1,
    start_day: int = 1
) -> np.timedelta64 | np.ndarray | pd.Series:
    """TODO"""
    raise NotImplementedError()


def ns_to_timedelta(
    arg: int | np.ndarray | pd.Series,
    start_year: int = 2001,
    start_month: int = 1,
    start_day: int = 1
) -> timedelta_like | np.ndarray | pd.Series:
    """TODO"""
    raise NotImplementedError()



