"""Implements a single function `round_float`, which performs customizable
rounding on floating point numbers and vectors.
"""
cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes.type_hints import array_like


######################
####    PUBLIC    ####
######################


def round_float(
    val: float | array_like,
    rule: str = "half_even",
    decimals: int = 0
) -> float | array_like:
    """Round a float or array of floats according to the specified `rule`.

    Parameters
    ----------
    val : float | array_like
        The value to be rounded.  Can be vectorized.
    rule : str, default 'half_even'
        A string specifying the rounding strategy to use.  Must be one of
        ('floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
        'half_down', 'half_up', 'half_even'), where `up/down` round
        away/toward zero, and `ceiling/floor` round toward +/- infinity,
        respectively.
    decimals : int, default 0
        The number of decimals to round to.  Positive numbers count to the
        right of the decimal point, and negative values count to the left.
        0 represents rounding in the ones place of `val`.  This follows the
        convention set out in `numpy.around()`.

    Returns
    -------
    float | array_like
        The result of rounding `val` according to the given rule.

    Raises
    ------
    ValueError
        If `rule` is not one of the accepted rounding rules ('floor',
        'ceiling', 'down', 'up', 'half_floor', 'half_ceiling', 'half_down',
        'half_up', 'half_even').
    """
    # select rounding strategy
    try:
        round_func = rounding_rules[rule]
    except KeyError as err:
        err_msg = (f"`rule` must be one of {tuple(rounding_rules)}, not "
                   f"{repr(rule)}")
        raise ValueError(err_msg) from err

    if decimals:
        scale_factor = 10**decimals
        return round_func(val * scale_factor) / scale_factor

    return round_func(val)


#######################
####    PRIVATE    ####
#######################


cdef dict rounding_rules = {
    "floor": np.floor,
    "ceiling": np.ceil,
    "down": np.trunc,
    "up": lambda x: np.sign(x) * np.ceil(np.abs(x)),
    "half_floor": lambda x: np.ceil(x - 0.5),
    "half_ceiling": lambda x: np.floor(x + 0.5),
    "half_down": lambda x: np.sign(x) * np.ceil(np.abs(x) - 0.5),
    "half_up": lambda x: np.sign(x) * np.floor(np.abs(x) + 0.5),
    "half_even": np.round
}
