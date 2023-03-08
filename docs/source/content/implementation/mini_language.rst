.. _mini_language:

.. TODO: maybe remove argument subsections and put this back in Types
    Arguments go in the individual type description.


Type specification mini-language
================================
With all these new types, we need a more precise way to build and specify them.
This is where ``pdcast``\'s type specification mini-language comes in.  This is
a **superset** of the existing
`numpy <https://numpy.org/doc/stable/reference/arrays.dtypes.html>`_\ /
\ `pandas <https://pandas.pydata.org/pandas-docs/stable/user_guide/basics.html#basics-dtypes>`_
``dtype`` keywords, and follows some of the conventions set out by pandas.
Here's how it works:

A type specifier is composed of 2 parts:

#.  a registered type alias.
#.  an optional list of comma-separated arguments nested within ``[]``
    characters.

This mimics a callable grammar.  When a type specifier is parsed, the alias is
resolved into its corresponding type definition, and that types's
``.resolve()`` method is called with the optional arguments.  Types are free to
customize their behavior during this process, and they can assign arbitrary
meanings to these arguments.

Aliases
-------

.. TODO:

Some aliases are `platform-specific <https://numpy.org/doc/stable/user/basics.types.html#data-types>`



Arguments
---------
When arguments are supplied to a type, they are automatically tokenized,
splitting on commas.  These arguments can include nested sequences or even
other type specifiers, which are both enabled by
`recursive regular expressions <https://perldoc.perl.org/perlre#(?PARNO)-(?-PARNO)-(?+PARNO)-(?R)-(?0)>`_.
Once tokenized, they are passed as strings to the type's ``.resolve()`` method,
filling out its call signature with positional arguments.  This method then
parses them and returns an appropriate instance of the attached type.

Most types don't accept any arguments at all.  The exceptions are datetimes,
timedeltas, object types, ``AdapterType``\s, and any type that has been marked
as generic.

.. note::

    ``pdcast.resolve_type()`` accepts a superset of the existing ``np.dtype()``
    syntax, meaning that any specifier that is accepted by numpy can also be
    accepted by ``pdcast``.

Datetime & timedelta types
^^^^^^^^^^^^^^^^^^^^^^^^^^
For datetimes and timedeltas, this mechanism is used to specify units, step
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






Backends
^^^^^^^^
.. TODO: this goes in actual @generic stub

AtomicTypes can also be marked as being generic, allowing them to serve as
containers for individual backends.  This can be done by appending an
``@generic`` decorator to its class definition, like so:

.. code:: python

    @pdcast.generic
    class Type1(pdcast.AtomicType):
        ...

In order to qualify as a generic type, an ``AtomicType`` must not implement a
custom ``__init__()`` method.  Once marked, backends can be added to a generic
type by calling its ``@register_backend()`` decorator, as shown:

.. code:: python

    @Type1.register_backend("<backend name>")
    class Type2(pdcast.AtomicType):
        ...

This allows ``Type2`` to be resolved from ``Type1`` by passing the specified
backend string during ``resolve_type()`` calls.  It also adds ``Type2`` to
``Type1.subtypes``, and automatically includes it in membership checks.

.. note::

    The backend string provided to ``@register_backend()`` must be unique.

