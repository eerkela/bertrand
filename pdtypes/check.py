"""This module contains functions to get and check the dtypes present in numpy
arrays, pandas series, and built-in sequences (list, tuple, set).

It exposes 2 public-facing functions, `get_dtype()` and `check_dtype()`, which,
when taken together, allow for virtually arbitrary type detection and checking
on numpy arrays and related data structures.  They work on both explicitly-typed
data (with a specified, non-object `.dtype` field) and implicit equivalents
(with `dtype="O"`, or generic python sequences).

`get_dtype()` functions like the `.dtype` accessor, but returns the underlying
element type(s) present in the array, rather than a (sometimes) ambiguous
`numpy.dtype` object.

`check_dtype()` functions like the built-in `isinstance()` and `issubclass()`
functions, creating a generalized interface for type checks, which can even
include third-party or user-defined custom classes.  Checks can be concatenated
using the tuple syntax of the aforementioned built-ins, and a shorthand syntax
for commonly encountered supertype categories can be toggled on and off,
allowing for both coarse and fine control.

Using these functions, one can easily implement type introspection for almost
any kind of input data or type specification, with maximal abstraction.
"""
from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes.error import error_trace
from pdtypes.util.array import vectorize
from pdtypes.util.type_hints import array_like, atomic_type, dtype_like, scalar


# TODO: change numpy M8 and m8 comparisons to include unit/step size info
#   -> if dtype has unit info, retrieve indices where type=np.datetime64 and
#   -> gather unit info.  Cast to set, then compare units present in array with
#   -> those given in `dtype`
# TODO: verify support for period, interval (supertypes, aliases)


#############################
####    Lookup Tables    ####
#############################


custom_aliases = {  # applied before pandas.dtype() resolution
    # integer
    int: int,
    "int": int,
    "integer": int,

    # float
    float: float,
    "float": float,
    "floating": float,
    "f": float,

    # complex
    complex: complex,
    "complex": complex,
    "c": complex,

    # decimal
    decimal.Decimal: decimal.Decimal,
    "decimal": decimal.Decimal,
    "decimal.decimal": decimal.Decimal,

    # datetime
    pd.Timestamp: pd.Timestamp,
    "pd.timestamp": pd.Timestamp,
    "pandas.timestamp": pd.Timestamp,
    datetime.datetime: datetime.datetime,
    "pydatetime": datetime.datetime,
    "datetime.datetime": datetime.datetime,
    np.datetime64: np.datetime64,
    "np.datetime64": np.datetime64,
    "numpy.datetime64": np.datetime64,

    # timedelta
    pd.Timedelta: pd.Timedelta,
    "pd.timedelta": pd.Timedelta,
    "pandas.timedelta": pd.Timedelta,
    datetime.timedelta: datetime.timedelta,
    "pytimedelta": datetime.timedelta,
    "datetime.timedelta": datetime.timedelta,
    np.timedelta64: np.timedelta64,
    "np.timedelta64": np.timedelta64,
    "numpy.timedelta64": np.timedelta64,

    # periods
    pd.Period: pd.Period,
    # pd.PeriodDtype(): pd.Period,  # throws an AttributeError on load
    "period": pd.Period,

    # intervals
    pd.Interval: pd.Interval,
    pd.IntervalDtype(): pd.Interval,
    "interval": pd.Interval,

    # string
    "char": str,
    "character": str,

    # object
    "obj": object,
    "o": object,

    # missing values
    None: type(None),
    "none": type(None),
    "missing": type(None),
    np.nan: type(None),
    "nan": type(None),
    "np.nan": type(None),
    "numpy.nan": type(None),
    pd.NA: type(None),
    "na": type(None),
    "n/a": type(None),
    "pd.na": type(None),
    "pandas.na": type(None),
    pd.NaT: type(None),
    "nat": type(None),
    "pd.nat": type(None),
    "pandas.nat": type(None)
}


fallback_aliases = {  # applied after pandas.dtype() resolution
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
    pd.StringDtype(): str,

    # object
    np.dtype(object): object,

    # bytes
    np.dtype("S"): bytes,
    np.dtype("a"): bytes,
    np.dtype("V"): bytes
}


custom_supertype_aliases = {  # applied before resolve_dtype()
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
    np.complexfloating: {np.complex64, np.complex128, np.clongdouble},
}


