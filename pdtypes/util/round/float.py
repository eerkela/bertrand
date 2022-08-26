"""Implements a single function `round_float`, which performs customizable
rounding on floating point numbers and vectors.
"""
from __future__ import annotations

import numpy as np
import pandas as pd


def _round_up(
    val: float | np.ndarray | pd.Series,
    out: None | np.ndarray | pd.Series = None
) -> float | np.ndarray | pd.Series:
    sign = np.sign(val)
    result = np.ceil(np.abs(val, out=out), out=out)
    result *= sign
    return result


def _round_half_down(
    val: float | np.ndarray | pd.Series,
    out: None | np.ndarray | pd.Series = None
) -> float | np.ndarray | pd.Series:
    sign = np.sign(val)
    result = np.ceil(np.abs(val, out=out) - 0.5, out=out)
    result *= sign
    return result


def _round_half_floor(
    val: float | np.ndarray | pd.Series,
    out: None | np.ndarray | pd.Series = None
) -> float | np.ndarray | pd.Series:
    return np.ceil(val - 0.5, out=out)


def _round_half_ceiling(
    val: float | np.ndarray | pd.Series,
    out: None | np.ndarray | pd.Series = None
) -> float | np.ndarray | pd.Series:
    return np.floor(val + 0.5, out=out)


def _round_half_up(
    val: float | np.ndarray | pd.Series,
    out: None | np.ndarray | pd.Series = None
) -> float | np.ndarray | pd.Series:
    sign = np.sign(val)
    result = np.floor(np.abs(val, out=out) + 0.5, out=out)
    result *= sign
    return result


float_rounding_rules = {
    "floor": np.floor,  # toward -infinity
    "ceiling": np.ceil,  # toward +infinity
    "down": np.trunc,  # toward 0
    "up": _round_up,  # away from 0
    "half_floor": _round_half_floor,  # half toward -infinity
    "half_ceiling": _round_half_ceiling,  # half toward +infinity
    "half_down": _round_half_down,  # half toward 0
    "half_up": _round_half_up,  # half away from 0
    "half_even": np.round  # half toward nearest even
}


def round_float(
    val: float | np.ndarray | pd.Series,
    rule: str = "half_even",
    decimals: int = 0,
    copy: bool = True
) -> float | np.ndarray | pd.Series:
    """Round a float or array of floats according to the specified `rule`.

    Parameters
    ----------
    val (float | np.ndarray | pd.Series):
        The value to be rounded.  Can be vectorized.
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
    float | np.ndarray | pd.Series:
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
    # select rounding strategy
    try:
        round_func = float_rounding_rules[rule]
    except KeyError as err:
        err_msg = (f"`rule` must be one of {tuple(float_rounding_rules)}, not "
                   f"{repr(rule)}")
        raise ValueError(err_msg) from err

    # case 1: vectorized
    if isinstance(val, (np.ndarray, pd.Series)):
        # optionally copy
        if copy:
            val = val.copy()

        # optionally scale
        if decimals:
            scale_factor = 10**decimals
            val *= scale_factor

        # round in-place
        if rule == "half_even" and isinstance(val, pd.Series):
            # special case: pandas implementation of round() ('half_even')
            # does not support the `out` parameter.  In this case, we use the
            # numpy implementation instead.
            index = val.index
            val = round_func(val.to_numpy(), out=val)
            val = pd.Series(val, index=index, copy=False)
        else:
            val = round_func(val, out=val)

        # undo scaling and return
        if decimals:
            val /= scale_factor
        return val

    # case 2: scalar (always copied)
    if decimals:
        scale_factor = 10**decimals
        return round_func(val * scale_factor) / scale_factor
    return round_func(val)
