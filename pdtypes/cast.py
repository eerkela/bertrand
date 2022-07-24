from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes.error import error_trace
from pdtypes.check import check_dtype, extension_type, get_dtype
from pdtypes.util.array import round_div, vectorize, broadcast_args
from pdtypes.util.type_hints import (
    array_like, dtype_like, datetime_like, timedelta_like
)

# TODO: add x_to_bytes support as well.  If I can vectorize this, then
# implementing downcast will be much easier.
# -> see np.frombuffer, np.packbits, and np.unpackbits
# -> you can `np.unpackbits(np.frombuffer(array.tobytes(), dtype=np.uint8))`
# -> ^ .reshape(len(array), 8 * dtype.itemsize)
# https://stackoverflow.com/questions/69560201/python-numpy-extracting-bits-of-bytes


# TODO: rename .coerce_dtypes() to .transmute()?


# TODO: when dispatching, use sparse and categorical boolean flags.  If the
# provided dtype is SparseDtype(), set sparse=True and convert to base type.


# TODO: implement a validate keyword, which controls argument validation.  This
# defaults to True, but can be set False when one conversion function calls
# another, giving significant performance increases on object arrays.


#############################################
####    `series` Validation Functions    ####
#############################################


def _validate_boolean_series(series: bool | array_like) -> None:
    """Raise a TypeError if `series` does not contain boolean data."""
    if not check_dtype(series, bool):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"boolean data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_integer_series(series: int | array_like) -> None:
    """Raise a TypeError if `series` does not contain integer data."""
    if not check_dtype(series, int):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"integer data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_float_series(series: float | array_like) -> None:
    """Raise a TypeError if `series` does not contain float data."""
    if not check_dtype(series, float):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"float data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_complex_series(series: complex | array_like) -> None:
    """Raise a TypeError if `series` does not contain complex data."""
    if not check_dtype(series, complex):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"complex data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_decimal_series(series: decimal.Decimal | array_like) -> None:
    """Raise a TypeError if `series` does not contain decimal data."""
    if not check_dtype(series, "decimal"):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"decimal data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_datetime_series(series: datetime_like | array_like) -> None:
    """Raise a TypeError if `series` does not contain datetime data."""
    if not check_dtype(series, "datetime"):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"datetime data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_timedelta_series(series: timedelta_like | array_like) -> None:
    """Raise a TypeError if `series` does not contain timedelta data."""
    if not check_dtype(series, "timedelta"):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"timedelta data, not {get_dtype(series)}")
        raise TypeError(err_msg)


def _validate_string_series(series: str | array_like) -> None:
    if not check_dtype(series, str):
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"string data, not {get_dtype(series)}")
        raise TypeError(err_msg)


############################################
####    `dtype` Validation Functions    ####
############################################


