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

import numpy as np
import pandas as pd

from pdtypes.util.array import vectorize
from pdtypes.util.type_hints import array_like, dtype_like, scalar

from .introspect import object_types
from .resolve import resolve_dtype
from .supertypes import subtypes, supertype


# TODO: change numpy M8 and m8 comparisons to include unit/step size info
#   -> if dtype has unit info, retrieve indices where type=np.datetime64 and
#   -> gather unit info.  Cast to set, then compare units present in array with
#   -> those given in `dtype`


def get_dtype(
    array: scalar | array_like,
    exact: bool = True
) -> type | set[type]:
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
        array = array.to_numpy() if isinstance(array, pd.Series) else array
        element_types = set(object_types(array, exact=exact))

        # return
        if len(element_types) == 1:  # as scalar
            return element_types.pop()
        return element_types

    # case 2: array has non-object dtype
    result = None
    if isinstance(array, pd.Series):
        # special cases for M8[ns], m8[ns], categorical, and period series
        if pd.api.types.is_datetime64_ns_dtype(array):
            result = pd.Timestamp
        elif pd.api.types.is_timedelta64_ns_dtype(array):
            result = pd.Timedelta
        elif pd.api.types.is_categorical_dtype(array):
            result = get_dtype(array.dtype.categories, exact=exact)
        elif pd.api.types.is_period_dtype(array):
            result = pd.Period
    if result is None:
        result = resolve_dtype(array.dtype)

    if not exact:
        return supertype(result)
    return result


def check_dtype(
    arg: scalar | array_like,
    dtype: dtype_like | array_like,
    exact: bool = False
) -> bool:
    """Check whether a scalar, sequence, array, or series contains elements
    of the given type/supertype.

    Parameters
    ----------
    arg (scalar | array_like):
        The value whose type will be checked.  If a scalar is provided, its
        type is checked directly.  If a sequence, array, or series is provided,
        then its unique element types are reduced to a set and checked
        collectively.
    dtype (dtype_like | array_like):
        The type to check against.  If multiple types are provided, then they
        are wrapped in a set and checked against the corresponding types in
        `arg`.  This function will return `True` if and only if the observed
        types in `arg` form a subset of those defined in `dtype`.
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
    observed = get_dtype(arg)
    if not isinstance(observed, set):  # coerce scalar to set of length 1
        observed = {observed}

    # resolve `dtype`
    if isinstance(dtype, (tuple, list, np.ndarray, pd.Series)):
        # merge into single set
        dtype = {subtype
                 for typespec in dtype
                 for subtype in subtypes(typespec, exact=exact)}
    else:
        dtype = subtypes(dtype, exact=exact)

    # expand `object` supertype, if present
    if not exact and object in dtype:
        custom_types = {o for o in observed if supertype(o) == object}
        dtype.remove(object)
        dtype.update(custom_types)

    return not observed - dtype


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
    # expand dtypes in `arg` to set of atomic types
    if isinstance(arg, (tuple, list, set, np.ndarray, pd.Series)):
        # merge into single set
        arg = {subtype
               for typespec in arg
               for subtype in subtypes(typespec, exact=exact)}
    else:
        arg = subtypes(arg, exact=exact)

    # expand `dtype` to set of atomic types
    if isinstance(dtype, (tuple, list, np.ndarray, pd.Series)):
        # merge into single set
        dtype = {subtype
                 for typespec in dtype
                 for subtype in subtypes(typespec, exact=exact)}
    else:
        dtype = subtypes(dtype, exact=exact)

    # expand `object` supertype, if present
    if not exact and object in dtype:
        custom_types = {a for a in arg if supertype(a) == object}
        dtype.remove(object)
        dtype.update(custom_types)

    return not arg - dtype






# for test purposes only

import datetime
import decimal

test = pd.Series([
    False,
    True,
    2,
    np.int8(3),
    np.int16(4),
    np.int32(5),
    np.int64(6),
    np.uint8(7),
    np.uint16(8),
    np.uint32(9),
    np.uint64(10),
    11.0,
    np.float16(12),
    np.float32(13),
    np.float64(14),
    np.longdouble(15),
    complex(16),
    np.complex64(17),
    np.complex128(18),
    np.clongdouble(19),
    decimal.Decimal(20),
    "21",
    None,
    np.nan,
    pd.NA
], dtype="O")
