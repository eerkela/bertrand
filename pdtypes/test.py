from __future__ import annotations
from contextlib import contextmanager
from dataclasses import dataclass
import decimal
from typing import Any, Generator
import sys

import numpy as np
import pandas as pd

from pdtypes.check.check import (
    check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype, supertype
)
from pdtypes.error import error_trace, shorten_list
from pdtypes.util.array import vectorize
from pdtypes.util.time import _to_ns
from pdtypes.util.type_hints import array_like, dtype_like, scalar


###############################
####    Genuine Helpers    ####
###############################


def _downcast_complex_series(
    series: pd.Series,
    real: None | np.ndarray = None,
    imag: None | np.ndarray = None
) -> pd.Series:
    """Attempt to downcast a complex series to the smallest possible complex
    dtype that can fully represent its values, without losing any precision.

    Note: this implementation is fully vectorized (and therefore fast), but
    may exceed memory requirements for very large data.
    """
    real = _downcast_float_series(np.real(series) if real is None else real)
    imag = _downcast_float_series(np.imag(series) if imag is None else imag)
    equivalent_complex_dtype = {
        np.dtype(np.float16): np.complex64,
        np.dtype(np.float32): np.complex64,
        np.dtype(np.float64): np.complex128,
        np.dtype(np.longdouble): np.clongdouble
    }
    most_general = equivalent_complex_dtype[max(real.dtype, imag.dtype)]
    return series.astype(most_general)


def _downcast_float_series(series: pd.Series) -> pd.Series:
    """Attempt to downcast a float series to the smallest possible float dtype
    that can fully represent its values, without losing any precision.

    Note: this implementation is fully vectorized (and therefore fast), but
    may exceed memory requirements for very large data (~10**8 rows).
    """
    dtype = series.dtype
    if np.issubdtype(dtype, np.float16):  # can't downcast past float16
        return series

    # get array elements as raw bits (exluding zeros, nans, infs)
    exclude = np.isnan(series) | np.isin(series, (0.0, -np.inf, np.inf))
    bits = np.array(series[~exclude])
    bits = bits.view(np.uint8).reshape(-1, dtype.itemsize)
    if sys.byteorder == "little":  # ensure big-endian byte order
        bits = bits[:,::-1]
    bits = np.unpackbits(bits, axis=1)

    # decompose bits into unbiased exponent + mantissa
    exp_length = {4: 8, 8: 11, 12: 15, 16: 15}[dtype.itemsize]
    exp_coefs = 2**np.arange(exp_length - 1, -1, -1)
    exp_bias = 2**(exp_length - 1) - 1
    if bits.shape[1] > 64:  # longdouble: only the last 80 bits are relevant
        # longdouble includes an explicit integer bit not present in other
        # IEEE 754 floating point standards, which we exclude from mantissa
        # construction.  This bit can be 0, but only in extreme edge cases
        # involving the Intel 8087/80287 platform or an intentional denormal
        # representation.  This should never occur in practice.
        start = bits.shape[1] - 79
        exponent = np.dot(bits[:,start:start+exp_length], exp_coefs) - exp_bias
        mantissa = bits[:, start+exp_length+1:]
    else:  # np.float32, np.float64
        exponent = np.dot(bits[:,1:1+exp_length], exp_coefs) - exp_bias
        mantissa = bits[:,1+exp_length:]

    # define search types and associated limits
    widths = {  # dtype: (min exp, max exp, mantissa width)
        np.dtype(np.float16): (-2**4 + 2, 2**4 - 1, 10),
        np.dtype(np.float32): (-2**7 + 2, 2**7 - 1, 23),
        np.dtype(np.float64): (-2**10 + 2, 2**10 - 1, 52),
        np.dtype(np.longdouble): (-2**14 + 2, 2**14 - 1, 63),
    }
    # filter `widths` to only include dtypes that are smaller than original
    widths = {k: widths[k] for k in list(widths)[:list(widths).index(dtype)]}

    # search for smaller dtypes that can fully represent series without
    # incurring precision loss
    for float_dtype, (min_exp, max_exp, man_width) in widths.items():
        if (((min_exp <= exponent) & (exponent <= max_exp)).all() and
            not np.sum(mantissa[:,man_width:])):
            return series.astype(float_dtype)
    return series


