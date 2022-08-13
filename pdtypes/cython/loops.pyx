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
    # TODO: only a substantial increase when supertypes=True
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
def as_pyint(numpy.ndarray arr):
    # TODO: not necessarily a performance increase over np.frompyfunc
    cdef int arr_length = arr.shape[0]
    cdef numpy.ndarray result = numpy.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = int(arr[i])

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
def as_decimal(numpy.ndarray arr, preprocess = None):
    # TODO: not a substantial performance increase over np.frompyfunc
    cdef int arr_length = arr.shape[0]
    cdef numpy.ndarray result = numpy.empty(arr_length, dtype="O")

    if preprocess is not None:
        for i in range(arr_length):
            result[i] = decimal.Decimal(preprocess(arr[i]))
    else:
        for i in range(arr_length):
            result[i] = decimal.Decimal(arr[i])

    return result