def _validate_dtype_is_scalar(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is a sequence of any length."""
    if np.array(dtype).shape:
        err_msg = (f"[{error_trace()}] `dtype` must be a single atomic type, "
                   f"not a sequence {repr(dtype)}")
        raise TypeError(err_msg)


def _validate_boolean_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid boolean dtype."""
    if not check_dtype(dtype, bool):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_integer_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid integer dtype."""
    if not check_dtype(dtype, int):
        err_msg = (f"[{error_trace()}] `dtype` must be int-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_float_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid float dtype."""
    if not check_dtype(dtype, float):
        err_msg = (f"[{error_trace()}] `dtype` must be float-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_complex_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid complex dtype."""
    if not check_dtype(dtype, complex):
        err_msg = (f"[{error_trace()}] `dtype` must be complex-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_decimal_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid decimal dtype."""
    if not check_dtype(dtype, "decimal"):
        err_msg = (f"[{error_trace()}] `dtype` must be decimal-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_datetime_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid datetime dtype."""
    if not check_dtype(dtype, "datetime"):
        err_msg = (f"[{error_trace()}] `dtype` must be datetime-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_timedelta_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid timedelta dtype."""
    if not check_dtype(dtype, "timedelta"):
        err_msg = (f"[{error_trace()}] `dtype` must be timedelta-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


def _validate_string_dtype(dtype: dtype_like) -> None:
    """Raise a TypeError if `dtype` is not a valid string dtype."""
    if not check_dtype(dtype, str):
        err_msg = (f"[{error_trace()}] `dtype` must be string-like, not "
                   f"{repr(dtype)}")
        raise TypeError(err_msg)


##################################################
####    Miscellaneous Validation Functions    ####
##################################################


# def _validate_unit(unit: str) -> str:


# def _validate_timezone(tz: str | datetime.tzinfo) -> datetime.tzinfo:


def _validate_tolerance(tol: float) -> None:
    """Raise a TypeError if `tol` isn't a float, and a ValueError if it is not
    >= 0.
    """
    if not isinstance(tol, float):
        err_msg = (f"[{error_trace(stack_index=2)}] `tol` must be a float "
                   f"between 0 and 1, not {type(tol)}")
        raise TypeError(err_msg)
    if not tol >= 0.0:
        err_msg = (f"[{error_trace(stack_index=2)}] `tol` must be a float "
                   f"between 0 and 1, not {tol}")
        raise ValueError(err_msg)


def _validate_rounding(rounding: str | None) -> None:
    """Raise a TypeError if `tol` isn't None or a string, and a ValueError if
    it is not one of the accepted rounding rules ('floor', 'ceiling',
    'truncate', 'infinity', 'nearest')
    """
    if rounding is None:
        return None
    valid_rules = ("floor", "ceiling", "truncate", "infinity", "nearest")
    if not isinstance(rounding, str):
        err_msg = (f"[{error_trace(stack_index=2)}] `rounding` must be a "
                   f"string {valid_rules}, not {type(rounding)}")
        raise TypeError(err_msg)
    if rounding not in valid_rules:
        err_msg = (f"[{error_trace(stack_index=2)}] `rounding` must be one of "
                   f"{valid_rules}, not {repr(rounding)}")
        raise ValueError(err_msg)
    return None


def _validate_errors(errors: str) -> None:
    """Raise  a TypeError if `errors` isn't a string, and a ValueError if it is
    not one of the accepted error-handling rules ('raise', 'coerce', 'ignore')
    """
    valid_errors = ("raise", "coerce", "ignore")
    if not isinstance(errors, str):
        err_msg = (f"[{error_trace(stack_index=2)}] `errors` must be a string "
                   f"{valid_errors}, not {type(errors)}")
        raise TypeError(err_msg)
    if errors not in valid_errors:
        err_msg = (f"[{error_trace(stack_index=2)}] `errors` must be one of "
                   f"{valid_errors}, not {repr(errors)}")
        raise ValueError(err_msg)


#################################
####    Utility Functions    ####
#################################


def _shorten_indices(indices: array_like, max_length: int = 5) -> str:
    if len(indices) <= max_length:
        return str(list(indices))
    shortened = ", ".join(str(i) for i in indices[:max_length])
    return f"[{shortened}, ...] ({len(indices)})"


#######################
####    Boolean    ####
#######################


def boolean_to_boolean(
    series: bool | array_like,
    dtype: dtype_like = bool
) -> pd.Series:
    """Convert a boolean series to another boolean dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_boolean_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_boolean_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)


def boolean_to_integer(
    series: bool | array_like,
    dtype: dtype_like = int,
    downcast: bool = False
) -> pd.Series:
    """Convert boolean series to integer"""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_boolean_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_integer_dtype(dtype)

    # the conversion is actually trivial - we only need integer_to_integer to
    # sort out `dtype` and `downcast` arguments
    result = series + 0  # automatically converts
    return integer_to_integer(result, dtype=dtype, downcast=downcast)


def boolean_to_float(
    series: bool | array_like,
    dtype: dtype_like = float,
    downcast: bool = False
) -> pd.Series:
    """Convert a boolean series to a float series."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_boolean_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_float_dtype(dtype)

    # the conversion is actually trivial - we only need float_to_float to
    # sort out `dtype` and `downcast` arguments
    result = series + 0.0  # automatically converts
    return float_to_float(result, dtype=dtype, downcast=downcast)


def boolean_to_complex(
    series: bool | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False
) -> pd.Series:
    """Convert a boolean series to a complex series."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_boolean_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_complex_dtype(dtype)

    # the conversion is actually trivial - we only need complex_to_complex to
    # sort out `dtype` and `downcast` arguments
    result = series + complex(0)  # automatically converts
    return complex_to_complex(result, dtype=dtype, downcast=downcast)


def boolean_to_decimal(
    series: bool | array_like,
    dtype: dtype_like = decimal.Decimal
) -> pd.Series:
    """Convert a boolean series to a decimal series."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_boolean_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_decimal_dtype(dtype)

    # the conversion is actually trivial - adding a decimal auto-converts
    return (series + decimal.Decimal(0)).fillna(pd.NA)


# def boolean_to_datetime(
#     series: bool | array_like,
#     unit: str | array_like = "ns",
#     since: str | datetime_like | array_like = "1970-01-01 00:00:00+0000",
#     tz: str | datetime.tzinfo = "local",
#     dtype: dtype_like = "datetime"
# ) -> pd.Series:
#     """Convert a boolean series to datetimes, where True represents one `unit`
#     after `since`.  This is mostly provided for completeness and reversability
#     reasons.
#     """
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_boolean_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_datetime_dtype(dtype)

#     raise NotImplementedError()


# def boolean_to_timedelta(
#     series: bool | array_like,
#     unit: str = "ns",
#     since: str | timedelta_like = "00:00:00",
#     dtype: dtype_like = "timedelta"
# ) -> pd.Series:
#     """Convert a boolean series to timedeltas, where True represents one `unit`
#     after `since`.  This is mostly provided for completeness and reversability
#     reasons.
#     """
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_boolean_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_timedelta_dtype(dtype)

#     raise NotImplementedError()


def boolean_to_string(
    series: bool | array_like,
    dtype: dtype_like = pd.StringDtype()
) -> pd.Series:
    """Convert a boolean series into strings `('True', 'False')`.  This is
    mostly provided for completeness and reversability reasons.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_boolean_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_string_dtype(dtype)

    # use pandas string extension type, if applicable
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


#######################
####    Integer    ####
#######################


def integer_to_boolean(
    series: int | array_like,
    dtype: dtype_like = bool,
    errors: str = "raise"
) -> pd.Series:
    """Convert an integer series to their boolean equivalents."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_integer_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_boolean_dtype(dtype)
    _validate_errors(errors)

    # check for information loss and apply error handling rule
    if errors == "raise" and (series.min() < 0 or series.max() > 1):
        bad = series[(series < 0) | (series > 1)].index.values
        err_msg = (f"[{error_trace()}] non-boolean value encountered at index "
                   f"{_shorten_indices(bad)}")
        raise ValueError(err_msg)
    if errors == "ignore" and (series.min() < 0 or series.max() > 1):
        return series
    hasnans = series.hasnans
    if errors == "coerce":
        if hasnans and pd.api.types.is_object_dtype(series):
            series = series.fillna(pd.NA)  # abs() doesn't work on NoneType
        series = series.abs().clip(0, 1)  # coerce to [0, 1, pd.NA/np.nan]

    # convert to final result
    if hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)


def integer_to_integer(
    series: int | array_like,
    dtype: dtype_like = int,
    downcast: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert an integer series to another integer dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_integer_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_integer_dtype(dtype)
    _validate_errors(errors)

    # get min/max to evaluate range
    min_val = series.min()
    max_val = series.max()

    # built-in integer special case - can be arbitrarily large
    if ((min_val < -2**63 or max_val > 2**63 - 1) and
        check_dtype(dtype, int, exact=True)):
        if min_val >= 0 and max_val <= 2**64 - 1:  # > int64 but < uint64
            if series.hasnans:
                return series.astype(pd.UInt64Dtype())
            return series.astype(np.uint64)
        # >int64 and >uint64, return as built-in python ints
        return series.astype(object).fillna(pd.NA)

    # convert to pandas dtype to expose itemsize attribute
    dtype = pd.api.types.pandas_dtype(dtype)
    if check_dtype(dtype, "u"):  # unsigned integer
        min_poss = 0
        max_poss = 2 ** (8 * dtype.itemsize) - 1
    else:
        min_poss = -2**(8 * dtype.itemsize - 1)
        max_poss = 2**(8 * dtype.itemsize - 1) - 1

    # check whether result fits within specified dtype
    if errors == "raise" and (min_val < min_poss or max_val > max_poss):
        bad = series[(series < min_poss) | (series > max_poss)].index.values
        err_msg = (f"[{error_trace()}] values exceed {dtype} range at index "
                   f"{_shorten_indices(bad)}")
        raise OverflowError(err_msg)
    if errors == "ignore" and (min_val < min_poss or max_val > max_poss):
        return series
    if errors == "coerce":
        bad_indices = (series < min_poss) | (series > max_poss)
        if bad_indices.any():
            series[bad_indices] = pd.NA
            min_val = series.min()
            max_val = series.max()

    # attempt to downcast if applicable
    if downcast:
        dtype = downcast_int_dtype(min_val, max_val, dtype)

    # convert and return
    if series.hasnans and not pd.api.types.is_extension_array_dtype(dtype):
        dtype = extension_type(dtype)
    return series.astype(dtype)


def integer_to_float(
    series: int | array_like,
    dtype: dtype_like = float,
    downcast: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert an integer series into floats of the given dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_integer_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_float_dtype(dtype)
    _validate_errors(errors)

    # do conversion
    result = series.astype(dtype)

    # check for overflow
    if errors == "raise" and (result - series).any():
        bad = result[(result - series) != 0].index.values
        err_msg = (f"[{error_trace()}] precision loss detected at index "
                   f"{_shorten_indices(bad)}")
        raise OverflowError(err_msg)
    if errors == "ignore" and (result - series).any():
        return series
    if errors == "coerce":
        result[result.isin((-np.inf, np.inf))] = np.nan

    # return
    if downcast:
        return result.apply(downcast_float)
    return result


def integer_to_complex(
    series: int | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert an integer series into complex numbers of the given dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_integer_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_complex_dtype(dtype)
    _validate_errors(errors)

    # do conversion
    result = series.astype(dtype)

    # check for overflow
    if errors == "raise" and (result - series).any():
        bad = result[(result - series) != 0].index.values
        err_msg = (f"[{error_trace()}] precision loss detected at index "
                   f"{_shorten_indices(bad)}")
        raise OverflowError(err_msg)
    if errors == "ignore" and (result - series).any():
        return series
    if errors == "coerce":
        result[result.isin((-np.inf, np.inf))] = complex(np.nan, np.nan)

    # return
    if downcast:
        return result.apply(downcast_complex)
    return result


def integer_to_decimal(
    series: int | array_like,
    dtype: dtype_like = decimal.Decimal
) -> pd.Series:
    """Convert an integer series into decimal."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_integer_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_decimal_dtype(dtype)

    # special case: decimal.Decimal can't parse numpy integers that are stored
    # in an object array
    if (pd.api.types.is_object_dtype(series) and
        not check_dtype(series, int, exact=True)):
        # handle these by casting to a string transfer format, then to decimal
        series = integer_to_string(series).astype("O")

    # initialize result
    result = pd.Series(np.full(series.shape, pd.NA, dtype="O"))

    # convert using generic numpy ufunc - marginally faster than series.apply
    # and about 6x faster than list comprehension
    not_na = series.notna()
    result[not_na] = np.frompyfunc(decimal.Decimal, 1, 1)(series[not_na])
    return result


# def integer_to_datetime(
#     series: int | array_like,
#     unit: str | array_like = "ns",
#     since: str | datetime_like | array_like = "1970-01-01 00:00:00+0000",
#     tz: str | datetime.tzinfo = "local",
#     dtype: dtype_like = "datetime"
# ) -> pd.Series:
#     """Convert an integer series into datetime."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_integer_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_datetime_dtype(dtype)

#     raise NotImplementedError()


# def integer_to_timedelta(
#     series: int | array_like,
#     unit: str | array_like = "ns",
#     since: str | timedelta_like | array_like = "00:00:00",
#     dtype: dtype_like = "timedelta"
# ) -> pd.Series:
#     """Convert an integer series to timedelta."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_integer_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_timedelta_dtype(dtype)

#     raise NotImplementedError()


def integer_to_string(
    series: int | array_like,
    dtype: dtype_like = pd.StringDtype()
) -> pd.Series:
    """Convert an integer series to strings."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_integer_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_string_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


#####################
####    Float    ####
#####################


def float_to_boolean(
    series: float | array_like,
    tol: float = 1e-6,
    rounding: str | None = None,
    dtype: dtype_like = bool,
    errors: str = "raise"
) -> pd.Series:
    """Convert a float series to booleans, using the specified rounding rule
    and floating point tolerance.
    """
    # TODO: tolerance should only be defined wrt 0 and 1.  Enable values > 1

    # vectorize input
    series = pd.Series(vectorize(series))
    if isinstance(tol, int):
        tol = float(tol)

    # validate input
    _validate_float_series(series)
    _validate_tolerance(tol)
    _validate_rounding(rounding)
    _validate_dtype_is_scalar(dtype)
    _validate_boolean_dtype(dtype)
    _validate_errors(errors)

    # python floats have no callable rint method, but are identical to float64
    if pd.api.types.is_object_dtype(series):
        result = series.astype(np.float64)
    else:
        result = series.copy()

    # round if applicable
    if tol:  # round if within tolerance
        within_tol = (result - result.round()).abs() <= tol
        result[within_tol] = result[within_tol].round()
    if rounding:  # apply specified rounding rule
        switch = {  # C-style switch statement
            "floor": lambda: np.floor(result),
            "ceiling": lambda: np.ceil(result),
            "nearest": lambda: result.round(),
            "truncate": lambda: np.sign(result) * np.floor(np.abs(result)),
            "infinity": lambda: np.sign(result) * np.ceil(np.abs(result))
        }
        result = switch[rounding]()

    # coerce if applicable
    if errors == "raise" and not result.dropna().isin((0, 1)).all():
        bad = result[(~result.isin((0, 1))) ^ result.isna()].index.values
        err_msg = (f"[{error_trace()}] non-boolean value encountered at index "
                   f"{_shorten_indices(bad)}")
        raise ValueError(err_msg)
    if errors == "ignore" and not result.dropna().isin((0, 1)).all():
        return series
    if errors == "coerce":
        result = np.ceil(result.abs().clip(0, 1))  # coerce to [0, 1, np.nan]

    # return
    if result.hasnans:
        return result.astype(pd.BooleanDtype())
    return result.astype(dtype)


def float_to_integer(
    series: float | array_like,
    tol: float = 1e-6,
    rounding: str | None = None,
    dtype: dtype_like = int,
    downcast: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert a float series to integers, using the specified rounding rule
    and floating point tolerance.
    """
    # vectorize input
    series = pd.Series(vectorize(series))
    if isinstance(tol, int):
        tol = float(tol)

    # validate input
    _validate_float_series(series)
    _validate_tolerance(tol)
    _validate_rounding(rounding)
    _validate_dtype_is_scalar(dtype)
    _validate_integer_dtype(dtype)
    _validate_errors(errors)

    # python floats have no callable rint method, but are identical to float64
    if pd.api.types.is_object_dtype(series):
        result = series.astype(np.float64)
    else:
        result = series.copy()

    # apply rounding, if applicable
    if tol:  # round if within tolerance
        within_tol = (result - result.round()).abs() <= tol
        result[within_tol] = result[within_tol].round()
    if rounding:  # apply specified rounding rule
        switch = {  # C-style switch statement
            "floor": lambda: np.floor(result),
            "ceiling": lambda: np.ceil(result),
            "nearest": lambda: result.round(),
            "truncate": lambda: np.sign(result) * np.floor(np.abs(result)),
            "infinity": lambda: np.sign(result) * np.ceil(np.abs(result))
        }
        result = switch[rounding]()

    # check for precision loss and apply error handling rule
    if errors == "raise" and (result - result.round()).any():
        bad = result[(result - result.round()) != 0].index.values
        err_msg = (f"[{error_trace()}] non-integer value encountered at index "
                   f"{_shorten_indices(bad)}")
        raise ValueError(err_msg)
    if errors == "ignore" and (result - result.round()).any():
        return series
    if errors == "coerce":  # truncate, just like int(float)
        result = np.sign(result) * np.floor(np.abs(result))

    # get min/max to evaluate range - longdouble maintains integer precision
    # for entire 64-bit range, prevents inconsistent comparison
    min_val = np.longdouble(result.min())
    max_val = np.longdouble(result.max())

    # built-in integer special case - can be arbitrarily large
    if ((min_val < -2**63 or max_val > 2**63 - 1) and
        check_dtype(dtype, int, exact=True)):
        # these special cases are unaffected by downcast
        # longdouble can't be compared to extended python integer (> 2**63 - 1)
        if min_val >= 0 and max_val < np.uint64(2**64 - 1):  # > i8 but < u8
            if result.hasnans:
                return result.astype(pd.UInt64Dtype())
            return result.astype(np.uint64)
        nans = result.isna()
        result = result.astype("O")
        result[nans] = pd.NA
        result[~nans] = np.frompyfunc(int, 1, 1)(result[~nans])
        return result

    # convert to pandas dtype to expose itemsize attribute
    dtype = pd.api.types.pandas_dtype(dtype)
    if check_dtype(dtype, "u"):  # unsigned integer
        min_poss = 0
        max_poss = 2 ** (8 * dtype.itemsize) - 1
    else:
        min_poss = -2**(8 * dtype.itemsize - 1)
        max_poss = 2**(8 * dtype.itemsize - 1) - 1

    # check whether result fits within specified dtype
    if errors == "raise" and (min_val < min_poss or max_val > max_poss):
        bad = result[(result < min_poss) | (result > max_poss)].index.values
        err_msg = (f"[{error_trace()}] values exceed {dtype} range at index "
                   f"{_shorten_indices(bad)}")
        raise OverflowError(err_msg)
    if errors == "ignore" and (min_val < min_poss or max_val > max_poss):
        return series
    if errors == "coerce":
        bad_indices = (result < min_poss) | (result > max_poss)
        if bad_indices.any():
            result[bad_indices] = np.nan
            min_val = result.min()
            max_val = result.max()

    # attempt to downcast if applicable
    if downcast:
        dtype = downcast_int_dtype(min_val, max_val, dtype)

    # convert and return
    if result.hasnans and not pd.api.types.is_extension_array_dtype(dtype):
        dtype = extension_type(dtype)
    return result.astype(dtype)


def float_to_float(
    series: float | array_like,
    dtype: dtype_like = float,
    downcast: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert a float series into another float dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_float_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_float_dtype(dtype)
    _validate_errors(errors)

    # do conversion
    if check_dtype(dtype, float, exact=True):  # preserve precision
        result = series
    else:  # convert and check for precision loss
        result = series.astype(dtype)
        if errors == "raise" and (result - series).any():
            bad = result[(result - series) != 0].index.values
            err_msg = (f"[{error_trace()}] precision loss detected at index "
                    f"{_shorten_indices(bad)}")
            raise OverflowError(err_msg)
        if errors == "ignore" and (result - series).any():
            return series
        if errors == "coerce":
            result[result.isin((-np.inf, np.inf))] = np.nan

    # return
    if downcast:
        return result.apply(downcast_float)
    return result


def float_to_complex(
    series: float | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert a float series into complex numbers."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_float_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_complex_dtype(dtype)
    _validate_errors(errors)

    # do conversion
    result = series.astype(dtype)

    # check for precision loss
    if errors == "raise" and (result - series).any():
        bad = result[(result - series) != 0].index.values
        err_msg = (f"[{error_trace()}] precision loss detected at index "
                   f"{_shorten_indices(bad)}")
        raise OverflowError(err_msg)
    if errors == "ignore" and (result - series).any():
        return series
    if errors == "coerce":
        result[result.isin((-np.inf, np.inf))] = complex(np.nan, np.nan)

    # return
    if downcast:
        return result.apply(downcast_complex)
    return result


def float_to_decimal(
    series: float | array_like,
    dtype: dtype_like = decimal.Decimal
) -> pd.Series:
    """Convert a float series into decimals."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_float_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_decimal_dtype(dtype)

    # special case: decimal.Decimal can't parse numpy floats that are stored
    # in an object array
    if (pd.api.types.is_object_dtype(series) and
        not check_dtype(series, float, exact=True)):
        # handle these by casting to a string transfer format, then to decimal
        series = float_to_string(series).astype("O")

    # initialize result
    result = pd.Series(np.full(series.shape, pd.NA, dtype="O"))

    # convert using generic numpy ufunc - marginally faster than series.apply
    # and about 6x faster than list comprehension
    not_na = series.notna()
    result[not_na] = np.frompyfunc(decimal.Decimal, 1, 1)(series[not_na])
    return result


# def float_to_datetime(
#     series: float | array_like,
#     unit: str | array_like = "ns",
#     since: str | datetime_like | array_like = "1970-01-01 00:00:00+0000",
#     tz: str | datetime.tzinfo = "local",
#     dtype: dtype_like = "datetime"
# ) -> pd.Series:
#     """Convert a float series to datetimes."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_float_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_boolean_dtype(dtype)

#     raise NotImplementedError()


# def float_to_timedelta(
#     series: float | array_like,
#     unit: str | array_like = "ns",
#     since: str | timedelta_like | array_like = "00:00:00",
#     dtype: dtype_like = "timedelta"
# ) -> pd.Series:
#     """Convert a float series to timedeltas."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_float_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_timedelta_dtype(dtype)

#     raise NotImplementedError()


def float_to_string(
    series: float | array_like,
    dtype: dtype_like = pd.StringDtype()
) -> pd.Series:
    """Convert a float series to string."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_float_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_string_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


#######################
####    Complex    ####
#######################


def complex_to_boolean(
    series: complex | array_like,
    tol: complex = 1e-6,
    rounding: str | None = None,
    dtype: dtype_like = bool,
    errors: str = "raise"
) -> pd.Series:
    """Convert a complex series to booleans, using the specified rounding rule
    and floating point tolerance.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_complex_series(series)
    _validate_tolerance(tol)
    _validate_rounding(rounding)
    _validate_dtype_is_scalar(dtype)
    _validate_boolean_dtype(dtype)
    _validate_errors(errors)

    # 2 steps: complex -> float, then float -> boolean
    series = complex_to_float(series, tol=tol, errors=errors)
    return float_to_boolean(series, tol=tol, rounding=rounding, dtype=dtype,
                            errors=errors)


def complex_to_integer(
    series: complex | array_like,
    tol: complex = 1e-6,
    rounding: str | None = None,
    dtype: dtype_like = int,
    downcast: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert a complex series to integers, using the specified rounding rule
    and floating point tolerance.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_complex_series(series)
    _validate_rounding(rounding)
    _validate_tolerance(tol)
    _validate_dtype_is_scalar(dtype)
    _validate_integer_dtype(dtype)
    _validate_errors(errors)

    # 2 steps: complex -> float, then float -> integer
    series = complex_to_float(series, tol=tol, errors=errors)
    return float_to_integer(series, tol=tol, rounding=rounding, dtype=dtype,
                            downcast=downcast, errors=errors)


def complex_to_float(
    series: complex | array_like,
    tol: float = 1e-6,
    dtype: dtype_like = float,
    downcast: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert a complex series into floats."""
    # vectorize input
    series = pd.Series(vectorize(series))
    if isinstance(tol, int):
        tol = float(tol)

    # validate input
    _validate_complex_series(series)
    _validate_tolerance(tol)
    _validate_dtype_is_scalar(dtype)
    _validate_float_dtype(dtype)
    _validate_errors(errors)

    # np.real & np.imag aren't stable for object-typed arrays
    if pd.api.types.is_object_dtype(series):
        # find the largest dtype present in array
        largest = np.max([np.dtype(x) for x in vectorize(get_dtype(series))])
        result = series.astype(largest)
    else:
        result = series.copy()

    # split result into real and imaginary components
    real = pd.Series(np.real(result))
    imag = np.imag(result)

    # check `imag` for information loss
    if errors == "raise" and (np.abs(imag) > tol).any():
        bad = result[np.abs(imag) > tol].index.values
        err_msg = (f"[{error_trace()}] imaginary component exceeds tolerance "
                   f"({tol}) at index {_shorten_indices(bad)}")
        raise ValueError(err_msg)
    if errors == "ignore" and (np.abs(imag) > tol).any():
        return series

    # return using float_to_float
    return float_to_float(real, dtype=dtype, downcast=downcast, errors=errors)


def complex_to_complex(
    series: complex | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert a complex series into another complex dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_complex_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_complex_dtype(dtype)
    _validate_errors(errors)

    # do conversion
    if check_dtype(dtype, complex, exact=True):  # preserve precision
        result = series
    else:  # convert and check for precision loss
        result = series.astype(dtype)
        if errors == "raise" and (result - series).any():
            bad = result[(result - series) != 0].index.values
            err_msg = (f"[{error_trace()}] precision loss detected at index "
                    f"{_shorten_indices(bad)}")
            raise OverflowError(err_msg)
        if errors == "ignore" and (result - series).any():
            return series
        if errors == "coerce":
            result[result.isin((-np.inf, np.inf))] = np.nan

    # return
    if downcast:
        return result.apply(downcast_complex)
    return result


def complex_to_decimal(
    series: complex | array_like,
    tol: float = 1e-6,
    dtype: dtype_like = decimal.Decimal,
    errors: str = "raise"
) -> pd.Series:
    """Convert a complex series into decimals."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_complex_series(series)
    _validate_tolerance(tol)
    _validate_dtype_is_scalar(dtype)
    _validate_decimal_dtype(dtype)
    _validate_errors(errors)

    # 2 steps: complex -> float, then float -> decimal
    series = complex_to_float(series, tol=tol, errors=errors)
    return float_to_decimal(series, dtype=dtype)


# def complex_to_datetime(
#     series: complex | array_like,
#     unit: str | array_like = "ns",
#     since: str | datetime_like | array_like = "1970-01-01 00:00:00+0000",
#     tz: str | datetime.tzinfo = "local",
#     dtype: dtype_like = "datetime"
# ) -> pd.Series:
#     """Convert a complex series to datetimes."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_complex_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_boolean_dtype(dtype)

#     raise NotImplementedError()


# def complex_to_timedelta(
#     series: complex | array_like,
#     unit: str | array_like = "ns",
#     since: str | timedelta_like | array_like = "00:00:00",
#     dtype: dtype_like = "timedelta"
# ) -> pd.Series:
#     """Convert a complex series to timedeltas."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_complex_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_timedelta_dtype(dtype)

#     raise NotImplementedError()


def complex_to_string(
    series: complex | array_like,
    dtype: dtype_like = pd.StringDtype()
) -> pd.Series:
    """Convert a complex series to string."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_complex_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_string_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


#######################
####    Decimal    ####
#######################


def decimal_to_boolean(
    series: decimal.Decimal | array_like,
    rounding: str | None = None,
    dtype: dtype_like = bool
) -> pd.Series:
    """Convert a decimal series to booleans, using the specified rounding rule.
    """
    # TODO: bool() rounds toward infinity by default
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_decimal_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_boolean_dtype(dtype)

    raise NotImplementedError()


def decimal_to_integer(
    series: decimal.Decimal | array_like,
    rounding: str | None = None,
    dtype: dtype_like = int,
    downcast: bool = False
) -> pd.Series:
    """Convert a decimal series to integers, using the specified rounding rule.
    """
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_decimal_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_integer_dtype(dtype)

    raise NotImplementedError()


def decimal_to_float(
    series: decimal.Decimal | array_like,
    dtype: dtype_like = float,
    downcast: bool = False
) -> pd.Series:
    """Convert a decimal series into floats."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_decimal_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_float_dtype(dtype)

    raise NotImplementedError()


def decimal_to_complex(
    series: decimal.Decimal | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False
) -> pd.Series:
    """Convert a decimal series into another complex dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_decimal_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_complex_dtype(dtype)

    raise NotImplementedError()


def decimal_to_decimal(
    series: decimal.Decimal | array_like,
    dtype: dtype_like = decimal.Decimal
) -> pd.Series:
    """Convert a decimal series into decimals."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_decimal_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_decimal_dtype(dtype)

    raise NotImplementedError()


# def decimal_to_datetime(
#     series: decimal.Decimal | array_like,
#     unit: str | array_like = "ns",
#     since: str | datetime_like | array_like = "1970-01-01 00:00:00+0000",
#     tz: str | datetime.tzinfo = "local",
#     dtype: dtype_like = "datetime"
# ) -> pd.Series:
#     """Convert a decimal series to datetimes."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_decimal_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_boolean_dtype(dtype)

#     raise NotImplementedError()


# def decimal_to_timedelta(
#     series: decimal.Decimal | array_like,
#     unit: str | array_like = "ns",
#     since: str | timedelta_like | array_like = "00:00:00",
#     dtype: dtype_like = "timedelta"
# ) -> pd.Series:
#     """Convert a decimal series to timedeltas."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     _validate_decimal_series(series)
#     _validate_dtype_is_scalar(dtype)
#     _validate_timedelta_dtype(dtype)

#     raise NotImplementedError()


def decimal_to_string(
    series: decimal.Decimal | array_like,
    dtype: dtype_like = pd.StringDtype()
) -> pd.Series:
    """Convert a decimal series to string."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_decimal_series(series)
    _validate_dtype_is_scalar(dtype)
    _validate_timedelta_dtype(dtype)

    raise NotImplementedError()


########################
####    Datetime    ####
########################




#########################
####    Timedelta    ####
#########################



######################
####    String    ####
######################