def _downcast_int_dtype(
    dtype: np.dtype | pd.api.extensions.ExtensionDtype,
    min_val: int | float | complex | decimal.Decimal,
    max_val: int | float | complex | decimal.Decimal
) -> np.dtype | pd.api.extensions.ExtensionDtype:
    dtype = pd.api.types.pandas_dtype(dtype)
    if dtype.itemsize == 1:
        return dtype

    # get type hierarchy
    if pd.api.types.is_extension_array_dtype(dtype):
        if pd.api.types.is_unsigned_integer_dtype(dtype):
            type_hierarchy = {
                8: pd.UInt64Dtype(),
                4: pd.UInt32Dtype(),
                2: pd.UInt16Dtype(),
                1: pd.UInt8Dtype()
            }
        else:
            type_hierarchy = {
                8: pd.Int64Dtype(),
                4: pd.Int32Dtype(),
                2: pd.Int16Dtype(),
                1: pd.Int8Dtype()
            }
    else:
        if pd.api.types.is_unsigned_integer_dtype(dtype):
            type_hierarchy = {
                8: np.dtype(np.uint64),
                4: np.dtype(np.uint32),
                2: np.dtype(np.uint16),
                1: np.dtype(np.uint8)
            }
        else:
            type_hierarchy = {
                8: np.dtype(np.int64),
                4: np.dtype(np.int32),
                2: np.dtype(np.int16),
                1: np.dtype(np.int8)
            }

    # check for smaller dtypes that fit given range
    size = dtype.itemsize
    selected = dtype
    while size > 1:
        test = type_hierarchy[size // 2]
        size = test.itemsize
        if pd.api.types.is_unsigned_integer_dtype(test):
            min_poss = 0
            max_poss = 2**(8 * test.itemsize) - 1
        else:
            min_poss = -2**(8 * test.itemsize - 1)
            max_poss = 2**(8 * test.itemsize - 1) - 1
        if min_val >= min_poss and max_val <= max_poss:
            selected = test
    return selected


def _parse_timezone(tz: str | datetime.tzinfo | None) -> datetime.tzinfo | None:
    # validate timezone and convert to datetime.tzinfo object
    if isinstance(tz, str):
        if tz == "local":
            return pytz.timezone(tzlocal.get_localzone_name())
        return pytz.timezone(tz)
    if tz and not isinstance(tz, pytz.BaseTzInfo):
        err_msg = (f"[{error_trace(stack_index=2)}] `tz` must be a "
                   f"pytz.timezone object or an IANA-recognized timezone "
                   f"string, not {type(tz)}")
        raise TypeError(err_msg)
    return tz


class ObjectSeries(SeriesWrapper):
    """test"""

    def __init__(self, series: Any | array_like) -> ObjectSeries:
        super().__init__(series)

    def to_boolean(
        self,
        dtype: dtype_like = bool,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, bool)
        SeriesWrapper._validate_errors(errors)

        def invoke(element: Any) -> tuple[bool, bool]:
            try:
                return (element.__bool__(), False)
            except AttributeError:
                return (bool(element), True)

        # invoke each object's underlying __bool__() method
        with self.exclude_na(pd.NA) as ctx:
            ctx.subset, invalid = np.frompyfunc(invoke, 1, 2)(ctx.subset)

            # check for conversion errors
            if invalid.any():
                if errors == "raise":
                    mask = self.not_na
                    mask[self.not_na] = invalid
                    bad = self.series[mask]
                    indices = bad.index.values
                    obj_type = get_dtype(bad)
                    if np.array(obj_type).shape:  # more than one bad object
                        obj_type = [repr(t.__name__) for t in obj_type]
                        obj_type = shorten_list(obj_type)
                    else:
                        obj_type = repr(obj_type.__name__)
                    err_msg = (f"[{error_trace()}] object of type {obj_type} "
                               f"has no '__bool__' attribute at index "
                               f"{shorten_list(indices)}")
                    raise AttributeError(err_msg)
                if errors == "ignore":
                    return self.series

        # convert result
        if self.hasnans:
            dtype = extension_type(dtype)
        return ctx.result.astype(dtype, copy=False)

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, int)
        SeriesWrapper._validate_errors(errors)

        def invoke(element: Any) -> tuple[int, bool]:
            try:
                return (int(element), False)
            except TypeError:
                return (pd.NA, True)

        # invoke each object's underlying __int__() method
        with self.exclude_na(pd.NA) as ctx:
            ctx.subset, invalid = np.frompyfunc(invoke, 1, 2)(ctx.subset)

            # check for conversion errors
            if invalid.any():
                if errors == "raise":
                    mask = self.not_na
                    mask[self.not_na] = invalid
                    bad = self.series[mask]
                    indices = bad.index.values
                    obj_type = get_dtype(bad)
                    if np.array(obj_type).shape:  # more than one bad object
                        obj_type = [repr(t.__name__) for t in obj_type]
                        obj_type = shorten_list(obj_type)
                    else:
                        obj_type = repr(obj_type.__name__)
                    err_msg = (f"[{error_trace()}] object of type {obj_type} "
                               f"has no '__int__' attribute at index "
                               f"{shorten_list(indices)}")
                    raise AttributeError(err_msg)
                if errors == "ignore":
                    return self.series

        # use IntegerSeries.to_integer to sort out `dtype`, `downcast` args
        int_result = IntegerSeries(ctx.result, validate=False)
        return int_result.to_integer(dtype=dtype, downcast=downcast,
                                     errors=errors)
