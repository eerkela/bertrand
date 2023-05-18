.. currentmodule:: pdcast

Usage
=====
Below is a cheat sheet for common ``pdcast`` operations.

Specifying Types
----------------
Types can be constructed manually using :func:`resolve_type`.  There are 3 ways
to do this:

    #.  By providing a string in the
        :ref:`type specification mini language <resolve_type.mini_language>`.
    #.  By providing a numpy ``dtype`` or pandas ``ExtensionDtype`` object.
    #.  By providing a python class object (e.g. ``int``, ``float``, ``str``,
        etc.)

.. doctest::

    >>> import numpy as np
    >>> import pandas as pd
    >>> import pdcast

    >>> class CustomObj:
    ...     def __init__(self, x):  self.x = x

    # string
    >>> pdcast.resolve_type("int64")
    Int64Type()
    >>> pdcast.resolve_type("float[python]")
    PythonFloatType()
    >>> pdcast.resolve_type("datetime[pandas, US/Pacific]")
    PandasTimestampType(tz=<DstTzInfo 'US/Pacific' LMT-1 day, 16:07:00 STD>)

    # dtype
    >>> pdcast.resolve_type(np.dtype("i4"))
    NumpyInt32Type()
    >>> pdcast.resolve_type(np.dtype("M8[30s]"))
    NumpyDatetime64Type(unit='s', step_size=30)
    >>> pdcast.resolve_type(pd.BooleanDtype())
    PandasBooleanType()
    >>> pdcast.resolve_type(pd.DatetimeTZDtype(tz="US/Pacific"))
    PandasTimestampType(tz=<DstTzInfo 'US/Pacific' LMT-1 day, 16:07:00 STD>)

    # class object
    >>> pdcast.resolve_type(int)
    PythonIntegerType()
    >>> pdcast.resolve_type(np.float32)
    NumpyFloat32Type()
    >>> pdcast.resolve_type(CustomObj)
    ObjectType(type_def=<class 'CustomObj'>)

This function is called automatically whenever a type specifier is supplied to
a ``pdcast`` function or method.

Adapters (most commonly :class:`sparse <SparseType>`\ /
:class:`categorical <CategoricalType>` types) can be added by providing an
appropriate ``ExtensionDtype`` or nesting specifiers in the
:ref:`type specification mini-language <resolve_type.mini_language>`.

.. doctest::

    >>> pdcast.resolve_type(pd.SparseDtype(bool))
    SparseType(wrapped=NumpyBooleanType(), fill_value=False)
    >>> pdcast.resolve_type("categorical[str]")
    CategoricalType(wrapped=StringType(), levels=None)

Additionally, types can be composited together by separating them with commas
in the :ref:`type specification mini language <resolve_type.mini_language>` or
concatenating them into a list, set, or other sequence:

.. doctest::

    >>> pdcast.resolve_type("bool, int, float")   # doctest: +SKIP
    CompositeType({bool, int, float})
    >>> pdcast.resolve_type([np.float32, "datetime", pd.UInt8Type()])   # doctest: +SKIP
    CompositeType({float32[numpy], datetime, uint8[pandas]})

Note that :class:`AtomicTypes <AtomicType>` are flyweights and therefore
immutable.

.. doctest::

    >>> pdcast.resolve_type("int") is pdcast.resolve_type("int")
    True
    >>> pdcast.resolve_type("int").x = 2
    Traceback (most recent call last):
        ...
    AttributeError: AtomicType objects are read-only

.. note::

    Python class objects are resolved *literally*.  This differs from the
    standard ``np.dtype`` or ``pd.api.types.pandas_dtype`` pattern, which maps
    built-in python types to fixed-width alternatives, which may not be
    precisely equal.

    .. doctest::

        >>> np.dtype(int)
        dtype('int64')
        >>> pdcast.resolve_type(int)
        PythonIntegerType()

Detecting Types
---------------
Types can also be detected from example data in a variety of formats using
:func:`detect_type`.  These can be:

    #.  Scalars.
    #.  List-like sequences or iterables.
    #.  Array-like objects with an appropriate ``.dtype`` field.

