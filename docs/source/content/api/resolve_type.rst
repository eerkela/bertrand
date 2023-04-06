.. currentmodule:: pdcast

pdcast.resolve_type
===================

.. autofunction:: resolve_type

.. _resolve_type.type_specifiers:

Type Specifiers
---------------
A type specifier can be any of the following:

    #.  A :ref:`numpy <resolve_type.type_specifiers.numpy>`\ /\ 
        :ref:`pandas <resolve_type.type_specifiers.pandas>` ``dtype``\ /\ 
        ``ExtensionDtype`` object.
    #.  A :ref:`python <resolve_type.type_specifiers.python>` class object.
    #.  A string in the
        :ref:`type specification mini-language <mini_language>`.
    #.  An :ref:`iterable <resolve_type.composite>` containing any combination
        of the above.

.. _resolve_type.type_specifiers.numpy:

Numpy ``dtypes``
^^^^^^^^^^^^^^^^
`Numpy <https://numpy.org/doc/stable/reference/arrays.dtypes.html>`_ uses
``dtype`` objects to describe the contents of `packed arrays
<https://numpy.org/doc/stable/reference/arrays.ndarray.html#internal-memory-layout-of-an-ndarray>`_.
These are closely related to their `C equivalents
<https://en.wikipedia.org/wiki/C_data_types>`_ and have all the same
constraints, including limited support for missing values, fixed-width
overflow, and restriction to numeric data.  Conversely, they are by far the
most efficient for both computation and storage, and pandas uses them by
default wherever possible.

:func:`resolve_type` can parse these data types directly, returning equivalents
in the ``pdcast`` type system.

.. doctest::

    >>> pdcast.resolve_type(np.dtype(np.int64))
    NumpyInt64Type()
    >>> pdcast.resolve_type(np.dtype("?"))
    NumpyBooleanType()
    >>> pdcast.resolve_type(np.dtype("M8[30s]"))
    NumpyDatetime64Type(unit='s', step_size=30)

.. note::

    Special cases exist for fixed-width string (``U``) and byte-like
    (``S``\ /\ ``a``\ /\ ``V``) data types.  ``pdcast``, like pandas, converts
    strings into their Python equivalents to support dynamic resizing, and raw
    bytes are unsupported.

    .. doctest::

        >>> pdcast.resolve_type(np.dtype("U32"))
        StringType()
        >>> pdcast.resolve_type(np.dtype("S"))
        Traceback (most recent call last):
            ...
        ValueError: numpy dtype not recognized: |S0
        >>> pdcast.resolve_type(np.dtype("V"))
        Traceback (most recent call last):
            ...
        ValueError: numpy dtype not recognized: |V0

Numpy also exposes a special `object
<https://numpy.org/doc/stable/reference/arrays.scalars.html#numpy.object_>`_
data type, which can store arbitrary Python objects.  Arrays of this type
behave more like standard Python lists and lack most of the benefits of their
packed equivalents.

.. doctest::

    >>> pdcast.resolve_type(np.dtype(object))
    ObjectType(type_def=<class 'object'>)

.. note::

    The behavior of :func:`resolve_type` on numpy ``dtype`` objects can be
    customized by overloading a type's
    :meth:`from_dtype() <AtomicType.from_dtype>` method.

.. _resolve_type.type_specifiers.pandas:

Pandas ``ExtensionDtypes``
^^^^^^^^^^^^^^^^^^^^^^^^^^
`Pandas <https://pandas.pydata.org/pandas-docs/stable/reference/api/pandas.api.extensions.ExtensionDtype.html>`_
also exposes its own ``ExtensionDtype`` objects.  These are conceptually
identical to their :ref:`numpy <resolve_type.type_specifiers.numpy>`
equivalents, but are free to define their own `storage backends
<https://pandas.pydata.org/pandas-docs/stable/reference/api/pandas.api.extensions.ExtensionArray.html>`_,
without being restricted to the numpy ecosystem.