fallback_supertype_aliases = {  # applied after resolve_dtype()
    # integer
    int: {int, np.int8, np.int16, np.int32, np.int64, np.uint8, np.uint16,
          np.uint32, np.uint64},

    # float
    float: {float, np.float16, np.float32, np.float64, np.longdouble},

    # complex
    complex: {complex, np.complex64, np.complex128, np.clongdouble},

    # datetime
    "datetime": {pd.Timestamp, datetime.datetime, np.datetime64},

    # timedelta
    "timedelta": {pd.Timedelta, datetime.timedelta, np.timedelta64}

    # object supertype expanded at runtime
}


atomic_to_extension_type = {  # atomic type to associated extension type
    # boolean
    bool: pd.BooleanDtype(),

    # integer
    np.int8: pd.Int8Dtype(),
    np.int16: pd.Int16Dtype(),
    np.int32: pd.Int32Dtype(),
    np.int64: pd.Int64Dtype(),
    np.uint8: pd.UInt8Dtype(),
    np.uint16: pd.UInt16Dtype(),
    np.uint32: pd.UInt32Dtype(),
    np.uint64: pd.UInt64Dtype(),

    # string
    str: pd.StringDtype()
}


atomic_to_supertype = {  # atomic type to associated supertype
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
    str: str,

    # bytes
    bytes: bytes,

    # missing values
    type(None): type(None)
}


def extension_type(
    dtype: dtype_like
) -> type | pd.api.extensions.ExtensionDtype:
    """Essentially an interface for the _atomic_to_extension_type lookup table.
    """
    return atomic_to_extension_type.get(resolve_dtype(dtype), dtype)


def resolve_dtype(dtype: dtype_like) -> type:
    """Collapse abstract dtype aliases into their corresponding atomic type.

    Essentially an interface to the `custom_aliases` and `fallback_aliases`
    lookup tables.
    """
    # case 1: `dtype` is a custom alias
    if dtype in custom_aliases:
        return custom_aliases[dtype]
    elif isinstance(dtype, str) and dtype.lower() in custom_aliases:
        return custom_aliases[dtype.lower()]

    # possible exception: dtype is ambiguous
    if dtype in ("i", "u"):  # ambiguous without associated bit size
        bad = "signed" if dtype == "i" else "unsigned"
        err_msg = (f"[{error_trace()}] {bad} integer alias {repr(dtype)} is "
                   f"ambiguous.  Use a specific bit size or generalized `int` "
                   f"instead.")
        raise ValueError(err_msg)

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

    # check against fallback_aliases lookup table
    return fallback_aliases[dtype]


def supertype(dtype: dtype_like) -> type | str:
    """Essentially an interface for the atomic_to_supertype lookup table."""
    return atomic_to_supertype.get(resolve_dtype(dtype), object)


#################################
####    Utility Functions    ####
#################################


def object_types(
    series: np.ndarray | pd.Series,
    supertypes: bool = False
) -> np.ndarray | pd.Series:
    """Get the type of each element in a given object series."""
    # case 1: series is array-like and has dtype="O"
    if pd.api.types.is_object_dtype(series):
        if supertypes:
            lookup = lambda x: supertype(x if pd.isna(x) else type(x))
            return np.frompyfunc(lookup, 1, 1)(series)
        return np.frompyfunc(type, 1, 1)(series)

    # case 2: series is array-like and has non-nullable dtype
    dtype = get_dtype(series)
    is_series = isinstance(series, pd.Series)
    if (is_dtype(dtype, (int, bool)) and
        not pd.api.types.is_extension_array_dtype(series)):
        if supertypes:
            series = np.full(series.shape, supertype(dtype), dtype="O")
        else:
            series = np.full(series.shape, dtype, dtype="O")
        return pd.Series(series) if is_series else series

    # case 3: series is array-like and has nullable dtype
    na_types = {
        bool: type(pd.NA),
        int: type(pd.NA),
        float: float,
        complex: complex,
        "datetime": type(pd.NaT),
        "timedelta": type(pd.NaT),
        str: type(pd.NA)
    }
    if supertypes:
        series = np.where(pd.isna(series), supertype(None), supertype(dtype))
    else:
        series = np.where(pd.isna(series), na_types[supertype(dtype)], dtype)
    return pd.Series(series) if is_series else series


##############################
####    Core Functions    ####
##############################


