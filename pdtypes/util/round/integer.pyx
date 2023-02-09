"""Implements a single function `round_div`, which mimics the integer division
operator `//`, but with customizable rounding behavior.
"""
import numpy as np
import pandas as pd

from pdtypes.type_hints import array_like


######################
####    PUBLIC    ####
######################


def round_div(
    numerator: int | array_like,
    denominator: int | array_like,
    rule: str = "floor"
) -> int | array_like:
    """Vectorized integer division with customizable rounding behavior.

    Unlike other approaches, this function does not perform float conversion
    at any point.  Instead, it replicates each rounding rule by adding a
    simple integer bias at each index before applying the `//` floor division
    operator.  This allows it to retain full integer precision for arbitrary
    choices of `numerator` and `denominator`.

    Parameters
    ----------
    numerator : int | array_like
        Integer numerator.  Can be vectorized (`np.ndarray`/`pd.Series`),
        with arbitrary dimensions.
    denominator : int | array_like
        Integer denominator.  Can be vectorized (`np.ndarray`/`pd.Series`),
        with arbitrary dimensions.
    rule : str (default 'floor')
        A string specifying the rounding strategy to use.  Must be one of
        ('floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
        'half_down', 'half_up', 'half_even'), where `down`/`up` round
        toward/away from zero, and `floor`/`ceiling` round toward -/+ infinity,
        respectively.  Defaults to 'floor', which matches the behavior of the
        base `//` operator.

    Returns
    -------
    int | np.ndarray:
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
    # NOTE: // does not automatically broadcast pd.Series objects
    if (
        isinstance(numerator, pd.Series) and
        isinstance(denominator, pd.Series) and
        numerator.shape != denominator.shape
    ):
        raise ValueError(
            f"operands could not be broadcast together with shapes "
            f"{numerator.shape} {denominator.shape}"
        )

    if rule == "floor":  # no bias to add, just use // directly
        return numerator // denominator

    try:  # get numerator bias for given rounding rule
        bias = integer_rounding_bias[rule](numerator, denominator)
    except KeyError as err:
        valid_rules = ('floor',) + tuple(integer_rounding_bias)
        err_msg = f"`rule` must be one of {valid_rules}, not {repr(rule)}"
        raise ValueError(err_msg) from err

    return (numerator + bias) // denominator


#######################
####    PRIVATE    ####
#######################


def _bias_down(n, d):
    """Apply floor where `n` and `d` have the same sign, and ceiling where
    they differ.  This is equivalent to:
        `((n < 0) ^ (d < 0)) * (d - sign(d))`
    """
    d_less_than_zero = (d < 0)
    bias = d + d_less_than_zero - (d > 0)
    bias *= (n < 0) ^ d_less_than_zero
    return bias


def _bias_up(n, d):
    """Apply ceiling where `n` and `d` have the same sign, and floor where
    they differ.  This is equivalent to:
        `((n > 0) ^ (d < 0)) * (d - sign(d))`
    """
    d_less_than_zero = (d < 0)
    bias = d + d_less_than_zero - (d > 0)
    bias *= (n > 0) ^ d_less_than_zero
    return bias


def _bias_half_down(n, d):
    """Apply half_floor where `n` and `d` have the same sign, and half_ceiling
    where they differ.  This is equivalent to:
        `(d + (d < 0) - ((n > 0) ^ (d < 0)) * sign(d)) // 2`
    """
    d_less_than_zero = (d < 0)
    bias = d + d_less_than_zero
    bias -= ((n > 0) ^ d_less_than_zero) * (-1 * d_less_than_zero + (d > 0))
    bias //= 2
    return bias


def _bias_half_up(n, d):
    """Apply half_ceiling where `n` and `d` have the same sign, and half_floor
    where they differ.  This is equivalent to:
        `(d + (d < 0) - ((n < 0) ^ (d < 0)) * sign(d)) // 2`
    """
    d_less_than_zero = (d < 0)
    bias = d + d_less_than_zero
    bias -= ((n < 0) ^ d_less_than_zero) * (-1 * d_less_than_zero + (d > 0))
    bias //= 2
    return bias


def _bias_half_even(n, d):
    """Apply half_ceiling where the quotient `n // d` would be odd, and
    half_floor where it would be even.  This is equivalent to:
        `(d + (d < 0) + ((n // d) % 2 - 1) * sign(d)) // 2`
    """
    d_less_than_zero = (d < 0)
    bias = d + d_less_than_zero
    bias += ((n // d) % 2 - 1) * (-1 * d_less_than_zero + (d > 0))
    bias //= 2
    return bias


cdef dict integer_rounding_bias = {
    "ceiling":      lambda n, d: d - (d > 0) + (d < 0),
    "down":         _bias_down,
    "up":           _bias_up,
    "half_floor":   lambda n, d: (d - (d > 0) + 2 * (d < 0)) // 2,
    "half_ceiling": lambda n, d: (d + (d < 0)) // 2,
    "half_down":    _bias_half_down,
    "half_up":      _bias_half_up,
    "half_even":    _bias_half_even
}
