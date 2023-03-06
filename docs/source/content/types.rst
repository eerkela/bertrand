.. TODO: add type specification mini-language section?

Types
=====
In ``pdcast``, types can be specified in one of three ways:

#.  by providing an ``np.dtype`` or ``pd.api.extensions.ExtensionDtype``
    object.
#.  by providing a raw python type.  If the type has not been registered as an
    AtomicType alias, a new ``ObjectType`` will be built around the class
    definition.
#.  by providing an appropriate string in the
    :ref:`type specification mini-language <mini_language>`.

Here are some examples of valid type specifiers:

.. doctest:: type_resolution

    >>> import numpy as np
    >>> import pandas as pd
    >>> import pdcast

    >>> class CustomObj:
    ...     pass

    # raw classes
    >>> pdcast.resolve_type(int)
    IntegerType()
    >>> pdcast.resolve_type(np.float32)
    NumpyFloat32Type()
    >>> pdcast.resolve_type(CustomObj)
    ObjectType(type_def=<class 'CustomObj'>)

    # dtype objects
    >>> pdcast.resolve_type(np.dtype("?"))
    NumpyBooleanType()
    >>> pdcast.resolve_type(np.dtype("M8[30s]"))
    NumpyDatetime64Type(unit='s', step_size=30)
    >>> pdcast.resolve_type(pd.UInt8Dtype())
    PandasUInt8Type()

    # type specification mini language
    >>> pdcast.resolve_type("string")
    StringType()
    >>> pdcast.resolve_type("timedelta[python]")
    PythonTimedeltaType()
    >>> pdcast.resolve_type("datetime[pandas, US/Pacific]")
    PandasTimestampType(tz=<DstTzInfo 'US/Pacific' LMT-1 day, 16:07:00 STD>)
    >>> pdcast.resolve_type("sparse[decimal]")
    SparseType(wrapped=DecimalType(), fill_value=Decimal('NaN'))
    >>> pdcast.resolve_type("sparse[bool, False]")
    SparseType(wrapped=BooleanType(), fill_value=False)
    >>> pdcast.resolve_type("categorical[str[pyarrow]]")
    CategoricalType(wrapped=PyArrowStringType(), levels=None)
    >>> pdcast.resolve_type("sparse[categorical[int]]")
    SparseType(wrapped=CategoricalType(wrapped=IntegerType(), levels=None), fill_value=<NA>)

.. _mini_language:

Type specification mini-language
--------------------------------
With all these new types, we need a more precise way to specify them.  This is
where ``pdcast``\'s type specification mini-language comes in.  This is a
superset of the existing numpy/pandas ``dtype`` resolution grammar, and follows
some of the conventions set by pandas.  Here's how it works:

A type specifier is composed of 2 parts:

#.  a registered type alias.
#.  an optional list of comma-separated arguments nested within ``[]``
    characters.

This mimics a callable grammar.  When a type specifier is parsed, the alias is
resolved into its corresponding type definition, and that types's
``.resolve()`` method is called with the optional arguments.  Types are free to
customize their behavior during this process, and they can assign arbitrary
meanings to these arguments.

When arguments are supplied to a type, they are automatically tokenized,
splitting on commas.  These arguments can include nested sequences or even
other type specifiers, which are both enabled by
`recursive regular expressions <https://www.rexegg.com/regex-recursion.html>`_.
Once tokenized, they are passed as strings to the type's ``.resolve()`` method,
filling out its call signature with positional arguments.  This method then
parses them and returns an appropriate instance of the attached type.

Most types don't accept any arguments at all.  The exceptions are datetimes,
timedeltas, object types, ``AdapterType``\s, and any type that has been marked
as generic.

Datetime & timedelta types
^^^^^^^^^^^^^^^^^^^^^^^^^^
For datetimes and timedeltas, this is used to specify units, step
sizes, and/or timezones, enabling the following forms:

.. doctest:: type_resolution

    >>> pdcast.resolve_type("Timestamp[US/Pacific]")
    PandasTimestampType(tz=<DstTzInfo 'US/Pacific' LMT-1 day, 16:07:00 STD>)
    >>> pdcast.resolve_type("pydatetime[UTC]")
    PythonDatetimeType(tz=<UTC>)
    >>> pdcast.resolve_type("M8[5ns]")
    NumpyDatetime64Type(unit='ns', step_size=5)
    >>> pdcast.resolve_type("m8[s]")
    NumpyTimedelta64Type(unit='s', step_size=1)