def check_dtype(
    arg: scalar | dtype_like | array_like,
    dtype: dtype_like | array_like,
    exact: bool = False
) -> bool:
    """Check whether a scalar, sequence, array, or series contains elements
    of the given type or supertype.

    Parameters
    ----------
    arg (scalar | dtype_like | array_like):
        The value whose type will be checked.  If a scalar is provided, its
        type is checked directly.  If a sequence, array, or series is provided,
        then its unique element types are checked collectively.
    dtype (dtype_like | array_like):
        The dtype to check against.  If a sequence, set, array, or series is
        given, then it is interpreted in the same fashion as `isinstance`.
        Namely, this function will return `True` if `arg` contains one or more
        of the given dtypes.
    exact (bool):
        Controls whether to expand supertypes contained in `dtype` into their
        constituents during comparison.  If this is `False`, supertypes are
        interpreted as-is (i.e. not expanded), and will only match objects that
        are of the equivalent type.  For instance, `int` will only match
        actual, built-in python integers rather than numpy integers or their
        tensorflow counterparts.

    Returns
    -------
    bool: `True` if the element types of `arg` are a subset of those defined in
        `dtype`.  `False` otherwise.

    Raises
    ------
    TypeError
        If any of the dtype specifications in `dtype` do not correspond to a
        recognized atomic type, an associated alias, or a supertype with
        `exact=False`.
    ValueError
        If `dtype` is of a form that could be interpreted by the `numpy.dtype`
        constructor, but is malformed in some way.  For more detail, see the
        [numpy documentation](https://numpy.org/doc/stable/reference/arrays.dtypes.html)
        for all the ways in which dtype objects can be created.

    Notes
    -----
    This is essentially the equivalent of the built-in `isinstance` and
    `issubclass` functions, as applied to arrays and their contents.  It
    supports a similar interface, including an allowance for sequence-based
    multiple comparison just like the aforementioned functions.  In its base
    form, it can be used for quick and easy schema validation with a
    generalized framework similar to the built-in analogues.

    The specificity of this comparison can be tuned via the `exact` argument,
    which controls the expansion of supertypes into their constituent subtypes.
    When `exact=False`, the following conversions are performed, generalizing
    commonly encountered data types into their most abstract forms, as follows:
        - `int` -> `(int, np.int8, np.int16, np.int32, np.int64, np.uint8,
            np.uint16, np.uint32, np.uint64)`
        - `np.integer` -> `(np.int8, np.int16, np.int32, np.int64, np.uint8,
            np.uint16, np.uint32, np.uint64)`
        - `'i'`/`signed` -> `(int, np.int8, np.int16, np.int32, np.int64)`
        - `np.signedinteger` -> `(np.int8, np.int16, np.int32, np.int64)`
        - `'u'`/`'unsigned'`/`np.unsignedinteger` -> `(np.uint8, np.uint16,
            np.uint32, np.uint64)`
        - `float` -> `(float, np.float16, np.float32, np.float64,
            np.longdouble)`
        - `np.floating` -> `(np.float16, np.float32, np.float64, np.longdouble)`
        - `complex` -> `(complex, np.complex64, np.complex128, np.clongdouble)`
        - `np.complexfloating` -> `(np.complex64, np.complex128,
            np.clongdouble)`
        - `'datetime'` -> `(pd.Timestamp, datetime.datetime, np.datetime64)`
        - `'timedelta'` -> `(pd.Timedelta, datetime.timedetla, np.timedelta64)`
        - `object` -> catch-all matching any custom third-party type definition
            that is present in `arg`

    If any of the types specified in `dtype` are coercible into one of the
    aforementioned supertypes (as is the case for the `'int'`, `'float'`,
    `'c'`, and `'O'` aliases, etc.), then they resolved before being expanded.

    If this behavior is undesirable, it can be disabled by setting
    `exact=True`, which interprets each dtype as-is, without expanding to
    include any subtypes.  Having a togglable switch for this enables both
    generalized categorization (does this array contain integers?) and fine
    comparison (does this array contain specifically 8-bit, unsigned integers
    with no missing values?) under the same interface and architecture.
    Combined, this effectively replaces the following boolean type check
    functions, found under `pd.api.types`:
        - `pd.api.types.is_bool_dtype(series)` -> `check_dtype(series, bool)`
        - `pd.api.types.is_integer_dtype(series)` -> `check_dtype(series, int)`
        - `pd.api.types.is_signed_integer_dtype(series)` ->
            `check_dtype(series, 'i')`
        - `pd.api.types.is_unsigned_integer_dtype(series)` ->
            `check_dtype(series, 'u')`
        - `pd.api.types.is_int64_dtype(series)` -> `check_dtype(series, 'i8')`
        - `pd.api.types.is_float_dtype(series)` -> `check_dtype(series, float)`
        - `pd.api.types.is_complex_dtype(series)` ->
            `check_dtype(series, complex)`
        - `pd.api.types.is_numeric_dtype(series)` ->
            `check_dtype(series, (int, float, complex, 'decimal'))`
        - `pd.api.types.is_datetime64_dtype(series)` ->
            `check_dtype(series, 'datetime')`
        # - `pd.api.types.is_datetime64_ns_dtype(series)` ->
        #     `check_dtype(series, (pd.Timestamp, 'M8[ns]')`
        - `pd.api.types.is_timedelta64_dtype(series)` ->
            `check_dtype(series, 'timedelta')`
        # - `pd.api.types.is_timedelta64_ns_dtype(series)` ->
        #     `check_dtype(series, (pd.Timedelta, 'm8[ns]'))`
        - `pd.api.types.is_string_dtype(series)` -> `check_dtype(series, str)`
        - `pd.api.types.is_period_dtype(series)` ->
            `check_dtype(series, pd.Period)`

    In many cases, the `check_dtype` formulations are even more generally
    applicable than the pandas equivalents.  For one, they apply equally to
    both explicitly-typed arrays with a well-defined `.dtype` field, and also
    to generic sequences and pyObject arrays (`dtype='O'`).  In addition, the
    string-specific comparison now properly excludes genuine object arrays,
    which the default pandas equivalent does not.  Similarly, the `object`
    dtype is restricted to only match those arrays that contain undefined,
    third-party type definitions, which are supported by default under this
    framework.

    Lastly, if the underlying array is composed of mixed types (both integer
    and float, for instance), then this function will return False for any
    `dtype` specification which does not include at least those element types.
    In other words, the given `dtype` must fully encapsulate the types that are
    present in `array` for this function to return `True`.
    """
    # get unique element types contained in `arg`
    observed = set(vectorize(get_dtype(arg)))

    # get elementwise resolution function for `dtype`
    if exact:
        resolve_ufunc = np.frompyfunc(resolve_dtype, 1, 1)
        resolve = lambda x: set(resolve_ufunc(vectorize(x)))
    else:
        def resolve_supertype(element: dtype_like) -> set[atomic_type]:
            # 1st lookup pass -> element is a pre-defined supertype alias
            if element in custom_supertype_aliases:  # catches 'i', 'u', etc.
                return custom_supertype_aliases[element]

            # 2nd lookup pass -> resolve before searching for supertype alias
            resolved = resolve_dtype(element)
            if resolved == object and custom_types:  # object supertype
                return custom_types  # set of unrecognized types in `arg`
            return fallback_supertype_aliases.get(resolved, {resolved})

        custom_types = {o for o in observed if o not in atomic_to_supertype}
        resolve_ufunc = np.frompyfunc(resolve_supertype, 1, 1)
        resolve = lambda x: set().union(*resolve_ufunc(vectorize(x)))

    # Set comparison.  Return True if `observed` is a subset of `dtype`
    return not observed - resolve(dtype)