.. doctest::

    >>> import numpy as np
    >>> import pandas as pd
    >>> import pdcast

    >>> class CustomObj:
    ...     def __init__(self, x):  self.x = x

    # scalar
    >>> pdcast.detect_type(True)
    PythonBooleanType()
    >>> pdcast.detect_type(np.int32(1))
    NumpyInt32Type()
    >>> pdcast.detect_type("foo")
    StringType()
    >>> pdcast.detect_type(CustomObj("bar"))
    ObjectType(type_def=<class 'CustomObj'>)
    
    # list-like
    >>> pdcast.detect_type([1, 2, 3])
    PythonIntegerType()
    >>> pdcast.detect_type(("foo", "bar", "baz"))
    StringType()
    >>> pdcast.detect_type(x for x in [1.0, 2.0, 3.0])
    PythonFloatType()

    # array-like
    >>> pdcast.detect_type(np.arange(10))
    NumpyInt64Type()
    >>> pdcast.detect_type(pd.Index(["foo", "bar", "baz"]))
    StringType()
    >>> pdcast.detect_type(pd.Series([1, 2, 3], dtype="M8[ns]"))
    PandasTimestampType(tz=None)
    >>> pdcast.detect_type(pd.Series([1, 2, 3], dtype="Sparse[int]"))
    SparseType(wrapped=NumpyInt64Type(), fill_value=0)

If an array is given and its ``dtype`` is not ``dtype: object``, then it will
be parsed directly.  This is an *O(1)* operation regardless of how many
elements are contained in the array.

Additionally, if a vector contains elements of multiple types, then a
:class:`CompositeType` will be returned describing each type that is present
within the vector.

.. doctest::

    >>> pdcast.detect_type([False, 1, "2"])   # doctest: +SKIP
    CompositeType({bool[python], int[python], string[python]})

Type Checks
-----------
The resolution and detection tools above can be combined to perform explicit
:func:`typechecks <typecheck>`.  This searches a type's subtype/backend tree
for abstract membership.  It resembles the built-in ``isinstance()`` function.

.. doctest::

    >>> import numpy as np
    >>> import pandas as pd
    >>> import pdcast

    >>> pdcast.typecheck(1, "int")
    True
    >>> pdcast.typecheck([True, False, True], "int")
    False
    >>> pdcast.typecheck(np.arange(10), "int64[numpy]")
    True
    >>> pdcast.typecheck(pd.Series(["foo", "bar", "baz"]), "string")
    True

More complex comparisons can be performed by giving a :class:`CompositeType` to
compare against.

.. doctest::

    >>> pdcast.typecheck([1, 2, 3], "bool, int, float")
    True
    >>> pdcast.typecheck(["foo", "bar", "baz"], "bool, int, float")
    False

This returns ``True`` if a match is found for **any** of the comparison types.

If the example data consists of multiple types, then it will return ``True``
if and only if the observed types form a **subset** of the comparison type(s).

.. doctest::

    >>> pdcast.typecheck([True, 1, 2.0], "bool, int, float")
    True
    >>> pdcast.typecheck([True, 1, "baz"], "bool, int, float")
    False

Conversions
-----------
Conversions can be done using the :func:`cast` function.

.. doctest::

    >>> import numpy as np
    >>> import pandas as pd
    >>> import pdcast

    >>> pdcast.cast(1, np.float32)
    0    1.0
    dtype: float32
    >>> pdcast.cast([1, 2, 3], "decimal")
    0    1
    1    2
    2    3
    dtype: decimal
    >>> pdcast.cast([1, 2, None], np.dtype("u1"))
    0       1
    1       2
    2    <NA>
    dtype: UInt8
    >>> pdcast.cast(np.arange(3), "datetime")
    0   1970-01-01 00:00:00.000000000
    1   1970-01-01 00:00:00.000000001
    2   1970-01-01 00:00:00.000000002
    dtype: datetime64[ns]
    >>> pdcast.cast(pd.Series([1, 2, 3], index=["a", "b", "c"]), "complex[python]")
    a   (1+0j)
    b   (2+0j)
    c   (3+0j)
    dtype: complex[python]

:ref:`Keyword arguments <cast_arguments>` can be supplied to change the behavior
of these conversions.

.. doctest::

    >>> pdcast.cast(["FF", "9A", "B0"], "int", base=16)
    >>> pdcast.cast([1, 2, 3], "datetime", unit="s", since="Jan 12th, 2023")

Mixed-type data can be converted using a split-apply-combine strategy.

.. doctest::

    >>> pdcast.cast([True, None, 1, 2.0, pd.NA], "int")
    0    255
    1    154
    2    176
    dtype: int64
    >>> pdcast.cast([1, "1979", pd.Timedelta(days=365)], "datetime", unit="Y")
    0   1971-01-01
    1   1979-01-01
    2   1971-01-01
    dtype: datetime64[ns]

Adapters can be added to the target type to modify its behavior.

.. doctest::

    >>> pdcast.cast([1, 2, 3], "sparse[int]")
    0    1
    1    2
    2    3
    dtype: Sparse[int64, <NA>]
    >>> pdcast.cast([1, 2, 3], "sparse[int, 1]")
    0    1
    1    2
    2    3
    dtype: Sparse[int64, 1]