Object types
^^^^^^^^^^^^
``ObjectType``\s use the type specification mini-language to specify python
types to dynamically wrap.  These are pulled directly from the calling
environment via the ``inspect`` module, which can resolve them directly by
name.

.. doctest:: type_resolution

    >>> class CustomObj:
    ...     pass

    >>> pdcast.resolve_type("object[int]")
    ObjectType(type_def=<class 'int'>)
    >>> pdcast.resolve_type("object[CustomObj]")
    ObjectType(type_def=<class 'CustomObj'>)

Generic types
^^^^^^^^^^^^^
For generic types, the process is somewhat different.  These types do not
implement their own ``.resolve()`` methods; they instead inherit them from the
``@generic`` decorator itself.  This comes with the restriction that the
generic type does not accept any arguments of its own, which is guaranteed by
the decorator at import time.  The inherited ``.resolve()`` method works as
follows:

#.  If provided, the first argument must be a registered backend of the generic
    type.
#.  Any additional arguments are passed on to the specified implementation's
    ``.resolve()`` method as if it were called directly.

This allows generic types to shift perspectives during the resolution process,
and enables constructs of the form:

.. doctest:: type_resolution

    >>> pdcast.resolve_type("int8[pandas]")
    PandasInt8Type()
    >>> pdcast.resolve_type("datetime[pandas, US/Pacific]")
    PandasTimestampType(tz=<DstTzInfo 'US/Pacific' LMT-1 day, 16:07:00 STD>)
    >>> pdcast.resolve_type("datetime[python, UTC]")
    PythonDatetimeType(tz=<UTC>)
    >>> pdcast.resolve_type("datetime[numpy, 5ns]")
    NumpyDatetime64Type(unit='ns', step_size=5)
    >>> pdcast.resolve_type("timedelta[numpy, s]")
    NumpyTimedelta64Type(unit='s', step_size=1)

Adapter types
^^^^^^^^^^^^^
``AdapterType``\s are types that modify other types.  These include sparse and
categorical types, which provide a wrapper on top of a base ``AtomicType``
instance, adding information related to fill values and levels, respectively.
These must be provided at least one argument (the type being wrapped), which
can be another ``AdapterType`` specifier, allowing them to be arbitrarily
nested.

Here are some examples of basic adapter types:

.. doctest:: type_resolution

    >>> pdcast.resolve_type("sparse[int]")
    SparseType(wrapped=IntegerType(), fill_value=<NA>)
    >>> pdcast.resolve_type("sparse[str[pyarrow]]")
    SparseType(wrapped=PyArrowStringType(), fill_value=<NA>)
    >>> pdcast.resolve_type("categorical[bool]")
    CategoricalType(wrapped=BooleanType(), levels=None)
    >>> pdcast.resolve_type("sparse[categorical[bool]]")
    SparseType(wrapped=CategoricalType(wrapped=BooleanType(), levels=None), fill_value=<NA>)

By default, sparse types use the base type's ``na_value`` field to determine
the ``fill_value``, but this can be manually specified by adding an additional
argument.

.. doctest:: type_resolution

    >>> pdcast.resolve_type("sparse[bool, True]")
    SparseType(wrapped=BooleanType(), fill_value=True)
    >>> pdcast.resolve_type("sparse[int, -32]")
    SparseType(wrapped=IntegerType(), fill_value=-32)
    >>> pdcast.resolve_type("sparse[decimal, 4.68]")
    SparseType(wrapped=DecimalType(), fill_value=Decimal('4.68'))

Note that the second argument is provided as a string, but is resolved to an
object of the same type as the base.  This is thanks to ``pdcast``\s robust
suite of type conversions!  In fact, any string that can be converted to the
base type can be accepted here.

.. doctest:: type_resolution

    >>> pdcast.resolve_type("sparse[bool, y]")
    SparseType(wrapped=BooleanType(), fill_value=True)
    >>> pdcast.resolve_type("sparse[datetime[pandas], Jan 12 2022 at 7:00 AM]")
    SparseType(wrapped=PandasTimestampType(tz=None), fill_value=Timestamp('2022-01-12 07:00:00'))

This is similar for categorical types, except that the second argument must be
a sequence, each element of which is resolved to form the levels of the
categorical type.

