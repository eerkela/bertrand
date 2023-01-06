"""Implements a single function `round_div`, which mimics the integer division
operator `//`, but with customizable rounding behavior.
"""
from __future__ import annotations

import numpy as np
import pandas as pd


def _bias_down(
    n: int | np.ndarray | pd.Series,
    d: int | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """Produce the appropriate numerator bias to replicate the 'down' rounding
    strategy.

    Applies 'floor' where `n` and `d` have the same sign, and 'ceiling'
    where they differ.  This is equivalent to:
        `((n < 0) ^ (d < 0)) * (d - sign(d))`
    """
    d_less_than_zero = (d < 0)
    bias = d - (d > 0) + d_less_than_zero
    bias *= ((n < 0) ^ d_less_than_zero)
    return bias


def _bias_up(
    n: int | np.ndarray | pd.Series,
    d: int | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """Produce the appropriate numerator bias to replicate the 'up' rounding
    strategy.

    Applies 'ceiling' where `n` and `d` have the same sign, and 'floor'
    where they differ.  This is equivalent to:
        `((n > 0) ^ (d < 0)) * (d - sign(d))`
    """
    d_less_than_zero = (d < 0)
    bias = d - (d > 0) + d_less_than_zero
    bias *= ((n > 0) ^ d_less_than_zero)
    return bias


def _bias_half_down(
    n: int | np.ndarray | pd.Series,
    d: int | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """Produce the appropriate numerator bias to replicate the 'half_down'
    rounding strategy.

    Applies 'half_floor' where `n` and `d` have the same sign, and
    'half_ceiling' where they differ.  This is equivalent to:
        `(d + (d < 0) - ((n > 0) ^ (d < 0)) * sign(d)) // 2`
    """
    d_less_than_zero = (d < 0)
    bias = d + d_less_than_zero
    bias -= ((n > 0) ^ d_less_than_zero) * (-1 * d_less_than_zero + (d > 0))
    bias //= 2
    return bias


def _bias_half_up(
    n: int | np.ndarray | pd.Series,
    d: int | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """Produce the appropriate numerator bias to replicate the 'half_up'
    rounding strategy.

    Applies 'half_ceiling' where `n` and `d` have the same sign, and
    'half_floor' where they differ.  This is equivalent to:
        `(d + (d < 0) - ((n < 0) ^ (d < 0)) * sign(d)) // 2`
    """
    d_less_than_zero = (d < 0)
    bias = d + d_less_than_zero
    bias -= ((n < 0) ^ d_less_than_zero) * (-1 * d_less_than_zero + (d > 0))
    bias //= 2
    return bias


def _bias_half_even(
    n: int | np.ndarray | pd.Series,
    d: int | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """Produce the appropriate numerator bias to replicate the 'half_even'
    rounding strategy.

    Applies 'half_ceiling' where the quotient `n // d` would be odd, and
    'half_floor' where it would be even.  This is equivalent to:
        `(d + (d < 0) + ((n // d) % 2 - 1) * sign(d)) // 2`
    """
    d_less_than_zero = (d < 0)
    bias = d + d_less_than_zero
    bias += ((n // d) % 2 - 1) * (-1 * d_less_than_zero + (d > 0))
    bias //= 2
    return bias


integer_rounding_bias = {
    "ceiling":      lambda n, d: d - (d > 0) + (d < 0),
    "down":         _bias_down,
    "up":           _bias_up,
    "half_floor":   lambda n, d: (d - (d > 0) + 2 * (d < 0)) // 2,
    "half_ceiling": lambda n, d: (d + (d < 0)) // 2,
    "half_down":    _bias_half_down,
    "half_up":      _bias_half_up,
    "half_even":    _bias_half_even
}


def round_div(
    numerator: int | np.ndarray | pd.Series,
    denominator: int | np.ndarray | pd.Series,
    rule: str = "floor",
    copy: bool = True
) -> int | np.array | pd.Series:
    """Vectorized integer division with customizable rounding behavior.

    Unlike other approaches, this function does not perform float conversion
    at any point.  Instead, it replicates each rounding rule by adding a
    simple integer bias at each index before applying the `//` floor division
    operator.  This allows it to retain full integer precision for arbitrary
    choices of `numerator` and `denominator`.

    Parameters
    ----------
    numerator (int | np.ndarray | pd.Series):
        Integer numerator.  Can be vectorized (`np.ndarray`/`pd.Series`),
        with arbitrary dimensions.
    denominator (int | np.ndarray | pd.Series):
        Integer denominator.  Can be vectorized (`np.ndarray`/`pd.Series`),
        with arbitrary dimensions.
    rule (str):
        A string specifying the rounding strategy to use.  Must be one of
        ('floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
        'half_down', 'half_up', 'half_even'), where `down`/`up` round
        toward/away from zero, and `floor`/`ceiling` round toward -/+ infinity,
        respectively.  Defaults to 'floor', which matches the behavior of the
        base `//` operator.
    copy (bool):
        Indicates whether to copy the input data during rounding.  If this
        is set to `False` and `numerator` is a numpy array or pandas Series,
        then the rounded quotient will be assigned back to `numerator` without
        copying, essentially replicating the behavior of the `//=` operator.
        In either case, the return value of this function is unaltered, and
        scalars are always copied.  `copy=False` should only be used when the
        previous state of the array can be safely discarded.  Defaults to
        `True`.

    Returns
    -------
    int | np.ndarray | pd.Series:
        The result of integer division with the specified rounding rule.  If
        either of the numeric inputs are vectorized, the result will match the
        input type.

    Raises
    ------
    ValueError:
        If `rule` is not one of the accepted rounding rules ('floor',
        'ceiling', 'down', 'up', 'half_floor', 'half_ceiling', 'half_down',
        'half_up', 'half_even').
    """
    if rule == "floor":  # no bias to add, just use // directly
        if copy:
            return numerator // denominator
        numerator //= denominator
        return numerator

    # get numerator bias for given rounding rule
    try:
        bias = integer_rounding_bias[rule](numerator, denominator)
    except KeyError as err:
        valid_rules = ('floor') + tuple(integer_rounding_bias)
        err_msg = f"`rule` must be one of {valid_rules}, not {repr(rule)}"
        raise ValueError(err_msg) from err

    # return
    if copy:
        return (numerator + bias) // denominator
    numerator += bias
    numerator //= denominator
    return numerator
