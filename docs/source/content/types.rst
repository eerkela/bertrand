Types
=====


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
      - :doc:`numpy <../generated/pdcast.NumpyBooleanType>`,
        :doc:`pandas <../generated/pdcast.PandasBooleanType>`,
        :doc:`python <../generated/pdcast.PythonBooleanType>`
      - 
    * - ``int``
      - :doc:`numpy <../generated/pdcast.NumpyIntegerType>`,
        :doc:`pandas <../generated/pdcast.PandasIntegerType>`,
        :doc:`python <../generated/pdcast.PythonIntegerType>`
      - ``signed``, ``unsigned``
    * - ``signed``
      - :doc:`numpy <../generated/pdcast.NumpySignedIntegerType>`,
        :doc:`pandas <../generated/pdcast.PandasSignedIntegerType>`,
        :doc:`python <../generated/pdcast.PythonIntegerType>`
      - ``int8``, ``int16``, ``int32``, ``int64``
    * - ``unsigned``
      - :doc:`numpy <../generated/pdcast.NumpyUnsignedIntegerType>`,
        :doc:`pandas <../generated/pdcast.PandasUnsignedIntegerType>`
      - ``uint8``, ``uint16``, ``uint32``, ``uint64``
    * - ``int8``
      - :doc:`numpy <../generated/pdcast.NumpyInt8Type>`,
        :doc:`pandas <../generated/pdcast.PandasInt8Type>`
      - 
    * - ``int16``
      - :doc:`numpy <../generated/pdcast.NumpyInt16Type>`,
        :doc:`pandas <../generated/pdcast.PandasInt16Type>`
      - 
    * - ``int32``
      - :doc:`numpy <../generated/pdcast.NumpyInt32Type>`,
        :doc:`pandas <../generated/pdcast.PandasInt32Type>`
      - 
    * - ``int64``
      - :doc:`numpy <../generated/pdcast.NumpyInt64Type>`,
        :doc:`pandas <../generated/pdcast.PandasInt64Type>`
      - 
    * - ``uint8``
      - :doc:`numpy <../generated/pdcast.NumpyUInt8Type>`,
        :doc:`pandas <../generated/pdcast.PandasUInt8Type>`
      - 
    * - ``uint16``
      - :doc:`numpy <../generated/pdcast.NumpyUInt16Type>`,
        :doc:`pandas <../generated/pdcast.PandasUInt16Type>`
      - 
    * - ``uint32``
      - :doc:`numpy <../generated/pdcast.NumpyUInt32Type>`,
        :doc:`pandas <../generated/pdcast.PandasUInt32Type>`
      - 
    * - ``uint64``
      - :doc:`numpy <../generated/pdcast.NumpyUInt64Type>`,
        :doc:`pandas <../generated/pdcast.PandasUInt64Type>`
      - 
    * - ``float``
      - :doc:`numpy <../generated/pdcast.NumpyFloatType>`,
        :doc:`python <../generated/pdcast.PythonFloatType>`
      - ``float16``, ``float32``, ``float64``, ``float80``\ [#longdouble]_
    * - ``float16``
      - :doc:`numpy <../generated/pdcast.NumpyFloat16Type>`
      - 
    * - ``float32``
      - :doc:`numpy <../generated/pdcast.NumpyFloat32Type>`
      - 
    * - ``float64``
      - :doc:`numpy <../generated/pdcast.NumpyFloat64Type>`,
        :doc:`python <../generated/pdcast.PythonFloatType>`
      - 
    * - ``float80``\ [#longdouble]_
      - :doc:`numpy <../generated/pdcast.NumpyFloat80Type>`
      - 
    * - ``complex``
      - :doc:`numpy <../generated/pdcast.NumpyComplexType>`,
        :doc:`python <../generated/pdcast.PythonComplexType>`
      - ``complex64``, ``complex128``, ``complex160``\ [#complex_longdouble]_
    * - ``complex64``
      - :doc:`numpy <../generated/pdcast.NumpyComplex64Type>`
      - 
    * - ``complex128``
      - :doc:`numpy <../generated/pdcast.NumpyComplex128Type>`,
        :doc:`python <../generated/pdcast.PythonComplexType>`
      - 
    * - ``complex160``\ [#complex_longdouble]_
      - :doc:`numpy <../generated/pdcast.NumpyComplex160Type>`
      - 
    * - ``decimal``
      - :doc:`python <../generated/pdcast.PythonDecimalType>`
      - 
    * - ``datetime``
      - :doc:`numpy <../generated/pdcast.NumpyDatetime64Type>`,
        :doc:`pandas <../generated/pdcast.PandasTimestampType>`,
        :doc:`python <../generated/pdcast.PythonDatetimeType>`
      - 
    * - ``timedelta``
      - :doc:`numpy <../generated/pdcast.NumpyTimedelta64Type>`,
        :doc:`pandas <../generated/pdcast.PandasTimedeltaType>`,
        :doc:`python <../generated/pdcast.PythonTimedeltaType>`
      - 
    * - ``string``
      - :doc:`python <../generated/pdcast.PythonStringType>`,
        :doc:`pyarrow <../generated/pdcast.PyArrowStringType>` [#pyarrow]_
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
.. TODO: move most of this into TypeRegistry.aliases.  Just include a link and
    an example here.

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
