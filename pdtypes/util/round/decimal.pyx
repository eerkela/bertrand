"""Implements a single function `round_decimal`, which performs customizable
rounding on arbitrary-precision decimal numbers and vectors.
"""
import decimal

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd


cdef tuple decimal_inf_nan = (decimal.Decimal("-inf"), decimal.Decimal("inf"),
                              decimal.Decimal("nan"))


cdef dict decimal_rounding_rules = {
    "floor": lambda x: x.quantize(1, decimal.ROUND_FLOOR),
    "ceiling": lambda x: x.quantize(1, decimal.ROUND_CEILING),
    "down": lambda x: x.quantize(1, decimal.ROUND_DOWN),
    "up": lambda x: x.quantize(1, decimal.ROUND_UP),
    "half_floor": (lambda x: x.quantize(1, decimal.ROUND_HALF_UP)
                                if x < 0 else
                                x.quantize(1, decimal.ROUND_HALF_DOWN)),
    "half_ceiling": (lambda x: x.quantize(1, decimal.ROUND_HALF_DOWN)
                                if x < 0 else
                                x.quantize(1, decimal.ROUND_HALF_UP)),
    "half_down": lambda x: x.quantize(1, decimal.ROUND_HALF_DOWN),
    "half_up": lambda x: x.quantize(1, decimal.ROUND_HALF_UP),
    "half_even": lambda x: x.quantize(1, decimal.ROUND_HALF_EVEN)
}


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] _round_decimal_array(
    np.ndarray[object] arr,
    str rule = "half_even",
    int decimals = 0,
    bint copy = True
):
    """Internal C interface for rounding decimal arrays in the public-facing
    `round_decimal` function.
    """
    cdef object scale_factor = decimal.Decimal(10)**decimals
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object element

    quantize = decimal_rounding_rules[rule]

    # modify in-place
    if not copy:
        for i in range(arr_length):
            element = arr[i]
            if element in decimal_inf_nan:
                arr[i] = element
            else:
                arr[i] = quantize(element * scale_factor) / scale_factor
        return arr

    # return a copy
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        element = arr[i]
        if element in decimal_inf_nan:
            result[i] = element
        else:
            result[i] = quantize(element * scale_factor) / scale_factor
    return result


@cython.boundscheck(False)
@cython.wraparound(False)
def round_decimal(
    val: decimal.Decimal | np.ndarray | pd.Series,
    str rule = "half_even",
    int decimals = 0,
    bint copy = True
) -> decimal.Decimal | np.ndarray | pd.Series:
    """Performs fast, elementwise rounding on a decimal scalars and arrays,
    according to the given rounding rule.

    Parameters
    ----------
    val (decimal.Decimal | np.ndarray | pd.Series):
        The value to round.  If array-like, `val` must have `dtype='O'` and
        contain only `decimal.Decimal` objects.
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
    if rule not in decimal_rounding_rules:
        err_msg = (f"`rule` must be one of {tuple(decimal_rounding_rules)}, "
                   f"not {repr(rule)}")
        raise ValueError(err_msg)

    # case 1: numpy array
    if isinstance(val, np.ndarray):
        return _round_decimal_array(val, rule=rule, decimals=decimals,
                                    copy=copy)

    # case 2: pandas Series
    if isinstance(val, pd.Series):
        index = val.index
        val = _round_decimal_array(val.to_numpy(), rule=rule,
                                   decimals=decimals, copy=copy)
        return pd.Series(val, index=index, copy=False)

    # case 3: scalar
    if val in decimal_inf_nan:
        return val
    scale_factor = decimal.Decimal(10)**decimals
    quantize = decimal_rounding_rules[rule]
    return quantize(val * scale_factor) / scale_factor
