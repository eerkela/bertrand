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

Any specifier that is valid in numpy or pandas is also valid in ``pdcast``.
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

Type Index
----------
Below is a complete list of all the types that come prepackaged with
``pdcast``.  Each can be supplied to ``pdcast.resolve_type()`` via the
:ref:`type specification mini-language <mini_language>` to retrieve the type in
question.

.. list-table::
    :header-rows: 1
    :align: center

    * - Type
      - Backends
      - Subtypes
    * - ``bool``
      - numpy, pandas, python
      - 
    * - ``int``
      - numpy, pandas, python
      - ``signed``, ``unsigned``
    * - ``signed``
      - numpy, pandas, python
      - ``int8``, ``int16``, ``int32``, ``int64``
    * - ``unsigned``
      - numpy, pandas
      - ``uint8``, ``uint16``, ``uint32``, ``uint64``
    * - ``int8``
      - numpy, pandas
      - 
    * - ``int16``
      - numpy, pandas
      - 
    * - ``int32``
      - numpy, pandas
      - 
    * - ``int64``
      - numpy, pandas
      - 
    * - ``uint8``
      - numpy, pandas
      - 
    * - ``uint16``
      - numpy, pandas
      - 
    * - ``uint32``
      - numpy, pandas
      - 
    * - ``uint64``
      - numpy, pandas
      - 
    * - ``float``
      - numpy, python
      - ``float16``, ``float32``, ``float64``, ``float80``\ [#longdouble]_
    * - ``float16``
      - numpy
      - 
    * - ``float32``
      - numpy
      - 
    * - ``float64``
      - numpy, python
      - 
    * - ``float80``\ [#longdouble]_
      - numpy
      - 
    * - ``complex``
      - numpy, python
      - ``complex64``, ``complex128``, ``complex160``\ [#complex_longdouble]_
    * - ``complex64``
      - numpy
      - 
    * - ``complex128``
      - numpy, python
      - 
    * - ``complex160``\ [#complex_longdouble]_
      - numpy
      - 
    * - ``decimal``
      - python
      - 
    * - ``datetime``
      - numpy, pandas, python
      - 
    * - ``timedelta``
      - numpy, pandas, python
      - 
    * - ``string``
      - python, pyarrow [#pyarrow]_
      - 
    * - ``object``
      - [#object]_
      - 
    * - ``sparse``
      - [#adapter]_
      - 
    * - ``categorical``
      - [#adapter]_
      - 

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
.. [#object] by default, object types describe *any* raw python type.  See
    the API docs for more information.
.. [#adapter] These are types that modify other types.  They must be provided
    with another type as their first argument.  See the API docs for more
    information.

Aliases
-------
A complete mapping from every alias that is currently recognized by
``resolve_type()`` to the corresponding type definition can be obtained by
calling ``pdcast.AtomicType.registry.aliases``.  This is guaranteed to be
up-to-date at the time it is invoked.

.. note::

    This includes aliases of every type, from strings and ``dtype`` objects to
    raw python classes.

Some aliases (such as ``"char"``, ``"short"``, ``"long"``, etc.) may be
platform-specific.  These are interpreted as if they were literal C types,
which always map to their
`numpy counterparts <https://numpy.org/doc/stable/user/basics.types.html#data-types>`_.
They can be used interchangeably with their fixed-width alternatives to reflect
current system configuration.

For example, on a 64-bit x86-64 platform:

.. doctest:: type_resolution

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

These might be different on 32-bit platforms, or on those that do not use the
x86-64 instruction set (such as ARM, RISC-V, etc.).

When in doubt, always prefer the platform-independent alternatives.