Pandas uses these ``ExtensionDtypes`` to support `nullable integers
<https://pandas.pydata.org/pandas-docs/stable/user_guide/integer_na.html>`_ and
`booleans <https://pandas.pydata.org/pandas-docs/stable/user_guide/boolean.html>`_,
as well as `sparse <https://pandas.pydata.org/pandas-docs/stable/user_guide/sparse.html>`_\ 
/\ `categorical <https://pandas.pydata.org/pandas-docs/stable/user_guide/categorical.html>`_
data structures and `pyarrow integration <https://pandas.pydata.org/pandas-docs/stable/user_guide/pyarrow.html>`_

:func:`resolve_type` can parse these the same way as their
:ref:`numpy <resolve_type.type_specifiers.numpy>` equivalents.

.. doctest::

    >>> pdcast.resolve_type(pd.UInt8Dtype())
    PandasUInt8Type()
    >>> pdcast.resolve_type(pd.BooleanDtype())
    PandasBooleanType()
    >>> pdcast.resolve_type(pd.DatetimeTZDtype(tz="US/Pacific"))
    PandasTimestampType(tz=<DstTzInfo 'US/Pacific' LMT-1 day, 16:07:00 STD>)
    >>> pdcast.resolve_type(pd.SparseDtype(np.float32))
    SparseType(wrapped=NumpyFloat32Type(), fill_value=nan)

.. note::

    The behavior of :func:`resolve_type` on pandas ``ExtensionDtype`` objects
    can be customized by overloading a type's
    :meth:`from_dtype() <AtomicType.from_dtype>` method.

.. _resolve_type.type_specifiers.python:

Python classes
^^^^^^^^^^^^^^
:func:`resolve_type` can also be used to interpret arbitrary Python classes,
which are translated as literally as possible into the ``pdcast`` type system.

.. doctest::

    >>> import decimal

    >>> pdcast.resolve_type(int)
    PythonIntegerType()
    >>> pdcast.resolve_type(np.complex64)
    NumpyComplex64Type()
    >>> pdcast.resolve_type(decimal.Decimal)
    PythonDecimalType()

This differs slightly from the numpy convention, which maps built-in Python
types to ``np.dtype`` objects that may not be entirely compatible.

.. doctest::

    >>> np.dtype(int)
    dtype('int64')

In contrast, the ``pdcast`` equivalents are guaranteed to be valid.

.. note::

    If a class has not been explicitly registered as an :class:`AtomicType`
    :attr:`alias <AtomicType.aliases>`, then a new :class:`ObjectType` will be
    built around it.

    .. doctest::

        >>> class CustomObj:
        ...     pass

        >>> pdcast.resolve_type(CustomObj)
        ObjectType(type_def=<class 'CustomObj'>)

.. _mini_language:

Type specification mini-language
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
``pdcast`` provides its own `domain-specific language
<https://en.wikipedia.org/wiki/Domain-specific_language>`_ for parsing
string-based type specifiers.  This language is a generalization of the
existing `numpy <https://numpy.org/doc/stable/reference/arrays.dtypes.html>`_\ /\ 
`pandas <https://pandas.pydata.org/pandas-docs/stable/user_guide/basics.html#basics-dtypes>`_
keywords and syntax, which can be customized on a per-type basis.  Here's how
it works:

A ``typespec`` string is composed of 2 parts:

#.  a registered string :attr:`alias <AtomicType.aliases>`.
#.  an optional list of comma-separated arguments nested within ``[]``
    characters.

This resembles a callable grammar.  In fact, it directly translates to a call
to the type's :meth:`resolve() <AtomicType.resolve>` constructor, which can be
customized to alter the type's behavior.  This leaves each type free to
assign its own meaning to the optional arguments and parse them accordingly.

