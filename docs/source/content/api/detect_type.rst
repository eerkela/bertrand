.. currentmodule:: pdcast

.. testsetup::

    import numpy as np
    import pandas as pd
    import pdcast

.. _detect_type:

pdcast.detect_type
==================

.. autofunction:: detect_type

.. _detect_type.scalar:

Scalar data
-----------
:func:`detect_type` can be used to parse scalar objects, similar to the
built-in :class:`type() <python:type>` function.

.. doctest::

    >>> pdcast.detect_type(True)
    PythonBooleanType()
    >>> pdcast.detect_type(pd.Timestamp(0, tz="US/Pacific"))
    PandasTimestampType(tz=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)

.. note::

    If the :class:`type() <python:type>` of an object has not been registered
    as an :attr:`alias <ScalarType.aliases>`, then a new :class:`ObjectType`
    will be built around it.

    .. doctest::

        >>> class CustomObj:
        ...     def __init__(self, x): self.x = x

        >>> pdcast.detect_type(CustomObj("abc"))
        ObjectType(type_def=<class 'CustomObj'>)

.. _detect_type.vector:

Vectorized data
---------------
:func:`detect_type` can also accept 1D examples in a variety of formats.  If an
input vector has an appropriate ``.dtype`` field, then :func:`detect_type` will
attempt to :func:`resolve <resolve_type>` it directly.

.. doctest::

    >>> pdcast.detect_type(np.arange(10))
    NumpyInt64Type()
    >>> pdcast.detect_type(pd.Series([1., 2., 3.]))
    NumpyFloat64Type()

This is an *O(1)* operation, regardless of how many elements are stored in the
array.

.. doctest::

    >>> import timeit

    >>> vals = np.arange(10**6)
    >>> timeit.timeit(lambda: pdcast.detect_type(vals), number=10**3)   # doctest: +SKIP
    0.0040021560052991845

If the input is a ``dtype: object`` array or other iterable, then its type
will be inferred by iterating elementwise.

.. doctest::

    >>> pdcast.detect_type([1, 2, 3])
    PythonIntegerType()
    >>> pdcast.detect_type((np.int32(1), np.int32(2), np.int32(3)))
    NumpyInt32Type()
    >>> pdcast.detect_type(np.float16(x) for x in (1, 2, 3))
    NumpyFloat16Type()
    >>> pdcast.detect_type(np.array([1., 2., 3.], dtype="O"))
    PythonFloatType()
    >>> pdcast.detect_type(pd.Series(["foo", "bar", "baz"]))
    StringType()

This is *O(N)*.

If the input is a vector whose elements are of mixed type, then a
:class:`CompositeType` will be returned.

.. doctest::

    >>> pdcast.detect_type([True, 2, "foo"])   # doctest: +SKIP
    CompositeType({bool[python], int[python], string})
    >>> pdcast.detect_type(pd.Series([1., 2+0j, pd.Timestamp(0)]))    # doctest: +SKIP
    CompositeType({float[python], complex[python], datetime[pandas]})

This also tracks the type that was observed at each
:attr:`index <CompositeType.index>`.

.. doctest::

    >>> _.index
    array([PythonFloatType(), PythonComplexType(),
           PandasTimestampType(tz=None)], dtype=object)

.. note::

    This is space-efficient thanks to :class:`ScalarType`\'s
    :ref:`flyweight construction <ScalarType.allocation>`.

.. _detect_type.pandas:

Pandas integration
------------------
If :func:`pdcast.attach() <attach>` is invoked, the output from this function
is attached directly to :class:`pandas.Series` objects under
:attr:`pandas.Series.element_type`.

.. doctest::

    >>> pdcast.attach()
    >>> pd.Series([1, 2, 3]).element_type
    NumpyInt64Type()

A similar attribute is attached to :class:`pandas.DataFrame` objects under
:attr:`pandas.DataFrame.element_type`, except that it returns a dictionary
mapping column names to their inferred type(s).

.. doctest::

    >>> pd.DataFrame({"a": [1, 2], "b": [1., 2.], "c": ["1", "2"]}).element_type
    {'a': NumpyInt64Type(), 'b': NumpyFloat64Type(), 'c': StringType()}
