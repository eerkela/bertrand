"""Simple Cython loops for typing-related operations on numpy arrays.

Further reading:
    https://blog.paperspace.com/faster-numpy-array-processing-ndarray-cython/
"""
import numpy
cimport numpy
cimport cython

import datetime
import decimal
import pandas


cdef dict atomic_to_supertype = {  # atomic type to associated supertype
    # booleans
    bool: bool,

    # integers
    int: int,
    numpy.int8: int,
    numpy.int16: int,
    numpy.int32: int,
    numpy.int64: int,
    numpy.uint8: int,
    numpy.uint16: int,
    numpy.uint32: int,
    numpy.uint64: int,

    # floats
    float: float,
    numpy.float16: float,
    numpy.float32: float,
    numpy.float64: float,
    numpy.longdouble: float,

    # complex numbers
    complex: complex,
    numpy.complex64: complex,
    numpy.complex128: complex,
    numpy.clongdouble: complex,

    # decimals
    decimal.Decimal: decimal.Decimal,

    # datetimes
    pandas.Timestamp: "datetime",
    datetime.datetime: "datetime",
    numpy.datetime64: "datetime",

    # timedeltas
    pandas.Timedelta: "timedelta",
    datetime.timedelta: "timedelta",
    numpy.timedelta64: "timedelta",

    # strings
    str: str
}


@cython.boundscheck(False)
@cython.wraparound(False)
def object_types(numpy.ndarray arr, bint supertypes = False):
    """Return the type/supertype of each element in a numpy object array.

    Parameters
    ----------
    arr (numpy.ndarray):
        The array to analyze.  Must be a pyobject array with `dtype='O'` for
        the returned types to be accurate.
    supertypes (bint):
        If `True`, return the supertype associated with each element, rather
        than the element type directly.

    Returns
    -------
    numpy.ndarray:
        The result of doing elementwise type introspection on the object array
        `arr`.
    """
    cdef int i
    cdef int arr_length = arr.shape[0]
    cdef numpy.ndarray result = numpy.empty(arr_length, dtype="O")

    if supertypes:
        for i in range(arr_length):
            result[i] = atomic_to_supertype.get(type(arr[i]), object)
    else:
        for i in range(arr_length):
            result[i] = type(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
def quantize_decimal(numpy.ndarray arr, rule, bint copy = True):
    """Performs fast rounding on decimal arrays, according to the given `rule`.

    Parameters
    ----------
    arr (numpy.ndarray):
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
    numpy.ndarray:
        The result of doing elementwise rounding on the decimal array `arr`.
        If `copy=False`, this is a direct reference to the modified array.
    """
    cdef int i
    cdef int arr_length = arr.shape[0]

    # modify in-place
    if not copy:
        for i in range(arr_length):
            arr[i] = rule(arr[i])
        return arr

    # return a copy
    cdef numpy.ndarray result = numpy.empty(arr_length, dtype="O")
    for i in range(arr_length):
        result[i] = rule(arr[i])
    return result


cdef dict bool_strings = {
    # True
    "y": True,
    "yes": True,
    "t": True,
    "true": True,
    "on": True,
    "1": True,

    # False
    "n": False,
    "no": False,
    "f": False,
    "false": False,
    "off": False,
    "0": False
}


@cython.boundscheck(False)
@cython.wraparound(False)
def string_to_boolean(numpy.ndarray arr):
    cdef int i
    cdef int arr_length = arr.shape[0]
    cdef str s
    cdef numpy.ndarray[numpy.uint8_t, ndim=1, cast=True] invalid
    cdef numpy.ndarray result

    result = numpy.empty(arr_length, dtype="O")
    invalid = numpy.empty(arr_length, dtype=bool)

    for i in range(arr_length):
        s = arr[i].replace(" ", "").lower()
        result[i] = bool_strings.get(s, pandas.NA)
        invalid[i] = s not in bool_strings
    return result, invalid
