"""Simple Cython loops for typing-related operations on np arrays.

Further reading:
    https://blog.paperspace.com/faster-np-array-processing-ndarray-cython/
"""
import decimal

cimport cython
import numpy as np
cimport numpy as np


@cython.boundscheck(False)
@cython.wraparound(False)
def quantize_decimal(
    np.ndarray[object] arr,
    rule,
    bint copy = True
) -> np.ndarray[object]:
    """Performs fast, elementwise rounding on a decimal arrays, according to
    the given rounding rule.

    Parameters
    ----------
    arr (np.ndarray[object]):
        The array to round.  Must contain `decimal.Decimal` objects with
        callable `.quantize()` methods.
    rule:
        A lambda function that accepts exactly one `decimal.Decimal` argument,
        which is called within the body of the loop.  It should return a new
        `decimal.Decimal` object representing the result of the desired
        rounding behavior.  Most often, this will call `Decimal.quantize()`
        with some combination of conditions and decimal rounding rules to
        replicate the most commonly used rounding rules ('floor', 'ceiling',
        ..., 'half_up', 'half_even', etc.).
    copy (bint):
        If `False`, modify `arr` in-place.  Else, return a copy.

    Returns
    -------
    np.ndarray[object]:
        The result of doing elementwise rounding on the decimal array `arr`.
        If `copy=False`, this is a direct reference to the modified array.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i

    # modify in-place
    if not copy:
        for i in range(arr_length):
            arr[i] = rule(arr[i])
        return arr

    # return a copy
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")
    for i in range(arr_length):
        result[i] = rule(arr[i])
    return result