"""Implements 2 functions `apply_tolerance` and `round`, which both allow
efficient, customizable rounding on arbitrary numeric inputs.

`apply_tolerance` is mostly meant for internal use, in order for conversions
between precise and imprecise formats to compensate for possible floating
point rounding errors.

`round_generic` is generically useful wherever rounding is necessary.  It is
intended as a drop-in replacement for `numpy.round()`, with the addition of
customizable rounding logic and wider support for the types that may be found
in `pdtypes`-enhanced array and series objects.
"""
from __future__ import annotations
import decimal

import numpy as np
import pandas as pd
from pdtypes.cast.base import NumericSeries, RealSeries

from pdtypes.check import get_dtype

from .integer import round_div
from .float import round_float
from .decimal import round_decimal


# rename round_generic -> round, apply_tolerance -> snap


def apply_tolerance(
    val: float | decimal.Decimal | np.ndarray | pd.Series,
    tol: int | float | decimal.Decimal,
    copy: bool = True
) -> float | decimal.Decimal | np.ndarray | pd.Series:
    """Apply a floating point tolerance to an imprecise numeric scalar/vector,
    rounding it to the nearest integer if and only if it is within `tol` units
    from the rounded value.

    Parameters
    ----------
    val (float | decimal.Decimal | np.ndarray | pd.Series):
        The value to be (conditionally) rounded.  Can be vectorized.
    tol (int | float | decimal.Decimal),
        The tolerance to use for the conditional check, which represents the
        width of the 2-sided region around each integer within which rounding
        is performed.  This can be arbitrarily large, but values over 0.5 are
        functionally equivalent.
    copy (bool):
        Indicates whether to return a modified copy of an input array (`True`),
        or modify it in-place (`False`).  In either case, the return value of
        this function is unaltered, and scalars are always copied. `copy=False`
        should only be used when the previous state of the array can be safely
        discarded.  Defaults to `True`.

    Returns
    -------
    float | decimal.Decimal | np.ndarray | pd.Series:
        The result of conditionally rounding `val` around integers, with
        tolerance `tol`.  If `copy=False` and `val` is array-like, this will
        be a reference to `val` itself.
    """
    if not tol:  # trivial case, tol=0
        return val
    rounded = round_generic(val, "half_even", decimals=0, copy=True)

    # numpy array, using np.where
    if isinstance(val, np.ndarray):
        if copy:
            return np.where(np.abs(val - rounded) > tol, val, rounded)
        val[:] = np.where(np.abs(val - rounded) > tol, val, rounded)
        return val

    # pandas series, using Series.where
    if isinstance(val, pd.Series):
        rounded.index = val.index  # match index
        if copy:
            return val.where(np.abs(val - rounded) > tol, rounded)
        val.where(np.abs(val - rounded) > tol, rounded, inplace=True)
        return val

    # scalar
    if np.abs(val - rounded) > tol:
        return val
    return rounded


def round_generic(
    val: int | float | complex | decimal.Decimal | np.ndarray | pd.Series,
    rule: str = "half_even",
    decimals: int = 0,
    copy: bool = True
) -> int | float | complex | decimal.Decimal | np.ndarray | pd.Series:
    """Round a numeric scalar/vector according to the specified `rule`.

    This function is intended to be a drop-in replacement for `numpy.round()`,
    with the addition of customizable rounding logic and wider support for the
    types that may be found in `pdtypes`-enhanced array and series objects.

    Parameters
    ----------
    val (int | float | complex | decimal.Decimal | np.ndarray | pd.Series):
        Any kind of numeric scalar or vector to round.  If this is an integer,
        no rounding is done unless `decimals` is negative.
    rule (str):
        A string specifying the rounding strategy to use.  Must be one of
        ('floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
        'half_down', 'half_up', 'half_even'), where `up`/`down` round
        away/toward zero, and `ceiling`/`floor` round toward +/- infinity,
        respectively.  Defaults to 'half_even'.
    decimals (int):
        The number of decimals to round to.  Positive numbers count to the
        right of the decimal point, and negative values count to the left.
        0 represents rounding in the ones place of `val`.  This follows the
        convention set out in `numpy.around`.  Defaults to 0.
    copy (bool):
        Indicates whether to return a modified copy of an input array (`True`),
        or modify it in-place (`False`).  In either case, the return value of
        this function is unaltered, and scalars are always copied. `copy=False`
        should only be used when the previous state of the array can be safely
        discarded.  Defaults to `True`.

    Returns
    -------
    int | float | complex | decimal.Decimal | np.ndarray | pd.Series:
        The result of rounding `val` according to the given rule.  If
        `copy=False` and `val` is array-like, this will be a reference to
        `val` itself.

    Raises
    ------
    ValueError:
        If `rule` is not one of the accepted rounding rules ('floor',
        'ceiling', 'down', 'up', 'half_floor', 'half_ceiling', 'half_down',
        'half_up', 'half_even').
    """
    dtype = get_dtype(val, exact=False)
    if isinstance(dtype, set):
        raise ValueError(f"`val` must have homogenous element types, not "
                         f"{dtype}")

    # numerics
    if dtype == int:
        if decimals < 0:
            scale = 10**(-1 * decimals)
            val = round_div(val, scale, rule=rule, copy=copy)
            val *= scale
        return val
    if dtype == float:
        return round_float(val, rule=rule, decimals=decimals, copy=copy)
    if dtype == complex:
        if isinstance(val, (np.ndarray, pd.Series)):
            if copy:
                val = val.copy()
            round_float(val.real, rule=rule, decimals=decimals, copy=False)
            round_float(val.imag, rule=rule, decimals=decimals, copy=False)
            return val
        real = round_float(val.real, rule=rule, decimals=decimals, copy=True)
        imag = round_float(val.imag, rule=rule, decimals=decimals, copy=True)
        return type(val)(f"{real}+{imag}j")
    if dtype == decimal.Decimal:
        return round_decimal(val, rule=rule, decimals=decimals, copy=copy)

    # everything else
    raise TypeError(f"`val` must be numeric, not {dtype}")


def snap_round(
    series: RealSeries,
    tol: int | float | decimal.Decimal,
    rule: str
) -> RealSeries:
    """Snap a FloatSeries to the nearest integer if it is within `tol`,
    otherwise apply the selected rounding rule.
    """
    # don't snap if rounding to nearest
    nearest = (
        "half_floor", "half_ceiling", "half_down", "half_up", "half_even"
    )

    # NOTE: with copy=False, these will modify SeriesWrappers in-place

    # snap
    if tol and rule not in nearest:
        series.series = apply_tolerance(series.series, tol=tol, copy=False)

    # round
    if rule:
        series.series = round_generic(series.series, rule=rule, copy=False)

    # return
    return series
