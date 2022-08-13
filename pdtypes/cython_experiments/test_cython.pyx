import time
import numpy
cimport numpy
cimport cython

import datetime
import decimal
import pandas


@cython.boundscheck(False)
@cython.wraparound(False)
def do_calc(numpy.ndarray[numpy.int_t, ndim=1] arr):
    cdef int maxval
    cdef unsigned long long int total
    cdef int k
    cdef double t1, t2, t
    cdef int arr_shape = arr.shape[0]

    t1 = time.time()

    for k in range(arr_shape):
        total = total + arr[k]
    print(f"Total = {total}")

    t2 = time.time()
    t = t2 - t1
    print(f"{t:.20f}")




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
    str: str,

    # bytes
    bytes: bytes,

    # missing values
    type(None): type(None)
}



@cython.boundscheck(False)
@cython.wraparound(False)
def object_types(numpy.ndarray arr, bint supertypes = False):
    cdef int arr_length = arr.shape[0]
    cdef numpy.ndarray result = numpy.empty(arr_length, dtype="O")

    if supertypes:
        for i in range(arr_length):
            result[i] = atomic_to_supertype[type(arr[i])]
    else:
        for i in range(arr_length):
            result[i] = type(arr[i])

    return result
