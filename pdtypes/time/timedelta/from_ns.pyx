import datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import check_dtype, get_dtype
from pdtypes.util.array import is_scalar
from pdtypes.util.type_hints import datetime_like, timedelta_like

from pdtypes.time.date import date_to_days, days_to_date, decompose_date
from pdtypes.time.unit cimport as_ns
from pdtypes.time.epoch import epoch_date


#########################
####    Constants    ####
#########################


# min/max representable ns values for each timedelta type
cdef long int min_pandas_timedelta_ns = -2**63 + 1
cdef long int max_pandas_timedelta_ns = 2**63 - 1
cdef object min_pytimedelta_ns = -86399999913600000000000
cdef object max_pytimedelta_ns = 86399999999999999999000
cdef object min_numpy_timedelta64_ns = -291061508645168391112156800000000000
cdef object max_numpy_timedelta64_ns = 291061508645168391112243200000000000


#######################
####    Private    ####
#######################


cdef inline object ns_to_pandas_timedelta_scalar(long int ns):
    """TODO"""
    return pd.Timedelta(nanoseconds=ns)


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] ns_to_pandas_timedelta_vector(
    np.ndarray arr
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = ns_to_pandas_timedelta_scalar(arr[i])

    return result


######################
####    Public    ####
######################


def ns_to_pandas_timedelta(
    arg: int | np.ndarray | pd.Series,
    min_ns: int = None,
    max_ns: int = None
) -> pd.Timedelta | np.ndarray | pd.Series:
    """TODO"""
    # ensure min/max fall within representable range
    is_array_like = isinstance(arg, (np.ndarray, pd.Series))
    if min_ns is None:
        min_ns = int(arg.min()) if is_array_like else int(arg)
    if max_ns is None:
        max_ns = int(arg.max()) if is_array_like else int(arg)
    if min_ns < min_pandas_timedelta_ns or max_ns > max_pandas_timedelta_ns:
        raise OverflowError(f"`arg` exceeds pd.Timedelta range")

    # np.ndarray
    if isinstance(arg, np.ndarray):
        # starting from scratch is faster than pd.to_timedelta + coercion to
        # object array, although neither are particularly fast
        return ns_to_pandas_timedelta_vector(arg)

    # pd.Series/scalar - convert using pd.to_timedelta
    arg = pd.to_timedelta(arg, unit="ns")
    return arg


def ns_to_pytimedelta(
    arg: int | np.ndarray | pd.Series,
    min_ns: int = None,
    max_ns: int = None
) -> datetime.timedelta | np.ndarray | pd.Series:
    """TODO"""
    # ensure min/max fall within representable range
    is_array_like = isinstance(arg, (np.ndarray, pd.Series))
    if min_ns is None:
        min_ns = int(arg.min()) if is_array_like else int(arg)
    if max_ns is None:
        max_ns = int(arg.max()) if is_array_like else int(arg)
    if min_ns < min_pytimedelta_ns or max_ns > max_pytimedelta_ns:
        raise OverflowError(f"`arg` exceeds datetime.timedelta range")

    # convert `arg` to microseconds
    arg = arg // 10**3

    # np.ndarray
    if isinstance(arg, np.ndarray):
        try:
            return arg.astype("m8[us]").astype("O")
        except OverflowError:  # datetime.timedelta range is wider than m8[us]
            return (arg // 10**3).astype("m8[ms]").astype("O")

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy()
        try:
            arg = arg.astype("m8[us]").astype("O")
        except OverflowError:  # datetime.timedelta range is wider than m8[us]
            arg = (arg // 10**3).astype("m8[ms]").astype("O")
        return pd.Series(arg, index=index, copy=False, dtype="O")

    # scalar
    try:
        return np.timedelta64(arg, "us").item()
    except OverflowError:  # datetime.timedelta range is wider than m8[us]
        return np.timedelta64(arg // 10**3, "ms").item()


def ns_to_numpy_timedelta64(
    arg: int | np.ndarray | pd.Series,
    since: str | datetime_like = np.datetime64("2001-01-01"),
    min_ns: int = None,
    max_ns: int = None
) -> np.timedelta64 | np.ndarray | pd.Series:
    """TODO"""
    # ensure min/max fall within representable range
    is_array_like = isinstance(arg, (np.ndarray, pd.Series))
    if min_ns is None:
        min_ns = int(arg.min()) if is_array_like else int(arg)
    if max_ns is None:
        max_ns = int(arg.max()) if is_array_like else int(arg)
    if min_ns < min_numpy_timedelta64_ns or max_ns > max_numpy_timedelta64_ns:
        raise OverflowError(f"`arg` exceeds np.timedelta64 range")

    # note original arg type
    original_type = type(arg)

    # choose unit to fit range and rescale `arg` appropriately
    choice = None
    for unit, scale_factor in as_ns.items():
        if unit == "ns" and min_ns > -2**63 and max_ns < 2**63:  # don't scale
            choice = "ns"
            break
        elif min_ns // scale_factor > -2**63 and max_ns // scale_factor < 2**63:
            choice = unit
            arg = arg // scale_factor
            break

    # no choice was found among ('ns', 'us', 'ms', 's', 'm', 'h', 'D')
    if not choice:  # try irregular units ('M', 'Y')
        since = epoch_date(since)
        since_days = date_to_days(**since)

        # convert to days and pass through days_to_date
        min_ns = days_to_date(min_ns // as_ns["D"] + since_days)
        max_ns = days_to_date(max_ns // as_ns["D"] + since_days)

        min_ns["year"] -= since["year"]
        max_ns["year"] -= since["year"]
        min_ns["month"] -= since["month"]
        max_ns["month"] -= since["month"]

        # check for unit='M'
        if (12 * min_ns["year"] + min_ns["month"] > -2**63 and
            12 * max_ns["year"] + max_ns["month"] < 2**63):
            choice = "M"
            arg = days_to_date(arg // as_ns["D"] + since_days)
            arg["year"] -= since["year"]
            arg["month"] -= since["month"]
            arg = 12 * arg["year"] + arg["month"]
        else:  # must be representable as 'Y' (passed overflow sentinel)
            choice = "Y"
            arg = days_to_date(arg // as_ns["D"] + since_days)
            arg = arg["year"] - since["year"]

    # np.ndarray
    if issubclass(original_type, np.ndarray):
        return arg.astype(f"m8[{choice}]")

    # pd.Series
    if issubclass(original_type, pd.Series):
        index = arg.index
        arg = arg.to_numpy().astype(f"m8[{choice}]")
        return pd.Series(list(arg), index=index, dtype="O")

    # scalar
    return np.timedelta64(int(arg), choice)


def ns_to_timedelta(
    arg: int | np.ndarray | pd.Series,
    since: str | datetime_like = np.datetime64("2001-01-01")
) -> timedelta_like | np.ndarray | pd.Series:
    """TODO"""
    # ensure min/max fall within representable range
    if isinstance(arg, (np.ndarray, pd.Series)):
        min_ns = int(arg.min())
        max_ns = int(arg.max())
    else:
        min_ns = int(arg)
        max_ns = int(arg)

    # if `arg` is a numpy array, skip straight to np.timedelta64
    if not isinstance(arg, np.ndarray):  # try pd.Timedelta, datetime.timedelta
        try:  # pd.Timedelta
            return ns_to_pandas_timedelta(arg, min_ns=min_ns, max_ns=max_ns)
        except OverflowError:
            pass

        try:  # datetime.timedelta
            return ns_to_pytimedelta(arg, min_ns=min_ns, max_ns=max_ns)
        except OverflowError:
            pass

    # np.timedelta64
    return ns_to_numpy_timedelta64(
        arg,
        since=since,
        min_ns=min_ns,
        max_ns=max_ns
    )
