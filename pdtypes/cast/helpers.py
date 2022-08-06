from __future__ import annotations
from contextlib import contextmanager
from dataclasses import dataclass
import decimal
from typing import Any, Generator

import numpy as np
import pandas as pd

from pdtypes.check import is_dtype, resolve_dtype, supertype
from pdtypes.error import error_trace, shorten_list
from pdtypes.util.array import vectorize
from pdtypes.util.time import _to_ns
from pdtypes.util.type_hints import array_like, dtype_like, scalar


def downcast_int_dtype(dtype, min_val, max_val) -> type:
    """Attempt to find a smaller dtype that can represent the given integer
    range defined by [min_val, max_val].
    """
    # resolve aliases
    dtype = resolve_dtype(dtype)

    # get all integer dtypes smaller than `dtype`
    if is_dtype(dtype, "unsigned"):
        int_types = [np.uint8, np.uint16, np.uint32, np.uint64]
    else:
        int_types = [np.int8, np.int16, np.int32, np.int64]
    smaller = int_types[:int_types.index(dtype)]

    # return smallest dtype that fully covers the given range
    for downcast_type in smaller:
        min_poss, max_poss = int_dtype_range(downcast_type)
        if min_val >= min_poss and max_val <= max_poss:
            return downcast_type

    # `dtype` could not be downcast.  Return original.
    return dtype


def int_dtype_range(dtype: dtype_like) -> tuple[int, int]:
    """Get the available range of a given integer dtype."""
    # convert to pandas dtype to expose .itemsize attribute
    dtype = pd.api.types.pandas_dtype(dtype)
    bit_size = 8 * dtype.itemsize
    if pd.api.types.is_unsigned_integer_dtype(dtype):
        return (0, 2**bit_size - 1)
    return (-2**(bit_size - 1), 2**(bit_size - 1) - 1)


@dataclass
class SubsetContainer:
    """test"""
    subset: pd.Series
    result: pd.Series = None


class SeriesWrapper:
    """test"""

    def __init__(self, series: scalar | array_like) -> SeriesWrapper:
        self.series = pd.Series(vectorize(series), copy=False)
        self.not_na = self.series.notna()
        self.hasnans = (~self.not_na).any()

    @contextmanager
    def exclude_na(
        self,
        fill_value: Any,
        dtype: dtype_like = "O"
    ) -> Generator[SubsetContainer, None, None]:
        """test"""
        ctx = SubsetContainer(self.series[self.not_na])
        yield ctx
        ctx.result = np.full(self.series.shape, fill_value, dtype=dtype)
        ctx.result = pd.Series(ctx.result)
        ctx.result[self.not_na] = ctx.subset

    @staticmethod
    def _validate_datetime_format(
        format: None | str,
        day_first: bool,
        year_first: bool
    ) -> None:
        """Raise a TypeError if `format` is not None or a string, and a
        RuntimeError if it is given and `day_first` or `year_first` are not
        False.
        """
        # check format is a string or None
        if format is not None:
            if day_first or year_first:
                err_msg = (f"[{error_trace()}] `day_first` and `year_first` "
                           f"should not be used when `format` is given.")
                raise RuntimeError(err_msg)
            if not isinstance(format, str):
                err_msg = (f"[{error_trace()}] `format` must be a datetime "
                           f"format string or None, not {type(format)}")
                raise TypeError(err_msg)

    @staticmethod
    def _validate_dtype(dtype: dtype_like, category: dtype_like) -> None:
        if not is_dtype(dtype, category):
            sup_type = supertype(category)
            if isinstance(sup_type, type):
                sup_type = sup_type.__name__
            err_msg = (f"[{error_trace()}] `dtype` must be {sup_type}-like, "
                       f"not {repr(dtype)}")
            raise TypeError(err_msg)

    @staticmethod
    def _validate_errors(errors: str) -> None:
        """Raise  a TypeError if `errors` isn't a string, and a ValueError if
        it is not one of the accepted error-handling rules ('raise', 'coerce',
        'ignore').
        """
        valid_errors = ("raise", "coerce", "ignore")
        if not isinstance(errors, str):
            err_msg = (f"[{error_trace()}] `errors` must be a string "
                       f"{valid_errors}, not {type(errors)}")
            raise TypeError(err_msg)
        if errors not in valid_errors:
            err_msg = (f"[{error_trace()}] `errors` must be one of "
                       f"{valid_errors}, not {repr(errors)}")
            raise ValueError(err_msg)

    @staticmethod
    def _validate_rounding(rounding: str | None) -> None:
        """Raise a TypeError if `rounding` isn't None or a string, and a
        ValueError if it is not one of the accepted rounding rules ('floor',
        'ceiling', 'down', 'up', 'half_floor', 'half_ceiling', 'half_down',
        'half_up', 'half_even').
        """
        if rounding is None:
            return None

        valid_rules = ("floor", "ceiling", "down", "up", "half_floor",
                    "half_ceiling", "half_down", "half_up", "half_even")
        if not isinstance(rounding, str):
            err_msg = (f"[{error_trace()}] `rounding` must be a string in "
                       f"{valid_rules}, not {type(rounding)}")
            raise TypeError(err_msg)
        if rounding not in valid_rules:
            err_msg = (f"[{error_trace()}] `rounding` must be one of "
                       f"{valid_rules}, not {repr(rounding)}")
            raise ValueError(err_msg)

        return None

    @staticmethod
    def _validate_tolerance(tol: int | float | decimal.Decimal) -> None:
        """Raise a TypeError if `tol` isn't a real numeric, and a ValueError
        if it is not between 0 and 0.5.
        """
        if not isinstance(tol, (int, float, decimal.Decimal)):
            err_msg = (f"[{error_trace()}] `tol` must be a real numeric "
                       f"between 0 and 0.5, not {type(tol)}")
            raise TypeError(err_msg)
        if not 0 <= tol <= 0.5:
            err_msg = (f"[{error_trace()}] `tol` must be a real numeric "
                       f"between 0 and 0.5, not {tol}")
            raise ValueError(err_msg)

    @staticmethod
    def _validate_unit(unit: str | np.ndarray | pd.Series) -> None:
        """Efficiently check whether an array of units is valid."""
        valid = list(_to_ns) + ["M", "Y"]
        if not np.isin(unit, valid).all():
            bad = list(np.unique(unit[~np.isin(unit, valid)]))
            err_msg = (f"[{error_trace()}] `unit` {shorten_list(bad)} not "
                       f"recognized: must be in {valid}")
            raise ValueError(err_msg)
