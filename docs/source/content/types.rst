.. currentmodule:: pdcast

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
    * - :class:`bool <BooleanType>`
      - :ref:`numpy <numpy_boolean_type>`,
        :ref:`pandas <pandas_boolean_type>`,
        :ref:`python <python_boolean_type>`
      - 
    * - :class:`int <IntegerType>`
      - :ref:`numpy <numpy_integer_type>`,
        :ref:`pandas <pandas_integer_type>`,
        :ref:`python <python_integer_type>`
      - :class:`signed <SignedIntegerType>`,
        :class:`unsigned <UnsignedIntegerType>`
    * - :class:`signed <SignedIntegerType>`
      - :ref:`numpy <numpy_signed_integer_type>`,
        :ref:`pandas <pandas_signed_integer_type>`,
        :ref:`python <python_integer_type>`
      - :class:`int8 <Int8Type>`,
        :class:`int16 <Int16Type>`,
        :class:`int32 <Int32Type>`,
        :class:`int64 <Int64Type>`
    * - :class:`unsigned <UnsignedIntegerType>`
      - :ref:`numpy <numpy_unsigned_integer_type>`,
        :ref:`pandas <pandas_unsigned_integer_type>`
      - :class:`uint8 <UInt8Type>`,
        :class:`uint16 <UInt16Type>`,
        :class:`uint32 <UInt32Type>`,
        :class:`uint64 <UInt64Type>`
    * - :class:`int8 <Int8Type>`
      - :ref:`numpy <numpy_int8_type>`,
        :ref:`pandas <pandas_int8_type>`
      - 
    * - :class:`int16 <Int16Type>`
      - :ref:`numpy <numpy_int16_type>`,
        :ref:`pandas <pandas_int16_type>`
      - 
    * - :class:`int32 <Int32Type>`
      - :ref:`numpy <numpy_int32_type>`,
        :ref:`pandas <pandas_int32_type>`
      - 
    * - :class:`int64 <Int64Type>`
      - :ref:`numpy <numpy_int64_type>`,
        :ref:`pandas <pandas_int64_type>`
      - 
    * - :class:`uint8 <UInt8Type>`
      - :ref:`numpy <numpy_uint8_type>`,
        :ref:`pandas <pandas_uint8_type>`
      - 
    * - :class:`uint16 <UInt16Type>`
      - :ref:`numpy <numpy_uint16_type>`,
        :ref:`pandas <pandas_uint16_type>`
      - 
    * - :class:`uint32 <UInt32Type>`
      - :ref:`numpy <numpy_uint32_type>`,
        :ref:`pandas <pandas_uint32_type>`
      - 
    * - :class:`uint64 <UInt64Type>`
      - :ref:`numpy <numpy_uint64_type>`,
        :ref:`pandas <pandas_uint64_type>`
      - 
    * - :class:`float <FloatType>`
      - :ref:`numpy <numpy_float_type>`,
        :ref:`python <python_float_type>`
      - :class:`float16 <Float16Type>`,
        :class:`float32 <Float32Type>`,
        :class:`float64 <Float64Type>`,
        :class:`float80 <Float80Type>`\ [#longdouble]_
    * - :class:`float16 <Float16Type>`
      - :ref:`numpy <numpy_float16_type>`
      - 
    * - :class:`float32 <Float32Type>`
      - :ref:`numpy <numpy_float32_type>`
      - 
    * - :class:`float64 <Float64Type>`
      - :ref:`numpy <numpy_float64_type>`,
        :ref:`python <python_float_type>`
      - 
    * - :class:`float80 <Float80Type>`\ [#longdouble]_
      - :ref:`numpy <numpy_float80_type>`
      - 
    * - :class:`complex <ComplexType>`
      - :ref:`numpy <numpy_complex_type>`,
        :ref:`python <python_complex_type>`
      - :class:`complex64 <Complex64Type>`,
        :class:`complex128 <Complex128Type>`,
        :class:`complex160 <Complex160Type>`\ [#complex_longdouble]_
    * - :class:`complex64 <Complex64Type>`
      - :ref:`numpy <numpy_complex64_type>`
      - 
    * - :class:`complex128 <Complex128Type>`
      - :ref:`numpy <numpy_complex128_type>`,
        :ref:`python <python_complex_type>`
      - 
    * - :class:`complex160 <Complex160Type>`\ [#complex_longdouble]_
      - :ref:`numpy <numpy_complex160_type>`
      - 
    * - :class:`decimal <DecimalType>`
      - :ref:`python <python_decimal_type>`
      - 
    * - :class:`datetime <DatetimeType>`
      - :ref:`numpy <numpy_datetime64_type>`,
        :ref:`pandas <pandas_timestamp_type>`,
        :ref:`python <python_datetime_type>`
      - 
    * - :class:`timedelta <TimedeltaType>`
      - :ref:`numpy <numpy_timedelta64_type>`,
        :ref:`pandas <pandas_timedelta_type>`,
        :ref:`python <python_timedelta_type>`
      - 
    * - :class:`string StringType`
      - :ref:`python <python_string_type>`,
        :ref:`pyarrow <pyarrow_string_type>` [#pyarrow]_
      - 
    * - :class:`object <ObjectType>`
      - [#object]_
      - 
    * - :class:`sparse <SparseType>`
      - [#adapter]_
      - 
    * - :class:`categorical <CategoricalType>`
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

    >>> import pdcast
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
