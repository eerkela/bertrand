import datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import check_dtype, get_dtype
from pdtypes.util.array import is_scalar
from pdtypes.util.type_hints import datetime_like, timedelta_like

from ..epoch import epoch
from ..unit import convert_unit
from ..unit cimport valid_units


# TODO: import valid_units from ..unit and add sentinels?


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
    *,
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
    *,
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
    unit: str = None,
    since: datetime_like = "2001-01-01 00:00:00+0000",
    rounding: str = "down",
    *,
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

    # kwargs for convert_unit
    unit_kwargs = {"since": epoch(since), "rounding": rounding}

    # if `unit` is already defined, rescale `arg` to match
    if unit is not None:
        min_ns = convert_unit(min_ns, "ns", unit, **unit_kwargs)
        max_ns = convert_unit(max_ns, "ns", unit, **unit_kwargs)

        # check for overflow
        if min_ns < -2**63 + 1 or max_ns > 2**63 - 1:
            raise OverflowError(f"`arg` exceeds np.timedelta64 range with "
                                f"unit={repr(unit)}")
        arg = convert_unit(arg, "ns", unit, **unit_kwargs)

    else:  # choose unit to fit range and rescale `arg` appropriately
        for test_unit in valid_units:
            if test_unit == "W":  # skip weeks
                # this unit has some really wierd (inconsistent) overflow
                # behavior that makes it practically useless over unit="D"
                continue

            # convert min/max to test unit
            test_min = convert_unit(min_ns, "ns", test_unit, **unit_kwargs)
            test_max = convert_unit(max_ns, "ns", test_unit, **unit_kwargs)

            # check for overflow
            if test_min >= -2**63 + 1 and test_max <= 2**63 - 1:
                unit = test_unit
                arg = convert_unit(arg, "ns", test_unit, **unit_kwargs)
                break

    # np.ndarray
    if isinstance(arg, np.ndarray):
        return arg.astype(f"m8[{unit}]")

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy().astype(f"m8[{unit}]")
        return pd.Series(list(arg), index=index, dtype="O")

    # scalar
    return np.timedelta64(int(arg), unit)


def ns_to_timedelta(
    arg: int | np.ndarray | pd.Series,
    since: str | datetime_like = "2001-01-01 00:00:00+0000"
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
