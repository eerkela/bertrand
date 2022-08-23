"""Simple Cython loops for typing-related operations on numpy arrays.

Further reading:
    https://blog.paperspace.com/faster-np-array-processing-ndarray-cython/
"""
import datetime
import decimal

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from .resolve cimport _resolve_dtype


cdef dict atomic_to_supertype = {
    # maps atomic types to their associated (default) supertype.  Any atomic
    # type that is not explicitly assigned a supertype in this dictionary is
    # interpreted as belonging to the `object` supertype.

    # booleans
    bool: bool,

    # integers
    int: int,
    np.int8: int,
    np.int16: int,
    np.int32: int,
    np.int64: int,
    np.uint8: int,
    np.uint16: int,
    np.uint32: int,
    np.uint64: int,

    # floats
    float: float,
    np.float16: float,
    np.float32: float,
    np.float64: float,
    np.longdouble: float,

    # complex numbers
    complex: complex,
    np.complex64: complex,
    np.complex128: complex,
    np.clongdouble: complex,

    # decimals
    decimal.Decimal: decimal.Decimal,

    # datetimes
    pd.Timestamp: "datetime",
    datetime.datetime: "datetime",
    np.datetime64: "datetime",

    # timedeltas
    pd.Timedelta: "timedelta",
    datetime.timedelta: "timedelta",
    np.timedelta64: "timedelta",

    # strings
    str: str
}


cdef dict default_supertypes = dict()  # inverse of atomic_to_supertype
for key, value in atomic_to_supertype.items():
    default_supertypes.setdefault(value, set()).add(key)


cdef dict custom_supertypes = {
    # custom aliases that refer to various subsets of the default supertype
    # categories.  For instance, `np.integer` matches any integer *except for*
    # built-in python integers.  These can be used to reference specific
    # 'slices' of the default supertype categories.

    # integer
    np.integer: {np.int8, np.int16, np.int32, np.int64, np.uint8, np.uint16,
                 np.uint32, np.uint64},
    np.signedinteger: {np.int8, np.int16, np.int32, np.int64},
    "signed": {int, np.int8, np.int16, np.int32, np.int64},
    "i": {int, np.int8, np.int16, np.int32, np.int64},
    np.unsignedinteger: {np.uint8, np.uint16, np.uint32, np.uint64},
    "unsigned": {np.uint8, np.uint16, np.uint32, np.uint64},
    "u": {np.uint8, np.uint16, np.uint32, np.uint64},

    # float
    np.floating: {np.float16, np.float32, np.float64, np.longdouble},

    # complex
    np.complexfloating: {np.complex64, np.complex128, np.clongdouble}
}


cdef dict custom_to_default_supertype = {
    # maps keys in custom_supertypes to the parent supertype that they
    # slice.  This allows supertype() to correctly resolve the parents of
    # these custom categories, just like any other atomic type

    # integer
    np.integer: int,
    np.signedinteger: int,
    "signed": int,
    "i": int,
    np.unsignedinteger: int,
    "unsigned": int,
    "u": int,

    # float
    np.floating: float,

    # complex
    np.complexfloating: complex
}


cdef list supertype_only = ["datetime", "timedelta", *custom_supertypes]


@cython.boundscheck(False)
@cython.wraparound(False)
@cython.nonecheck(False)
cdef object _supertype(object dtype):
    """Internal C interface for the public-facing supertype() function."""
    cdef str lower = dtype.lower() if isinstance(dtype, str) else None

    # case 1: dtype is a custom supertype alias - get parent supertype
    if dtype in custom_to_default_supertype:
        return custom_to_default_supertype[dtype]
    if lower and lower in custom_to_default_supertype:
        return custom_to_default_supertype[lower]

    # case 2: dtype is a default alias
    if lower and lower in default_supertypes:
        return lower

    # case 3: dtype must be resolved
    return atomic_to_supertype.get(_resolve_dtype(dtype), object)


@cython.boundscheck(False)
@cython.wraparound(False)
@cython.nonecheck(False)
def supertype(object dtype) -> type | str:
    """Return the supertype associated with a given dtype.  If none exists,
    return `object` instead.
    """
    return _supertype(dtype)


@cython.boundscheck(False)
@cython.wraparound(False)
@cython.nonecheck(False)
cdef set _subtypes(
    object dtype,
    bint exact = False
):
    """Internal C interface for the public-facing subtypes() function."""
    if exact:
        return {_resolve_dtype(dtype)}

    cdef str lower = dtype.lower() if isinstance(dtype, str) else None

    # case 1: dtype is a custom supertype alias
    if dtype in custom_supertypes:
        return custom_supertypes[dtype]
    if lower and lower in custom_supertypes:
        return custom_supertypes[lower]

    # case 2: dtype is a default supertype alias
    if lower and lower in default_supertypes:
        return default_supertypes[lower]

    # case 3: dtype must be resolved
    dtype = _resolve_dtype(dtype)
    return default_supertypes.get(dtype, {dtype})


@cython.boundscheck(False)
@cython.wraparound(False)
@cython.nonecheck(False)
def subtypes(object dtype, bint exact = False) -> set[object]:
    """Return the set of subtypes associated with a given dtype.

    If `dtype` is a supertype specifier (`int`, `float`, `'i'`, `'datetime'`,
    etc.), then the returned set will contain all of the subtypes assigned to
    that supertype.  If `dtype` refers to an atomic type (`'i8'`, 'float32',
    `datetime.datetime`, etc.), then the returned set will include only the
    atomic type and no other elements.
    """
    return _subtypes(dtype, exact=exact)
