.. currentmodule:: pdcast

.. TODO: split each of these into a separate page and use autoclass directives
    rather than autosummary.  This prevents automatic membership documentation

Types
=====
Below is a complete list of all the types that come prepackaged with
``pdcast``.  Each can be constructed via :func:`resolve_type` using the
:ref:`type specification mini-language <resolve_type.mini_language>` or a
related :ref:`type specifier <resolve_type.type_specifiers>`.

.. list-table::
    :header-rows: 1
    :align: center

    * - Type
      - Backends
      - Subtypes
    * - :class:`bool <BooleanType>`
      - :class:`numpy <NumpyBooleanType>`,
        :class:`pandas <PandasBooleanType>`,
        :class:`python <PythonBooleanType>`
      - 
    * - :class:`int <IntegerType>`
      - :class:`numpy <NumpyIntegerType>`,
        :class:`pandas <PandasIntegerType>`,
        :class:`python <PythonIntegerType>`
      - :class:`signed <SignedIntegerType>`,
        :class:`unsigned <UnsignedIntegerType>`
    * - :class:`signed <SignedIntegerType>`
      - :class:`numpy <NumpySignedIntegerType>`,
        :class:`pandas <PandasSignedIntegerType>`,
        :class:`python <PythonIntegerType>`
      - :class:`int8 <Int8Type>`,
        :class:`int16 <Int16Type>`,
        :class:`int32 <Int32Type>`,
        :class:`int64 <Int64Type>`
    * - :class:`unsigned <UnsignedIntegerType>`
      - :class:`numpy <NumpyUnsignedIntegerType>`,
        :class:`pandas <PandasUnsignedIntegerType>`
      - :class:`uint8 <UInt8Type>`,
        :class:`uint16 <UInt16Type>`,
        :class:`uint32 <UInt32Type>`,
        :class:`uint64 <UInt64Type>`
    * - :class:`int8 <Int8Type>`
      - :class:`numpy <NumpyInt8Type>`,
        :class:`pandas <PandasInt8Type>`
      - 
    * - :class:`int16 <Int16Type>`
      - :class:`numpy <NumpyInt16Type>`,
        :class:`pandas <PandasInt16Type>`
      - 
    * - :class:`int32 <Int32Type>`
      - :class:`numpy <NumpyInt32Type>`,
        :class:`pandas <PandasInt32Type>`
      - 
    * - :class:`int64 <Int64Type>`
      - :class:`numpy <NumpyInt64Type>`,
        :class:`pandas <PandasInt64Type>`
      - 
    * - :class:`uint8 <UInt8Type>`
      - :class:`numpy <NumpyUInt8Type>`,
        :class:`pandas <PandasUInt8Type>`
      - 
    * - :class:`uint16 <UInt16Type>`
      - :class:`numpy <NumpyUInt16Type>`,
        :class:`pandas <PandasUInt16Type>`
      - 
    * - :class:`uint32 <UInt32Type>`
      - :class:`numpy <NumpyUInt32Type>`,
        :class:`pandas <PandasUInt32Type>`
      - 
    * - :class:`uint64 <UInt64Type>`
      - :class:`numpy <NumpyUInt64Type>`,
        :class:`pandas <PandasUInt64Type>`
      - 
    * - :class:`float <FloatType>`
      - :class:`numpy <NumpyFloatType>`,
        :class:`python <PythonFloatType>`
      - :class:`float16 <Float16Type>`,
        :class:`float32 <Float32Type>`,
        :class:`float64 <Float64Type>`,
        :class:`float80 <Float80Type>`\ [#longdouble]_
    * - :class:`float16 <Float16Type>`
      - :class:`numpy <NumpyFloat16Type>`
      - 
    * - :class:`float32 <Float32Type>`
      - :class:`numpy <NumpyFloat32Type>`
      - 
    * - :class:`float64 <Float64Type>`
      - :class:`numpy <NumpyFloat64Type>`,
        :class:`python <PythonFloatType>`
      - 
    * - :class:`float80 <Float80Type>`\ [#longdouble]_
      - :class:`numpy <NumpyFloat80Type>`
      - 
    * - :class:`complex <ComplexType>`
      - :class:`numpy <NumpyComplexType>`,
        :class:`python <PythonComplexType>`
      - :class:`complex64 <Complex64Type>`,
        :class:`complex128 <Complex128Type>`,
        :class:`complex160 <Complex160Type>`\ [#complex_longdouble]_
    * - :class:`complex64 <Complex64Type>`
      - :class:`numpy <NumpyComplex64Type>`
      - 
    * - :class:`complex128 <Complex128Type>`
      - :class:`numpy <NumpyComplex128Type>`,
        :class:`python <PythonComplexType>`
      - 
    * - :class:`complex160 <Complex160Type>`\ [#complex_longdouble]_
      - :class:`numpy <NumpyComplex160Type>`
      - 
    * - :class:`decimal <DecimalType>`
      - :class:`python <PythonDecimalType>`
      - 
    * - :class:`datetime <DatetimeType>`
      - :class:`numpy <NumpyDatetime64Type>`,
        :class:`pandas <PandasTimestampType>`,
        :class:`python <PythonDatetimeType>`
      - 
    * - :class:`timedelta <TimedeltaType>`
      - :class:`numpy <NumpyTimedelta64Type>`,
        :class:`pandas <PandasTimedeltaType>`,
        :class:`python <PythonTimedeltaType>`
      - 
    * - :class:`string <StringType>`
      - :class:`python <PythonStringType>`,
        :class:`pyarrow <PyArrowStringType>` [#depends_pyarrow]_
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
    :class:`float96 <numpy.float96>` or :class:`float128 <numpy.float128>`
    object, but neither is technically accurate and only one of them is ever
    exposed at a time, depending on system configuration
    (:class:`float96 <numpy.float96>` for 32-bit systems,
    :class:`float128 <numpy.float128>` for 64-bit).
    :class:`float80 <Float80Type>` was chosen to reflect the actual number of
    significant bits in the specification, rather than the length it occupies
    in memory.  The type's :attr:`itemsize <AtomicType.itemsize>` differs from
    this, and is always accurate for the system in question.
.. [#complex_longdouble] Complex equivalent of [1]
.. [#depends_pyarrow] "pyarrow" backend requires `PyArrow
    <https://arrow.apache.org/docs/python/index.html>`_\>=1.0.0.
.. [#object] by default, :class:`ObjectTypes <ObjectType>` describe *any* raw
    python type, storing instances internally as a ``dtype: object`` array.
.. [#adapter] These are :class:`AdapterTypes <AdapterType>`, which can be used
    modify the behavior of other types.  See the :ref:`API docs <AdapterType>`
    for more information.

.. toctree::
    :hidden:
    :maxdepth: 2

    boolean
    integer
    float
    complex
    decimal
    datetime
    timedelta
    string
    object
    sparse
    categorical


.. TODO: RELEVANT LINKS
    numpy:
    https://numpy.org/doc/stable/reference/arrays.scalars.html#sized-aliases
    https://numpy.org/doc/stable/reference/arrays.dtypes.html
    https://numpy.org/doc/stable/user/basics.types.html#data-types

    pandas
    https://pandas.pydata.org/pandas-docs/stable/reference/arrays.html
    https://pandas.pydata.org/pandas-docs/stable/development/extending.html


.. TODO: each type needs to list its attributes and dispatched methods, if
    it has any.
