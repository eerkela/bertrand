"""Simple Cython loops for typing-related operations on np arrays.

Further reading:
    https://blog.paperspace.com/faster-np-array-processing-ndarray-cython/
"""
import datetime
from cpython cimport datetime
import decimal
import re

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

import dateutil


# TODO: this should go in a parse.pyx file


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
def string_to_boolean(
    np.ndarray[object] arr
) -> tuple[np.ndarray[object], np.ndarray[bool]]:
    """Convert a boolean string ('True', 'False', 'yes', 'no', 'on', 'off',
    etc.) to its equivalent boolean value.  Invalid results are converted to
    `pd.NA`, and their locations are returned as the second element of the
    resulting tuple `(result, invalid)`.

    Parameters
    ----------
    arr (np.ndarray[object]):
        The array to convert.  Must contain strings and have `dtype='O'`.

    Returns
    -------
    tuple[np.ndarray[object], np.ndarray[bool]]:
        A tuple of np arrays.  The first index contains the result of the
        boolean conversion, including invalid results.  The second index is
        a boolean mask indicating the locations of invalid results.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef str s
    cdef np.ndarray[object] result
    cdef np.ndarray[np.uint8_t, ndim=1, cast=True] invalid

    result = np.empty(arr_length, dtype="O")
    invalid = np.empty(arr_length, dtype=bool)

    for i in range(arr_length):
        s = arr[i].replace(" ", "").lower()
        result[i] = bool_strings.get(s, pd.NA)
        invalid[i] = s not in bool_strings
    return result, invalid


@cython.boundscheck(False)
@cython.wraparound(False)
def split_complex_strings(
    np.ndarray[object] arr
) -> tuple[np.ndarray[object], np.ndarray[object], np.ndarray[bool]]:
    """test"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef str element
    cdef list[str] components
    cdef np.ndarray[object] real
    cdef np.ndarray[object] imag
    cdef np.ndarray[np.uint8_t, ndim=1, cast=True] invalid

    real = np.empty(arr_length, dtype="O")
    imag = np.empty(arr_length, dtype="O")
    invalid = np.empty(arr_length, dtype=bool)

    # build regex to extract real, imaginary components from complex string
    cdef str number_regex = r"[+-]?[\d.]+(e[+-]?\d+)?"
    cdef str nan_or_inf = r"[+-]?(nan|infinity|infty|inf)"
    cdef str real_regex = rf"(?P<real>{number_regex}|{nan_or_inf})(?![\d.eji])"
    cdef str imag_regex = rf"(?P<imag>{number_regex}|{nan_or_inf})[ji]"
    cdef list patterns = [
        re.compile(rf"{real_regex}{imag_regex}"),
        re.compile(rf"{imag_regex}{real_regex}"),
        re.compile(rf"{real_regex}"),
        re.compile(rf"{imag_regex}")
    ]
    cdef object pat  # re.Pattern
    cdef object match  # re.Match
    cdef dict groups = None

    # iterate through array
    for i in range(arr_length):
        element = arr[i].replace(" ", "").lower()

        # attempt to match element
        for pat in patterns:
            match = pat.search(element)
            if match:
                groups = match.groupdict()
                break

        # extract real, imaginary components
        if groups is None:
            real[i] = "nan"
            imag[i] = "nan"
            invalid[i] = True
        else:
            real[i] = groups.get("real", "0")
            imag[i] = groups.get("imag", "0")
            invalid[i] = False

        # reset for next iteration
        groups = None

    return real, imag, invalid
