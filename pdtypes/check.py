from __future__ import annotations
import datetime
import decimal
from typing import Any, Union

import numpy as np
import pandas as pd

from pdtypes.error import error_trace
from pdtypes.util.array import array_like, object_types, vectorize


# TODO: change numpy M8 and m8 comparisons to include unit/step size info
#   -> if dtype has unit info, retrieve indices where type=np.datetime64 and
#   -> gather unit info.  Cast to set, then compare units present in array with
#   -> those given in `dtype`
# TODO: verify support for period, interval (supertypes, aliases)


##########################
####    Type Hints    ####
##########################


atomic_type = Union[type, pd.api.extensions.ExtensionDtype]
dtype_like = Union[type, str, np.dtype, pd.api.extensions.ExtensionDtype]
scalar = Any


#############################
####    Lookup Tables    ####
#############################


_type_aliases = {  # dtype alias to atomic type
    # booleans
    np.dtype(bool): bool,
    pd.BooleanDtype(): pd.BooleanDtype(),

    # integers
    int: int,
    "int": int,
    "integer": int,
    np.dtype("i1"): np.int8,
    np.dtype("i2"): np.int16,
    np.dtype("i4"): np.int32,
    np.dtype("i8"): np.int64,
    np.dtype("u1"): np.uint8,
    np.dtype("u2"): np.uint16,
    np.dtype("u4"): np.uint32,
    np.dtype("u8"): np.uint64,
    pd.Int8Dtype(): pd.Int8Dtype(),
    pd.Int16Dtype(): pd.Int16Dtype(),
    pd.Int32Dtype(): pd.Int32Dtype(),
    pd.Int64Dtype(): pd.Int64Dtype(),
    pd.UInt8Dtype(): pd.UInt8Dtype(),
    pd.UInt16Dtype(): pd.UInt16Dtype(),
    pd.UInt32Dtype(): pd.UInt32Dtype(),
    pd.UInt64Dtype(): pd.UInt64Dtype(),

    # floats
    float: float,
    "float": float,
    "floating": float,
    "f": float,
    np.dtype("f2"): np.float16,
    np.dtype("f4"): np.float32,
    np.dtype("f8"): np.float64,
    np.dtype(np.longdouble): np.longdouble,  # platform-specific

    # complex numbers
    complex: complex,
    "complex": complex,
    "c": complex,
    np.dtype("c8"): np.complex64,
    np.dtype("c16"): np.complex128,
    np.dtype(np.clongdouble): np.clongdouble,  # platform-specific

    # decimals
    decimal.Decimal: decimal.Decimal,
    "decimal": decimal.Decimal,
    "decimal.decimal": decimal.Decimal,
    "d": decimal.Decimal,

    # datetimes
    pd.Timestamp: pd.Timestamp,
    "pd.timestamp": pd.Timestamp,
    "pandas.timestamp": pd.Timestamp,
    datetime.datetime: datetime.datetime,
    "pydatetime": datetime.datetime,
    "datetime.datetime": datetime.datetime,
    np.datetime64: np.datetime64,
    "np.datetime64": np.datetime64,
    "numpy.datetime64": np.datetime64,

    # timedeltas
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

    # objects
    np.dtype(object): object,
    "obj": object,
    "o": object,

    # strings
    np.dtype(str): str,
    pd.StringDtype(): pd.StringDtype()
}