When a type specifier is parsed, its alias is compared against a shared
:attr:`table <TypeRegistry.aliases>` representing the current
:class:`state <TypeRegistry>` of the ``pdcast`` type system.  This is
done via `PCRE <https://pcre.org/>`_\-style `recursive regular expressions
<https://perldoc.perl.org/perlre#(?PARNO)-(?-PARNO)-(?+PARNO)-(?R)-(?0)>`_,
which allow the arguments to include nested sequences or even other type
specifiers of arbitrary depth.  These are then tokenized and passed as
positional arguments to the type's :meth:`resolve <AtomicType.resolve>` method,
which parses them and returns an instance of the associated type.

Most types don't accept any arguments at all.  The exceptions are
:doc:`datetimes <../types/datetime>`, :doc:`timedeltas <../types/timedelta>`,
:class:`object <ObjectType>` types, :class:`AdapterTypes <AdapterType>`, and
any type that has been marked as :func:`generic <generic>`.  Check the
documentation for each type to find the available arguments and their
associated meanings.

.. doctest::

    >>> pdcast.resolve_type("string")
    StringType()
    >>> pdcast.resolve_type("timedelta[pandas]")   # doctest: +SKIP
    PandasTimedeltaType()
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

.. note::

    :func:`resolve_type()` accepts a **superset** of the existing
    ``np.sctypeDict`` type codes.  Therefore, any specifier that is accepted
    by numpy is also accepted by ``pdcast`` on a 1:1 basis.  This is also the
    case for pandas ``ExtensionDtype`` codes such as ``"Int64"``, ``"Sparse"``,
    etc.

.. _resolve_type.platform_specific:

Platform-specific aliases
-------------------------
Some aliases (such as ``"char"``, ``"short"``, ``"long"``, etc.) may be
`platform-specific <https://www.learnc.net/c-tutorial/c-integer/>`_.  These are
interpreted as if they were literal `C types
<https://en.wikipedia.org/wiki/C_data_types>`_,
which can be used interchangeably with their fixed-width alternatives to
reflect current system configuration.  For example, on a `64-bit
<https://en.wikipedia.org/wiki/64-bit_computing>`_ `x86-64
<https://en.wikipedia.org/wiki/X86-64>`_ platform:

.. doctest::

    >>> pdcast.resolve_type("char")  # C char
    Int8Type()
    >>> pdcast.resolve_type("short int")  # C short
    Int16Type()
    >>> pdcast.resolve_type("signed intc")  # C int
    Int32Type()
    >>> pdcast.resolve_type("unsigned long integer")  # C unsigned long
    UInt64Type()
    >>> pdcast.resolve_type("longlong")  # C long long
    Int64Type()
    >>> pdcast.resolve_type("ssize_t")  # C pointer size
    Int64Type()

These might be different on `32-bit
<https://en.wikipedia.org/wiki/32-bit_computing>`_ platforms, or on those that
do not use the `x86-64 <https://en.wikipedia.org/wiki/X86-64>`_ instruction
set, such as `ARM <https://en.wikipedia.org/wiki/ARM_architecture_family>`_,
`RISC-V <https://en.wikipedia.org/wiki/RISC-V>`_, etc.

When in doubt, always prefer the fixed-width alternatives.

.. _resolve_type.composite:

Composite types
---------------
:func:`resolve_type` also supports easy :class:`compositing <CompositeType>` of
types.  This can be done by providing an iterable as input:

.. doctest::

    >>> pdcast.resolve_type(["int"])
    CompositeType({int})
    >>> pdcast.resolve_type((float, complex))   # doctest: +SKIP
    CompositeType({float[python], complex[python]})
    >>> pdcast.resolve_type({bool, np.dtype("i2"), "decimal"})   # doctest: +SKIP
    CompositeType({bool[python], int16[numpy], decimal})

Or by separating specifiers with commas in the :ref:`type specification
mini-language <mini_language>`.

.. doctest::

    >>> pdcast.resolve_type("int, float, complex")   # doctest: +SKIP
    CompositeType({int, float, complex})
    >>> pdcast.resolve_type("sparse[bool], Timestamp, categorical[str]")   # doctest: +SKIP
    CompositeType({sparse[bool, <NA>], datetime[pandas], categorical[string]})
