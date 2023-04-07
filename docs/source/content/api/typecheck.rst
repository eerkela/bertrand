.. currentmodule:: pdcast

.. testsetup::

    import numpy as np
    import pandas as pd
    import pdcast

pdcast.typecheck
================

.. autofunction:: typecheck

Hierarchical checks
-------------------
:func:`typecheck` can be used to perform ``isinstance()``-like type checks,
with many of the same semantics as its built-in equivalent.

Generic types
^^^^^^^^^^^^^
:func:`generic <generic>` types act as wildcards, which can match any of their
:func:`backends <AtomicType.register_backend>`.

.. doctest::

    >>> pdcast.typecheck(np.int64(1), "int64[numpy]")
    True
    >>> pdcast.typecheck(np.int64(1), "int64[pandas]")
    False
    >>> pdcast.typecheck(np.int64(1), "int64")
    True

Subtypes
^^^^^^^^
If ``include_subtypes=True`` (the default), then :func:`subtypes <subtype>`
will also be included in membership checks.

.. doctest::

    >>> pdcast.typecheck(np.int64(1), "int")
    True
    >>> pdcast.typecheck(np.int64(1), "int[numpy]")
    True
    >>> pdcast.typecheck(np.int64(1), "int", include_subtypes=False)
    False

Adapters
^^^^^^^^
By default, :func:`typecheck` requires an exact match for any
:class:`adapters <AdapterType>` that are attached to either the example data
or the comparison type.  For instance:

.. doctest::

    >>> pdcast.typecheck(pd.Series([1, 2, 3]), "sparse[int]")
    False
    >>> pdcast.typecheck(pd.Series([1, 2, 3], dtype="Sparse[int]"), "int")
    False
    >>> pdcast.typecheck(pd.Series([1, 2, 3], dtype="Sparse[int]"), "sparse[int]")
    True

These can be ignored by setting ``ignore_adapters=True``, which strips
:class:`adapters <AdapterType>` from both sides of the comparison.

.. doctest::

    >>> pdcast.typecheck(pd.Series([1, 2, 3]), "sparse[int]", ignore_adapters=True)
    True
    >>> pdcast.typecheck(pd.Series([1, 2, 3], dtype="Sparse[int]"), "int", ignore_adapters=True)
    True
    >>> pdcast.typecheck(pd.Series([1, 2, 3], dtype="Sparse[int]"), "sparse[int]", ignore_adapters=True)
    True

Composite checks
----------------
Just like ``isinstance()``, multiple
:ref:`type specifiers <resolve_type.type_specifiers>` can be given to compare
against.  This returns ``True`` if **any** of them match the example data.

.. doctest::

    >>> pdcast.typecheck(pd.Series([1, 2, 3]), "bool, int, float")
    True
    >>> pdcast.typecheck(pd.Series(["foo", "bar", "baz"]), "bool, int, float")
    False

If the example data consists of multiple types, then :func:`typecheck` will
return ``True`` if and only if the observed types form a **subset** of the
comparison type(s).

.. doctest::

    >>> pdcast.typecheck([True, 2, 3.0], "bool, int, float")
    True
    >>> pdcast.typecheck([True, 2, "baz"], "bool, int, float")
    False

Pandas integration
------------------
If :func:`pdcast.attach() <attach>` is invoked, this function is attached
directly to ``pandas.Series`` objects, allowing users to omit the ``data``
argument.  It is accessible under :meth:`pandas.Series.typecheck`.

.. doctest::

    >>> pdcast.attach()
    >>> pd.Series([1, 2, 3]).typecheck("int")
    True

A similar attribute is attached to ``pandas.DataFrame`` objects under
:meth:`pandas.DataFrame.typecheck`.  This version, however, can accept
dictionaries mapping column names to comparison types in its ``dtype`` field.

.. doctest::

    >>> df = pd.DataFrame({"a": [1, 2], "b": [1., 2.], "c": ["1", "2"]})
    >>> df.typecheck({"a": "int", "b": "float", "c": "string")
    True
