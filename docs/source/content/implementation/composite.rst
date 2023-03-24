CompositeType
=============
``CompositeType``\s are set-like containers for ``AtomicType`` and
``AdapterType`` objects.  They implement a standard set interface, with all the
same methods as the built in ``set`` type.  They can be constructed by
providing multiple type specifiers to ``pdcast.resolve_type()``, either in
comma-separated string form or by providing an iterable.

.. doctest::

    >>> import numpy as np
    >>> import pdcast
    >>> pdcast.resolve_type("bool, int, float")   # doctest: +SKIP
    CompositeType({bool, int, float})
    >>> pdcast.resolve_type([np.dtype("f2"), "sparse[datetime]", str])   # doctest: +SKIP
    CompositeType({float16[numpy], sparse[datetime, NaT], string})

``CompositeType`` objects implicitly include subtypes for each of their
elements, and automatically resolve comparison targets.  This allows for easy
membership tests that account for subtypes in the same way as
``AtomicType.contains()``.

.. doctest::

    >>> import pdcast
    >>> pdcast.resolve_type("int, float").contains("int8[pandas]")
    True
    >>> pdcast.resolve_type("int, float") - "float64"   # doctest: +SKIP
    CompositeType({int, float16, float32, float80})

They can also be constructed by providing non-homogenous example data to
``pdcast.detect_type()``, as shown:

.. doctest::

    >>> import pdcast
    >>> pdcast.detect_type([False, 1, 2.3, 4+5j])   # doctest: +SKIP
    CompositeType({bool, float, complex, int})

In this case, the resulting ``CompositeType`` also includes a special
``.index`` attribute, which keeps track of the type's position in the original
data.

.. doctest::

    >>> import pdcast
    >>> pdcast.detect_type([False, 1, 2.3, 4+5j]).index
    array([PythonBooleanType(), PythonIntegerType(), PythonFloatType(),
           PythonComplexType()], dtype=object)

This can be used during ``pd.Series.groupby()`` operations to apply functions
by type, rather than all at once.
