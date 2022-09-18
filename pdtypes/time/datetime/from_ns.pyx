import datetime
from cpython cimport datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.util.type_hints import datetime_like

from ..timezone import is_utc, timezone
from ..timezone cimport utc_timezones
from ..unit import convert_unit_integer
from ..unit cimport as_ns, valid_units


#########################
####    Constants    ####
#########################


cdef object utc_epoch = pd.Timestamp("1970-01-01 00:00:00+0000")


cdef object utc_naive_pydatetime = datetime.datetime.utcfromtimestamp(0)
cdef object utc_aware_pydatetime
utc_aware_pydatetime = datetime.datetime.fromtimestamp(0, datetime.timezone.utc)


# min/max representable ns values for each datetime type
cdef long int min_pandas_timestamp_ns = -2**63 + 1
cdef long int max_pandas_timestamp_ns = 2**63 - 1
cdef object min_pydatetime_ns = -62135596800000000000
cdef object max_pydatetime_ns = 253402300799999999000
cdef object min_numpy_datetime64_ns = -291061508645168391112243200000000000
cdef object max_numpy_datetime64_ns = 291061508645168328945024000000000000


#######################
####    Private    ####
#######################


cdef inline object ns_to_pydatetime_scalar(
    object ns,
    datetime.tzinfo tz
):
    """TODO"""
    cdef object offset = datetime.timedelta(microseconds=int(ns) // as_ns["us"])

    # naive
    if tz is None:
        return utc_naive_pydatetime + offset

    # aware (utc)
    if tz in utc_timezones:
        return utc_aware_pydatetime + offset

    # aware (non-utc)
    return (utc_aware_pydatetime + offset).astimezone(tz)


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] ns_to_pydatetime_vector(
    np.ndarray arr,
    datetime.tzinfo tz
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = ns_to_pydatetime_scalar(arr[i], tz=tz)

    return result


######################
####    Public    ####
######################


def ns_to_pandas_timestamp(
    arg: int | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo | None = None,
    min_ns: int = None,
    max_ns: int = None
) -> pd.Timestamp | np.ndarray | pd.Series:
    """TODO"""
    # ensure min/max fall within representable range
    is_array_like = isinstance(arg, (np.ndarray, pd.Series))
    if min_ns is None:
        min_ns = int(arg.min()) if is_array_like else int(arg)
    if max_ns is None:
        max_ns = int(arg.max()) if is_array_like else int(arg)
    if min_ns < min_pandas_timestamp_ns or max_ns > max_pandas_timestamp_ns:
        raise OverflowError(f"`arg` exceeds pd.Timestamp range")

    # resolve timezone
    tz = timezone(tz)

    # convert using pd.to_datetime, accounting for timezone
    original_type = type(arg)
    if tz is None:
        arg = pd.to_datetime(arg, unit="ns")
    else:
        arg = pd.to_datetime(arg, unit="ns", utc=True)
        if not is_utc(tz):
            # localization can cause overflow if close to min/max timestamp
            try:
                if issubclass(original_type, pd.Series):
                    arg = arg.dt.tz_convert(tz)  # use `.dt` namespace
                else:
                    arg = arg.tz_convert(tz)
            except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime as err:
                err_msg = (f"localizing to {tz} causes `arg` to exceed "
                           f"pd.Timestamp range")
                raise OverflowError(err_msg) from err

    # convert array inputs back to array form (rather than DatetimeIndex)
    if issubclass(original_type, np.ndarray):
        return arg.to_numpy(dtype="O")

    return arg


def ns_to_pydatetime(
    arg: int | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo | None = None,
    min_ns: int = None,
    max_ns: int = None
) -> datetime.datetime | np.ndarray | pd.Series:
    """TODO"""
    # ensure min/max fall within representable range
    is_array_like = isinstance(arg, (np.ndarray, pd.Series))
    if min_ns is None:
        min_ns = int(arg.min()) if is_array_like else int(arg)
    if max_ns is None:
        max_ns = int(arg.max()) if is_array_like else int(arg)
    if min_ns < min_pydatetime_ns or max_ns > max_pydatetime_ns:
        raise OverflowError(f"`arg` exceeds datetime.datetime range")

    # resolve timezone
    tz = timezone(tz)

    # localization can cause overflow if close to min/max datetime
    overflow_msg = (f"localizing to {tz} causes `arg` to exceed "
                    f"datetime.datetime range")

    # np.ndarray
    if isinstance(arg, np.ndarray):
        if tz is None:  # fastpath for naive utc datetimes
            arg = arg // as_ns["us"]
            return arg.astype("M8[us]").astype("O")
        try:
            return ns_to_pydatetime_vector(arg, tz)
        except OverflowError as err:
            raise OverflowError(overflow_msg) from err

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy()
        if tz is None:  # fastpath for naive utc datetimes
            arg = arg // as_ns["us"]
            arg = arg.astype("M8[us]").astype("O")
        else:
            try:
                arg = ns_to_pydatetime_vector(arg, tz)
            except OverflowError as err:
                raise OverflowError(overflow_msg) from err
        return pd.Series(arg, index=index, copy=False, dtype="O")

    # scalar
    try:
        return ns_to_pydatetime_scalar(arg, tz)
    except OverflowError as err:
        raise OverflowError(overflow_msg) from err


def ns_to_numpy_datetime64(
    arg: int | np.ndarray | pd.Series,
    unit: str = None,
    rounding: str = "down",
    *,
    min_ns: int = None,
    max_ns: int = None
) -> np.datetime64 | np.ndarray | pd.Series:
    """TODO"""
    # ensure min/max fall within representable range
    is_array_like = isinstance(arg, (np.ndarray, pd.Series))
    if min_ns is None:
        min_ns = int(arg.min()) if is_array_like else int(arg)
    if max_ns is None:
        max_ns = int(arg.max()) if is_array_like else int(arg)
    if min_ns < min_numpy_datetime64_ns or max_ns > max_numpy_datetime64_ns:
        raise OverflowError(f"`arg` exceeds np.datetime64 range")

    # ensure unit is valid
    if unit not in valid_units:
        raise ValueError(f"`unit` must be one of {valid_units}, not "
                         f"{repr(unit)}")

    # kwargs for convert_unit_integer
    unit_kwargs = {"since": utc_epoch, "rounding": rounding}

    # if `unit` is already defined, rescale `arg` to match
    if unit is not None:
        min_ns = convert_unit_integer(min_ns, "ns", unit, **unit_kwargs)
        max_ns = convert_unit_integer(max_ns, "ns", unit, **unit_kwargs)

        # get allowable limits for given unit
        lower_lim = -2**63 + 1
        upper_lim = 2**63 - 1
        if unit == "W":
            lower_lim //= 7  # wierd overflow behavior related to weeks
            upper_lim //= 7
        elif unit == "Y":
            upper_lim -= 1970  # appears to be utc offset

        # check for overflow
        if min_ns < lower_lim or max_ns > upper_lim:
            raise OverflowError(f"`arg` exceeds np.datetime64 range with "
                                f"unit={repr(unit)}")
        arg = convert_unit_integer(arg, "ns", unit, **unit_kwargs)

    else:  # choose unit to fit range and rescale `arg` appropriately
        for test_unit in valid_units:
            if test_unit == "W":  # skip weeks
                # this unit has some really wierd (inconsistent) overflow
                # behavior that makes it practically useless over unit="D"
                continue

            # convert min/max to test unit
            test_min = convert_unit_integer(
                min_ns,
                "ns",
                test_unit,
                **unit_kwargs
            )
            test_max = convert_unit_integer(
                max_ns,
                "ns",
                test_unit,
                **unit_kwargs
            )

            # get allowable limits for test unit
            lower_lim = -2**63 + 1
            upper_lim = 2**63 - 1
            if unit == "W":
                lower_lim //= 7  # wierd overflow behavior related to weeks
                upper_lim //= 7
            elif unit == "Y":
                upper_lim -= 1970  # appears to be utc offset

            # check for overflow
            if test_min >= lower_lim and test_max <= upper_lim:
                unit = test_unit
                arg = convert_unit_integer(arg, "ns", test_unit, **unit_kwargs)
                break

    # np.ndarray
    if isinstance(arg, np.ndarray):
        return arg.astype(f"M8[{unit}]")

    # pd.Series
    if isinstance(arg, pd.Series):
        index = arg.index
        arg = arg.to_numpy().astype(f"M8[{unit}]")
        return pd.Series(list(arg), index=index, dtype="O")

    # scalar
    return np.datetime64(int(arg), unit)


def ns_to_datetime(
    arg: int | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo | None = None
) -> datetime_like | np.ndarray | pd.Series:
    """TODO"""
    # ensure min/max fall within representable range
    if isinstance(arg, (np.ndarray, pd.Series)):
        min_ns = int(arg.min())
        max_ns = int(arg.max())
    else:
        min_ns = int(arg)
        max_ns = int(arg)

    # resolve timezone and check if utc
    tz = timezone(tz)

    # if `arg` is a numpy array and `tz` is utc, skip straight to np.datetime64
    if isinstance(arg, np.ndarray) and (not tz or is_utc(tz)):
        return ns_to_numpy_datetime64(arg, min_ns=min_ns, max_ns=max_ns)

    try:  # pd.Timestamp
        return ns_to_pandas_timestamp(arg, tz, min_ns=min_ns, max_ns=max_ns)
    except OverflowError:
        pass

    try:  # datetime.datetime
        return ns_to_pydatetime(arg, tz, min_ns=min_ns, max_ns=max_ns)
    except OverflowError:
        pass

    # np.datetime64
    if tz and not is_utc(tz):
        err_msg = ("`numpy.datetime64` objects do not carry timezone "
                   "information (must be utc)")
        raise RuntimeError(err_msg)
    return ns_to_numpy_datetime64(arg)