_extension_types = {  # atomic type to associated extension type
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


_supertype = {  # atomic type to associated supertype
    # booleans
    bool: bool,
    pd.BooleanDtype(): bool,

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
    pd.Int8Dtype(): int,
    pd.Int16Dtype(): int,
    pd.Int32Dtype(): int,
    pd.Int64Dtype(): int,
    pd.UInt8Dtype(): int,
    pd.UInt16Dtype(): int,
    pd.UInt32Dtype(): int,
    pd.UInt64Dtype(): int,

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
    "datetime": "datetime",
    pd.Timestamp: "datetime",
    datetime.datetime: "datetime",
    np.datetime64: "datetime",

    # timedeltas
    "timedelta": "timedelta",
    pd.Timedelta: "timedelta",
    datetime.timedelta: "timedelta",
    np.timedelta64: "timedelta",

    # strings
    str: str,
    pd.StringDtype(): str
}


_subtypes = {  # supertype to associated atomic types
    # boolean
    bool: {bool, pd.BooleanDtype()},

    # integer
    int: {int, np.int8, np.int16, np.int32, np.int64, np.uint8, np.uint16,
          np.uint32, np.uint64, pd.Int8Dtype(), pd.Int16Dtype(),
          pd.Int32Dtype(), pd.Int64Dtype(), pd.UInt8Dtype(), pd.UInt16Dtype(),
          pd.UInt32Dtype(), pd.UInt64Dtype()},
    "i": {int, np.int8, np.int16, np.int32, np.int64, pd.Int8Dtype(),
          pd.Int16Dtype(), pd.Int32Dtype(), pd.Int64Dtype()},
    "u": {np.uint8, np.uint16, np.uint32, np.uint64, pd.UInt8Dtype(),
          pd.UInt16Dtype(), pd.UInt32Dtype(), pd.UInt64Dtype()},
    np.int8: {np.int8, pd.Int8Dtype()},
    np.int16: {np.int16, pd.Int16Dtype()},
    np.int32: {np.int32, pd.Int32Dtype()},
    np.int64: {np.int64, pd.Int64Dtype()},
    np.uint8: {np.uint8, pd.UInt8Dtype()},
    np.uint16: {np.uint16, pd.UInt16Dtype()},
    np.uint32: {np.uint32, pd.UInt32Dtype()},
    np.uint64: {np.uint64, pd.UInt64Dtype()},

    # float
    float: {float, np.float16, np.float32, np.float64, np.longdouble},

    # complex
    complex: {complex, np.complex64, np.complex128, np.clongdouble},

    # datetime
    "datetime": {pd.Timestamp, datetime.datetime, np.datetime64},

    # timedelta
    "timedelta": {pd.Timedelta, datetime.timedelta, np.timedelta64},

    # object supertype must be expanded manually, catching 3rd-party type defs

    # string
    str: {str, pd.StringDtype()}
}


#################################
####    Utility Functions    ####
#################################


def resolve_dtype(dtype: dtype_like) -> atomic_type:
    """Collapse abstract dtype aliases into their corresponding atomic type.

    Essentially an interface to _type_aliases lookup table.
    """
    # case 1: dtype is directly present in aliases
    if dtype in _type_aliases:
        return _type_aliases[dtype]

    # case 2: dtype is a specified (case-insensitive) string dtype alias
    if isinstance(dtype, str) and dtype.lower() in _type_aliases:
        return _type_aliases[dtype.lower()]

    # case 3: dtype is abstract and must be parsed
    if dtype in ("i", "u"):  # ambiguous without associated bit size
        bad = "signed" if dtype == "i" else "unsigned"
        err_msg = (f"[{error_trace()}] {bad} integer alias {repr(dtype)} is "
                   f"ambiguous.  Use a specific bit size or generalized `int` "
                   f"instead.")
        raise ValueError(err_msg)
    if isinstance(dtype, (np.bool_, np.integer, np.floating, np.complex_,
                          np.datetime64, np.timedelta64)):
        # pd.api.types.pandas_dtype() does not always throw an error on scalar
        # input, so we have to do this manually.
        err_msg = (f"[{error_trace()}] `dtype` must be a numpy/pandas dtype "
                   f"specification or an associated alias, not a scalar of "
                   f"type {type(dtype)}")
        raise TypeError(err_msg)
    try:  # resolve directly
        dtype = pd.api.types.pandas_dtype(dtype)
    except TypeError as err:  # dtype cannot be resolved, might be custom
        if isinstance(dtype, type):  # element is 3rd-party type def
            return dtype
        raise err

    # M8 and m8 must be handled separately due to differing units/step sizes
    is_extension = pd.api.types.is_extension_array_dtype(dtype)
    if not is_extension and np.issubdtype(dtype, "M8"):
        return np.datetime64
    if not is_extension and np.issubdtype(dtype, "m8"):
        return np.timedelta64
    return _type_aliases[dtype]


##############################
####    Core Functions    ####
##############################


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
        # disregard missing values
        nans = pd.isna(array)
        array = array[~nans]
        if len(array) == 0:  # trivial case: empty array
            return None

        # get element types, converting to extension if nans were detected
        types = pd.unique(object_types(array))
        if nans.any():
            to_extension = lambda d: _extension_types.get(d, d)
            types = np.frompyfunc(to_extension, 1, 1)(types)

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


def check_dtype(
    array: scalar | dtype_like | array_like,
    dtype: dtype_like | tuple,
    exact: bool = False
) -> bool:
    """Check whether an array contains elements of the given type.

    This is essentially the equivalent of the built-in `isinstance` and
    `issubclass` functions, as applied to arrays and their contents.  It
    supports a similar interface, allowing for tuple-based multiple comparison
    just like the aforementioned functions.  In its base form, it can be used
    for quick and easy schema validation with a generalized framework similar
    to the built-in analogues.

    The specificity of this comparison can be tuned via the `exact` argument,
    which controls the expansion of supertypes into their constituent subtypes.
    When `exact=False`, the following conversions are performed, generalizing
    commonly encountered data types into their most abstract forms as follows:
        - `bool` -> `(bool, pd.BooleanDtype())`
        - `int` -> `(int, np.int8, np.int16, np.int32, np.int64, np.uint8,
            np.uint16, np.uint32, np.uint64, pd.Int8Dtype(), pd.Int16Dtype(),
            pd.Int32Dtype(), pd.Int64Dtype(), pd.UInt8Dtype(), pd.UInt16Dtype(),
            pd.UInt32Dtype(), pd.UInt64Dtype())`
        - `float` -> `(float, np.float16, np.float32, np.float64,
            np.longdouble)`
        - `complex` -> `(complex, np.complex64, np.complex128, np.clongdouble)`
        - `'datetime'` -> `(pd.Timestamp, datetime.datetime, np.datetime64)`
        - `'timedelta'` -> `(pd.Timedelta, datetime.timedetla, np.timedelta64)`
        - `object` -> catch-all matching any custom third-party type definition
        - `str` -> `(str, pd.StringDtype())`

    If any of the types specified in `dtype` are coercible into one of the
    aforementioned supertypes (as is the case for the `'int'`, `'float'`, and
    `'complex'` aliases, etc.), then they are treated as such and expanded
    along with them.  Additionally, non-nullable integer types (such as `'i8'`,
    `'u1'`, np.int16, etc.) are expanded to include their nullable counterparts.
    If this behavior is undesirable, it can be disabled by setting
    `exact=True`, which interprets each dtype as-is, without expanding to
    include any subtypes.

    Having a togglable switch for this enables both generalized categorization
    (does this array contain integers?) and fine comparison (does this array
    contain specifically 8-bit, unsigned integers with no missing values?)
    under the same interface and architecture.  Combined, this effectively
    replaces the following boolean comparison functions, found under
    `pd.api.types`:
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
    to generic sequences and object arrays (`dtype='O'`).  In addition, the
    string-specific comparison now properly excludes genuine object arrays,
    which the default pandas equivalent does not.  Similarly, the `object`
    dtype is restricted to only match those arrays that contain undefined,
    third-party type definitions, which are supported by default under this
    framework.

    Lastly, if the underlying array is composed of mixed types (both integer
    and float, for instance), then this function will return False for any
    `dtype` specification which does not include at least those element types.
    The given `dtype` must be at least as general as the types contained in
    `array`.
    """
    def resolve_and_expand(element: dtype_like) -> set[atomic_type]:
        if element in _subtypes:  # element is a defined supertype
            return _subtypes[element]
        resolved = resolve_dtype(element)
        if resolved == object and custom_types:  # object supertype
            return custom_types
        return _subtypes.get(resolved, {resolved})

    # vectorized dtype resolution funcs, with and without supertype expansion
    custom_types = None
    resolve = np.frompyfunc(resolve_dtype, 1, 1)  # vectorized dtype resolution
    resolve_and_expand = np.frompyfunc(resolve_and_expand, 1, 1)

    # resolve observed dtypes from `array` and convert to set
    try:  # case 1: `array` contains dtype-like objects -> resolve directly
        if exact:
            observed = set(resolve(vectorize(array)))
        else:
            observed = set().union(*resolve_and_expand(vectorize(array)))
    except TypeError:  # case 2: `array` contains scalars -> interpret
        observed = set(vectorize(get_dtype(array)))

    # resolve `dtype` aliases and convert to set
    if exact:  # resolve directly
        dtype = set(resolve(vectorize(dtype)))
    else:  # expand supertypes during alias resolution
        # make a note of unrecognized object types.  These will be substituted
        # in to account for the `object` supertype, if it is provided.
        custom_types = {o for o in observed if o not in _supertype}
        dtype = set().union(*resolve_and_expand(vectorize(dtype)))

    # return overlap exists, ensuring `dtype`` is more general than `observed`
    if observed - dtype:  # types present in `array` are missing from `dtype`
        return False
    return len(dtype.intersection(observed)) > 0