.. doctest:: type_resolution

    >>> pdcast.resolve_type("categorical[bool, [y, n]]")
    CategoricalType(wrapped=BooleanType(), levels=[True, False])
    >>> pdcast.resolve_type("categorical[int, [1, 2, 3]]")
    CategoricalType(wrapped=IntegerType(), levels=[1, 2, 3])
    >>> pdcast.resolve_type("categorical[decimal, [1.23, 2.34]]")
    CategoricalType(wrapped=DecimalType(), levels=[Decimal('1.23'), Decimal('2.34')])

.. note::

    These conversions use the **default** values for ``cast()`` operations.  If
    you'd like to change how these are interpreted, modify the defaults using
    ``cast.defaults``.

Composite types
^^^^^^^^^^^^^^^
Types can also be easily composited in the type specification mini-language
simply by separating them with commas, like so:

.. doctest:: type_resolution

    >>> pdcast.resolve_type("int, float, complex")   # doctest: +SKIP
    CompositeType({int, float, complex})
    >>> pdcast.resolve_type("sparse[bool], Timestamp, categorical[str]")   # doctest: +SKIP
    CompositeType({sparse[bool, <NA>], datetime[pandas], categorical[string]})

Or by providing an iterable to ``pdcast.resolve_type()``.

.. doctest:: type_resolution

    >>> pdcast.resolve_type([int, float, complex])   # doctest: +SKIP
    CompositeType({int, float, complex})
    >>> pdcast.resolve_type(["sparse[bool]", pd.Timestamp, "categorical[str]"])  # doctest: +SKIP
    CompositeType({sparse[bool, <NA>], datetime[pandas], categorical[string]})

Type Index
----------
Below is a complete list of all the types that come prepackaged with
``pdcast``, in hierarchical order.  Each type is listed along with all of its
potential backends in square brackets, separated by slashes.  Any one of these
can be supplied to ``pdcast.resolve_type()`` via the
:ref:`type specification mini-language <mini_language>` to retrieve the type in
question.

Boolean
^^^^^^^
* ``bool[numpy/pandas/python]``

Integer
^^^^^^^
* ``int[numpy/pandas/python]``

    * ``signed[numpy/pandas/python]``

        * ``int8[numpy/pandas]``

        * ``int16[numpy/pandas]``

        * ``int32[numpy/pandas]```

        * ``int64[numpy/pandas]``

    * ``unsigned[numpy/pandas]``

        * ``uint8[numpy/pandas]``

        * ``uint16[numpy/pandas]``

        * ``uint32[numpy/pandas]``

        * ``uint64[numpy/pandas]``

Float
^^^^^
* ``float[numpy/python]``

    * ``float16[numpy]``

    * ``float32[numpy]``

    * ``float64[numpy/python]``

    * ``float80[numpy]``\ [#longdouble]_

Complex
^^^^^^^
* ``complex[numpy/python]``

    * ``complex64[numpy]``

    * ``complex128[numpy/python]``

    * ``complex160[numpy]``\ [#complex_longdouble]_

Decimal
^^^^^^^
* ``decimal[python]``

Datetime
^^^^^^^^
* ``datetime[numpy/pandas/python]``

Timedelta
^^^^^^^^^
* ``timedelta[numpy/pandas/python]``

String
^^^^^^
* ``string[pyarrow/python]``\ [#pyarrow]_

Object
^^^^^^
* ``object``

Aliases
-------
A complete mapping from every alias that is currently recognized by
``resolve_type()`` to the corresponding type definition can be obtained by
calling ``pdcast.AtomicType.registry.aliases``

.. note::

    This includes aliases of every type, from strings to ``dtype`` objects and
    raw python classes.  It is guaranteed to be up-to-date.

.. NOTE: raw html section header does not appear in TOC tree

.. raw:: html

    <h2>Footnotes</h2>

.. [#longdouble] This is an alias for an `x86 extended precision float (long double) <https://en.wikipedia.org/wiki/Extended_precision#x86_extended_precision_format>`_ 
    and may not be exposed on every system.  Numpy defines this as either a
    ``float96`` or ``float128`` object, but neither is technically accurate and
    only one of them is ever exposed at a time, depending on system configuration
    (``float96`` for 32-bit systems, ``float128`` for 64-bit).  ``float80`` was
    chosen to reflect the actual number of significant bits in the specification,
    rather than the length it occupies in memory.  The type's ``itemsize`` differs
    from this, and is always accurate for the system in question.
.. [#complex_longdouble] Complex equivalent of [1]
.. [#pyarrow] "pyarrow" backend requires PyArrow>=1.0.0.