def get_dtype(
    array: scalar | array_like
) -> atomic_type | tuple[atomic_type, ...]:
    """Retrieve the common atomic element types stored in `array`.

    This function operates in a manner similar to `pd.api.types.infer_dtype()`,
    but is more generalized and direct in its approach.  Rather than returning
    a string identifier for the common array element types, this will return
    the underlying types themselves.  This gives the following mapping from the
    results of `pd.api.types.infer_dtype()`:
        - `'string'` -> `str`
        - `'bytes'` -> `bytes`
        - `'floating'` -> `float`
        - `'mixed-integer'` -> `(int, x1, x2, ...)`, where x1, x2, ... can be
            one or more additional atomic types
        - `'mixed-integer-float'` -> `(int, float)`
        - `'decimal'` -> `decimal.Decimal`
        - `'complex'` -> `complex`
        - `'categorical'` -> N/A (replaced by underlying category types)
        - `'boolean'` -> `bool`
        - `'datetime64'` -> `pd.Timestamp` or `np.datetime64`, based on
            which are present in the array.
        - `datetime` -> `pd.Timestamp`, `datetime.datetime`, or
            `np.datetime64`, based on which are present in the array.
        - `date` -> `datetime.date`.
        - `timedelta64` -> `pd.Timedelta` or `np.timedelta64`, based on
            which are present in the array.
        - `timedelta` -> `pd.Timedelta`, `datetime.timedelta`, or
            `np.timedelta64`, based on which are present in the array.
        - `time` -> `datetime.time`.
        - `period` -> `pd.Period`
        - `mixed` -> N/A (replaced by exact element types)
        - `unknown-array` -> N/A (replaced by exact element types)

    This approach works even in the case of generic python scalars and
    sequences, such as lists, sets, and tuples, or `np.ndarray`/`pd.Series`
    instances with `dtype='O'`.  If multiple types are present in the array,
    the returned result will be a tuple concatenation of all the types that are
    present in the array.  If the array contains custom user-defined or third
    party class definitions, then they are returned as represented in the array.
    This allows for almost endless extension, as well as default coverage of
    the most common data types that are stored in numpy arrays and pandas
    series'.

    This function attempts to be as precise as possible.  If the given array
    contains missing values (defined in relation to `pd.isna()`), then any
    non-nullable element types (such as `np.int64`, `bool`, `str`, etc.) will
    be replaced with their pandas extension analogues.  For instance,
    `get_dtype([np.int64(1), None])` will return the `pd.Int64Dtype()`
    extension type rather than `np.int64` directly.
    """
    #  vectorize scalar and sequence input
    array = vectorize(array)

    # case 1: array has dtype="O" -> scan elementwise
    if pd.api.types.is_object_dtype(array):
        array = array[pd.notna(array)]  # disregard missing values
        if len(array) == 0:  # trivial case: empty array
            return None

        # get unique element types
        types = pd.unique(object_types(array, supertypes=False))

        # return
        if len(types) == 1:  # as scalar
            return types[0]
        return tuple(types)  # as tuple

    # case 2: array has non-object dtype
    if isinstance(array, pd.Series):
        # special cases for M8[ns], m8[ns], categorical, and period series
        if pd.api.types.is_datetime64_ns_dtype(array):
            return pd.Timestamp
        if pd.api.types.is_timedelta64_ns_dtype(array):
            return pd.Timedelta
        if pd.api.types.is_categorical_dtype(array):  # get category types
            return get_dtype(array.dtype.categories)
        if pd.api.types.is_period_dtype(array):
            return pd.Period
    return resolve_dtype(array.dtype)


