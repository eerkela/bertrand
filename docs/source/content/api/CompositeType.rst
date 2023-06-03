.. currentmodule:: pdcast

.. _CompositeType:

pdcast.CompositeType
====================

.. autoclass:: CompositeType

Add/Remove
----------

.. autosummary::
    :toctree: ../../generated/

    CompositeType.add
    CompositeType.remove
    CompositeType.clear
    CompositeType.pop
    CompositeType.discard

Subtypes/Supertypes
-------------------

.. autosummary::
    :toctree: ../../generated/

    CompositeType.expand
    CompositeType.collapse
    CompositeType.subtypes

Comparisons
-----------

.. autosummary::
    :toctree: ../../generated/

    CompositeType.union
    CompositeType.intersection
    CompositeType.difference
    CompositeType.symmetric_difference

Membership
----------

.. autosummary::
    :toctree: ../../generated/

    CompositeType.contains
    CompositeType.issubset
    CompositeType.issuperset
    CompositeType.isdisjoint

In-place Updates
----------------

.. autosummary::
    :toctree: ../../generated/

    CompositeType.update
    CompositeType.intersection_update
    CompositeType.difference_update
    CompositeType.symmetric_difference_update

Special Methods
---------------

.. autosummary::
    :toctree: ../../generated/

    CompositeType.__and__
    CompositeType.__contains__
    CompositeType.__eq__
    CompositeType.__ge__
    CompositeType.__gt__
    CompositeType.__iand__
    CompositeType.__ior__
    CompositeType.__isub__
    CompositeType.__iter__
    CompositeType.__ixor__
    CompositeType.__le__
    CompositeType.__lt__
    CompositeType.__len__
    CompositeType.__or__
    CompositeType.__repr__
    CompositeType.__str__
    CompositeType.__sub__
    CompositeType.__xor__

Notes
-----
``CompositeType``\s are set-like containers for ``ScalarType`` and
``DecoratorType`` objects.  They implement a standard set interface, with all the
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
``ScalarType.contains()``.

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

