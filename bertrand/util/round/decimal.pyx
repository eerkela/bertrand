"""This module describes a single function (``round_decimal()``), which
performs customizable, vectorized rounding on arbitrary-precision ``Decimal``
values.
"""
import decimal

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd


######################
####    PUBLIC    ####
######################


cpdef object round_decimal(
    object val,
    short decimals,
    str rule
):
    """Performs fast, elementwise rounding on a decimal scalars and arrays,
    according to the given rounding rule.

    Parameters
    ----------
    val : decimal.Decimal | array_like
        The value to round.  If array-like, `val` must have `dtype='O'` and
        contain only `decimal.Decimal` objects.
    rule : str
        A string specifying the rounding strategy to use.  Must be one of
        ('floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
        'half_down', 'half_up', 'half_even'), where `up`/`down` round
        away/toward zero, and `ceiling`/`floor` round toward +/- infinity,
        respectively.  Defaults to 'half_even'.
    decimals : int
        The number of decimals to round to.  Positive numbers count to the
        right of the decimal point, and negative values count to the left.
        0 represents rounding in the ones place of `val`.  This follows the
        convention set out in `numpy.around`.  Defaults to 0.

    Returns
    -------
    decimal.Decimal | array_like:
        The result of rounding `val` according to the given rule.

    Raises
    ------
    ValueError:
        If `rule` is not one of the accepted rounding rules ('floor',
        'ceiling', 'down', 'up', 'half_floor', 'half_ceiling', 'half_down',
        'half_up', 'half_even').
    """
    # array
    if isinstance(val, np.ndarray):
        return _round_decimal_array(
            val,
            rule=rule,
            decimals=decimals
        )

    cdef object index

    # series
    if isinstance(val, pd.Series):
        index = val.index
        val = _round_decimal_array(
            val.to_numpy(dtype="O"),
            rule=rule,
            decimals=decimals
        )
        return pd.Series(val, index=index)

    cdef object quantize
    cdef object scale_factor

    # scalar
    if val in decimal_inf_nan:
        return val
    quantize = rounding_rules[rule]
    scale_factor = decimal.Decimal(10) ** decimals
    return quantize(val * scale_factor) / scale_factor



#######################
####    PRIVATE    ####
#######################


cdef set decimal_inf_nan = {
    decimal.Decimal("-inf"),
    decimal.Decimal("inf"),
    decimal.Decimal("nan")
}


cdef dict rounding_rules = {
    "floor": lambda x: x.quantize(1, decimal.ROUND_FLOOR),
    "ceiling": lambda x: x.quantize(1, decimal.ROUND_CEILING),
    "down": lambda x: x.quantize(1, decimal.ROUND_DOWN),
    "up": lambda x: x.quantize(1, decimal.ROUND_UP),
    "half_floor": lambda x: (
        x.quantize(1, decimal.ROUND_HALF_UP) if x < 0 else
        x.quantize(1, decimal.ROUND_HALF_DOWN)
    ),
    "half_ceiling": lambda x: (
        x.quantize(1, decimal.ROUND_HALF_DOWN) if x < 0 else
        x.quantize(1, decimal.ROUND_HALF_UP)
    ),
    "half_down": lambda x: x.quantize(1, decimal.ROUND_HALF_DOWN),
    "half_up": lambda x: x.quantize(1, decimal.ROUND_HALF_UP),
    "half_even": lambda x: x.quantize(1, decimal.ROUND_HALF_EVEN)
}


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] _round_decimal_array(
    object[:] arr,
    str rule = "half_even",
    int decimals = 0
):
    """Call the appropriate .quantize() method for each element of input."""
    cdef int arr_length = arr.shape[0]
    cdef object scale_factor = decimal.Decimal(10)**decimals
    cdef int i
    cdef object element
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")
    
    quantize = rounding_rules[rule]

    for i in range(arr_length):
        element = arr[i]
        if element in decimal_inf_nan:
            result[i] = element
        else:
            result[i] = quantize(element * scale_factor) / scale_factor
    return result