def is_dtype(
    arg: dtype_like | array_like,
    dtype: dtype_like | array_like,
    exact: bool = False
) -> bool:
    """_summary_

    Args:
        arg (dtype_like | array_like): _description_
        dtype (dtype_like | array_like): _description_
        exact (bool, optional): _description_. Defaults to False.

    Returns:
        bool: _description_
    """
    # get elementwise resolution function
    if exact:
        resolve_ufunc = np.frompyfunc(resolve_dtype, 1, 1)
        resolve = lambda x: set(resolve_ufunc(vectorize(x)))
    else:
        def resolve_supertype(element: dtype_like) -> set[atomic_type]:
            # 1st lookup pass -> element is a pre-defined supertype alias
            if element in custom_supertype_aliases:  # catches 'i', 'u', etc.
                return custom_supertype_aliases[element]

            # 2nd lookup pass -> resolve before searching for supertype alias
            resolved = resolve_dtype(element)
            if resolved == object and custom_types:  # object supertype
                return custom_types  # set of unrecognized types in `arg`
            return fallback_supertype_aliases.get(resolved, {resolved})

        custom_types = set()
        resolve_ufunc = np.frompyfunc(resolve_supertype, 1, 1)
        resolve = lambda x: set().union(*resolve_ufunc(vectorize(x)))

    try:  # case 1: `arg` contains resolvable dtype-like elements
        observed = resolve(arg)
    except (TypeError, ValueError):  # case 2: `arg` contains scalars
        return False

    if not exact:
        custom_types = {o for o in observed if o not in atomic_to_supertype}
    return not observed - resolve(dtype)