If an adapter is provided without an associated :class:`AtomicType`, the
detected type of the input series is used instead.

.. doctest::

    >>> pdcast.cast(["foo", "bar", "baz"], "categorical")
    0    foo
    1    bar
    2    baz
    dtype: category
    Categories (3, string): [bar, baz, foo]

This is also true for null cast operations, which can be used to rectify
``dtype: object`` arrays, similar to the ``convert_dtypes()`` method from
`pandas <https://pandas.pydata.org/pandas-docs/stable/reference/api/pandas.Series.convert_dtypes.html>`_.

.. doctest::

    >>> pdcast.cast(pd.Series([1, 2, 3], dtype="O"))
    0    1
    1    2
    2    3
    dtype: int64

.. note::

    There are also several stand-alone
    :ref:`conversion functions <conversions>`, similar to
    ``pandas.to_datetime()``.  These are functionally equivalent to
    :func:`cast`, and are actually called by it in the backend.

Attaching/Detaching from pandas
-------------------------------
All of the above functionality can be attached directly to pandas data
structures by invoking the :func:`attach` function.  This allows users to omit
the ``series`` argument.

.. doctest::

    >>> import pandas as pd
    >>> import pdcast

    >>> pdcast.attach()

    >>> pd.Series([1, 2, 3]).element_type
    NumpyInt64Type()
    >>> pd.Series([1, 2, 3]).typecheck("int")
    True
    >>> pd.Series([1, 2, 3]).cast("datetime", unit="s", since="07/03/2022 at 9:00 AM")
    0   2022-07-03 09:00:01
    1   2022-07-03 09:00:02
    2   2022-07-03 09:00:03
    dtype: datetime64[ns]

.. note::

    Importing and attaching ``pdcast`` to pandas can be done in one line:

    .. code:: python

        import pdcast; pdcast.attach()

DataFrames have all of the same methods, and can accept dictionaries mapping
column names to type specifiers wherever they are accepted.

.. doctest::

    >>> df = pd.DataFrame({"a": [True, False, True], "b": [1, 2, 3], "c": ["foo", "bar", "baz"]})
    >>> df.element_type
    {"a": NumpyBooleanType(), "b": NumpyInt64Type(), "c": StringType()}
    >>> df.typecheck({"a": "bool", "b": "int", "c": "string"})
    True
    >>> df.cast({"a": "uint8", "b": "sparse[float16, 2]"})
       a    b    c
    0  1  1.0  foo
    1  0  2.0  bar
    2  1  3.0  baz

Columns that are excluded from these mappings will be ignored.

Additionally, attaching ``pdcast`` in this way makes
:func:`@dispatch <dispatch>` attributes available from their associated series.

.. doctest::

    >>> series = pd.Series([1.1, 2.5, 3.8])
    >>> type(series.round)
    <class 'pdcast.patch.virtual.DispatchMethod'>
    >>> type(series.round.original)
    <class 'functools.partial'>

These attributes can be hidden behind virtual namespaces, similar to
``Series.dt``, ``Series.str``, etc.

.. doctest::

    >>> series = pd.Series([1, 2, 3], dtype="M8[ns]")
    >>> type(series.dt)
    <class 'pdcast.patch.virtual.Namespace'>
    >>> type(series.dt.tz_localize)
    <class 'pdcast.patch.virtual.DispatchMethod'>
    >>> type(series.dt.tz_localize.original)
    <class 'method'>

Users can freely define new :func:`@dispatch <dispatch>` methods in type
definitions or programmatically at runtime:

.. doctest::

    >>> @pdcast.dispatch(namespace="foo", types="bool, int, float")
    ... def bar(series: pdcast.SeriesWrapper) -> pdcast.SeriesWrapper:
    ...     print("Hello, World!")
    ...     return series

    >>> pd.Series([1, 2, 3]).foo.bar()
    Hello, World!
    0    1
    1    2
    2    3
    dtype: int64
    >>> pd.Series(["a", "b", "c"]).foo.bar()
    Traceback (most recent call last):
        ...
    AttributeError: type object 'Series' has no attribute 'foo'

``pdcast`` functionality can be removed from pandas data structures by calling
:func:`detach`, which returns ``pandas.Series`` and ``pandas.DataFrame``
objects to their original condition, before :func:`attach` was invoked.

.. doctest::

    >>> pdcast.detach()

    >>> pd.Series([1, 2, 3]).cast("float")
    Traceback (most recent call last):
        ...
    AttributeError: 'Series' object has no attribute 'cast'. Did you mean: 'last'?
    >>> pd.Series([1, 2, 3]).foo.bar()
    Traceback (most recent call last):
        ...
    AttributeError: 'Series' object has no attribute 'foo'
