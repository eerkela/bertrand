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

Aliases
-------
A complete mapping from every alias that is currently recognized by
``resolve_type()`` to the corresponding type definition can be obtained by
calling ``pdcast.AtomicType.registry.aliases``

.. note::

    This includes aliases of every type, from strings to ``dtype`` objects and
    raw python classes.  It is guaranteed to be up-to-date.

Type Index
----------
Below is a complete list of all the types that come prepackaged with
``pdcast``, in hierarchical order.  Each type is listed along with all of its
potential backends in square brackets, separated by slashes.  Any one of these
can be supplied to ``pdcast.resolve_type()`` via the
:ref:`type specification mini-language <mini_language>` to retrieve the type in
question.

* ``bool[numpy/pandas/python]``
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

* ``float[numpy/python]``

    * ``float16[numpy]``
    * ``float32[numpy]``
    * ``float64[numpy/python]``
    * ``float80[numpy]``\ [#longdouble]_

* ``complex[numpy/python]``

    * ``complex64[numpy]``
    * ``complex128[numpy/python]``
    * ``complex160[numpy]``\ [#complex_longdouble]_

* ``decimal[python]``
* ``datetime[numpy/pandas/python]``
* ``timedelta[numpy/pandas/python]``
* ``string[pyarrow/python]``\ [#pyarrow]_
* ``object``
* ``sparse``
* ``categorical``

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
