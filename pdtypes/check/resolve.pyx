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

from pdtypes import PYARROW_INSTALLED

from .supertypes cimport supertype_only


cdef dict custom_dtype_aliases = {
    # custom extensions to the numpy/pandas dtype() interface.  This table is
    # consulted before dtype() is called, allowing resolve_dtype() to catch
    # a wider range of type specifiers than numpy/pandas can by default.

    # integer
    int: int,  # remapped from int64
    "int": int,  # remapped from int64
    "integer": int,

    # float
    float: float,  # remapped from float64
    "float": float,  # remapped from float64
    "floating": float,
    "f": float,  # remapped from float32

    # complex
    complex: complex,  # remapped from complex128
    "complex": complex,  # remapped from complex128
    "c": complex,  # remapped from zero-terminated bytes ('S1')

    # decimal
    decimal.Decimal: decimal.Decimal,
    "decimal": decimal.Decimal,

    # datetime
    pd.Timestamp: pd.Timestamp,
    "datetime[pandas]": pd.Timestamp,
    datetime.datetime: datetime.datetime,
    "datetime[python]": datetime.datetime,
    np.datetime64: np.datetime64,
    "datetime[numpy]": np.datetime64,

    # timedelta
    pd.Timedelta: pd.Timedelta,
    "timedelta[pandas]": pd.Timedelta,
    datetime.timedelta: datetime.timedelta,
    "timedelta[python]": datetime.timedelta,
    np.timedelta64: np.timedelta64,
    "timedelta[numpy]": np.timedelta64,

    # periods - not attached to any supertype
    pd.Period: pd.Period,
    # pd.PeriodDtype(): pd.Period,  # throws an AttributeError on load
    "period": pd.Period,

    # intervals - not attached to any supertype
    pd.Interval: pd.Interval,
    pd.IntervalDtype(): pd.Interval,
    "interval": pd.Interval,

    # string
    "char": str,
    "character": str,

    # object
    "obj": object,
    "o": object  # lowercase
}


cdef dict default_dtype_aliases = {
    # maps numpy/pandas dtype objects to their corresponding atomic type.
    # This table is consulted after dtype() is called, allowing resolve_dtype()
    # to catch any type specifier that is accepted by numpy/pandas, provided
    # that specifier is not re-mapped in custom_dtype_aliases

    # bool
    np.dtype(bool): bool,
    pd.BooleanDtype(): bool,

    # integer
    np.dtype(np.int8): np.int8,
    np.dtype(np.int16): np.int16,
    np.dtype(np.int32): np.int32,
    np.dtype(np.int64): np.int64,
    np.dtype(np.uint8): np.uint8,
    np.dtype(np.uint16): np.uint16,
    np.dtype(np.uint32): np.uint32,
    np.dtype(np.uint64): np.uint64,
    pd.Int8Dtype(): np.int8,
    pd.Int16Dtype(): np.int16,
    pd.Int32Dtype(): np.int32,
    pd.Int64Dtype(): np.int64,
    pd.UInt8Dtype(): np.uint8,
    pd.UInt16Dtype(): np.uint16,
    pd.UInt32Dtype(): np.uint32,
    pd.UInt64Dtype(): np.uint64,

    # float
    np.dtype(np.float16): np.float16,
    np.dtype(np.float32): np.float32,
    np.dtype(np.float64): np.float64,
    np.dtype(np.longdouble): np.longdouble,

    # complex
    np.dtype(np.complex64): np.complex64,
    np.dtype(np.complex128): np.complex128,
    np.dtype(np.clongdouble): np.clongdouble,

    # datetime
    np.dtype(np.datetime64): np.datetime64,

    # timedelta
    np.dtype(np.timedelta64): np.timedelta64,

    # string
    np.dtype(str): str,
    pd.StringDtype("python"): str,

    # object
    np.dtype(object): object,

    # bytes
    np.dtype("S"): bytes,
    np.dtype("a"): bytes,
    np.dtype("V"): bytes
}
if PYARROW_INSTALLED:
    default_dtype_aliases[pd.StringDtype("pyarrow")] = str


@cython.boundscheck(False)
@cython.wraparound(False)
@cython.nonecheck(False)
cdef object _resolve_dtype(object dtype):
    """internal C interface for the public-facing resolve_dtype() function."""
    cdef str lower

    # case 1: dtype is a custom alias
    if dtype in custom_dtype_aliases:
        return custom_dtype_aliases[dtype]
    elif isinstance(dtype, str):
        lower = dtype.lower()
        if lower in custom_dtype_aliases:
            return custom_dtype_aliases[lower]
        if lower in supertype_only:
            raise ValueError(f"dtype {repr(dtype)} does not have an "
                             f"associated atomic type (supertype-only)")

    # case 2: dtype is abstract and must be parsed
    try:
        dtype = pd.api.types.pandas_dtype(dtype)

    # case 3: dtype can't be parsed, might be custom
    except TypeError as err:
        if isinstance(dtype, type):
            return dtype
        raise err

    # M8 and m8 must be handled separately due to differing units/step sizes
    if not pd.api.types.is_extension_array_dtype(dtype):  # would throw error
        if np.issubdtype(dtype, "M8"):
            return np.datetime64
        if np.issubdtype(dtype, "m8"):
            return np.timedelta64

    # check against default_dtype_aliases
    return default_dtype_aliases[dtype]


@cython.boundscheck(False)
@cython.wraparound(False)
@cython.nonecheck(False)
def resolve_dtype(object dtype) -> type:
    """Collapse an abstract dtype alias into its corresponding atomic type.

    Custom types are supported by default so long as they are passed as a
    direct reference and not as an alias, string or otherwise.  For instance,
    defining a custom class `Foo` and calling `resolve_dtype(Foo)` will return
    `Foo` unmodified, since the atomic type associated with the custom class
    `Foo` is `Foo` itself.  If `resolve_dtype()` is instead called on the
    string `'Foo'`, a TypeError will be raised, since the alias `'Foo'` is
    unrecognized.

    Parameters
    ----------
    dtype (object):
        An arbitrary dtype alias to be resolved.  Can be a raw atomic type
        (e.g. `int`, `float`, `str`, etc.), a preprocessed numpy/pandas dtype
        object (e.g. `np.dtype('int64')`, `pd.Int64Dtype()`, etc.), or a
        string identifier following the numpy array interface protocol
        ('i8', 'int64', 'f4', 'longdouble', etc.) with pandas extensions
        ('Int64', 'string[python]', etc.).

    Returns
    -------
    type:
        The atomic type associated with the given alias, if one exists.  This
        will always match the underlying element type of an array/series with
        dtype=`dtype`, as would be observed by the built-in `type` function.

    Raises
    ------
    ValueError:
        If `dtype` does not have an associated atomic type.  This can occur
        if `dtype` refers to a supertype-only alias ('i', 'u', 'datetime',
        'timedelta'), which are ambiguous by default.
    TypeError:
        If `dtype` is unrecognized and is not a custom atomic type.  See above.
    """
    return _resolve_dtype(dtype)
