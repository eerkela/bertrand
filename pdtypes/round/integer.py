"""Implements a single function `round_div`, which mimics the integer division
operator `//`, but with customizable rounding behavior.
"""
from __future__ import annotations

import numpy as np
import pandas as pd


integer_rounding_bias = {
    "floor":        lambda n, d: 0,
    "ceiling":      lambda n, d: d - 1,
    "down":         lambda n, d: ((n < 0) ^ (d < 0)) * (d - 1),
    "up":           lambda n, d: ((n > 0) ^ (d < 0)) * (d - 1),
    "half_floor":   lambda n, d: d // 2 - 1,
    "half_ceiling": lambda n, d: d // 2,
    "half_down":    lambda n, d: d // 2 - ((n > 0) ^ (d < 0)),
    "half_up":      lambda n, d: d // 2 - ((n < 0) ^ (d < 0)),
    "half_even":    lambda n, d: d // 2 + (n // d % 2 - 1)
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
    simple integer bias at each index, before applying the `//` operator.  This
    allows it to retain full integer precision for arbitrary choices of
    `numerator` and `denominator`.

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
    try:
        bias = integer_rounding_bias[rule](numerator, denominator)
    except KeyError as err:
        err_msg = (f"`rule` must be one of {tuple(integer_rounding_bias)}, "
                   f"not {repr(rule)}")
        raise ValueError(err_msg) from err

    if copy or not isinstance(numerator, (np.ndarray, pd.Series)):
        return (numerator + bias) // denominator
    numerator += bias
    numerator //= denominator
    return numerator
