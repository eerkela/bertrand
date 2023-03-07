.. TODO: move this entirely into API docs

Implementation
==============

Below is a rough overview of the internal data structures ``pdcast`` uses to
process data, with condensed descriptions for their important functionality.
This is intended to provide increased context for those seeking to extend
``pdcast`` in some way, or those simply interested in learning how it works.
If a question is not answered by this document, check the API docs for more
information.

AdapterType
-----------
AdapterTypes are types that modify other types.  They can dynamically wrap
``AtomicType`` objects to change their behavior on a programmatic basis and
are not cached as flyweights like ``AtomicType``\s are.

``AdapterType``\s are examples of the Gang of Four `Decorator Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_
(not to be confused with the built-in python ``@decorator`` syntax).  This
leverages `composition over inheritance <https://en.wikipedia.org/wiki/Composition_over_inheritance>`_,
to prevent subclass explosions for type objects.  They can be nested to form
a singly-linked list that sits on top of the base ``AtomicType`` object,
successively wrapping it to modify its behavior.

Common use cases for ``AdapterType``\s include sparse and categorical data.
These are handled by ``pdcast.SparseType`` and ``pdcast.CategoricalType``
respectively, which can be applied to any ``AtomicType`` object to convert it
into a sparse/categorical representation.  They are designed to be as
minimally-intrusive as possible, and can be constructed in the same way as
``AtomicTypes`` via the type-specification mini-language, as shown:

.. doctest::

    >>> import pdcast
    >>> pdcast.resolve_type("sparse[float32]")
    SparseType(wrapped=Float32Type(), fill_value=nan)
    >>> pdcast.resolve_type("categorical[string]")
    CategoricalType(wrapped=StringType(), levels=None)
    >>> pdcast.resolve_type("sparse[categorical[bool]]")
    SparseType(wrapped=CategoricalType(wrapped=BooleanType(), levels=None), fill_value=<NA>)

If you'd like to write your own ``AdapterType``, the best place to start would
be to examine these types to see how they work.

CompositeType
-------------
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
    array([BooleanType(), IntegerType(), FloatType(), ComplexType()],
          dtype=object)

This can be used during ``pd.Series.groupby()`` operations to apply functions
by type, rather than all at once.

.. _SeriesWrapper_description:

SeriesWrapper
-------------
``SeriesWrapper``\s are `Decorator Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_
wrappers around ``pd.Series`` objects.  They provide a standardized format for
dispatch and conversion methods, and are aware of their type via
``pdcast.detect_type()``.  They are guaranteed to have certain qualities, which
help to abstract away many common problems that can arise with dispatched
logic.  These include:

*   **No missing values**.  These are filtered out before passing on to the
    dispatched method, and are reintroduced dynamically after it is executed.
    This means that **any values that are dropped from a** ``SeriesWrapper``
    **instance will be implicitly replaced with missing values**.  This is
    intended behavior, allowing for easy coercion to NAs while maintaining the
    *no missing values* status of SeriesWrapper objects.
*   **A unique RangeIndex** with no duplicates.  The missing value filtering
    step above requires ``SeriesWrapper``\s to be aligned on index, meaning they
    are not allowed to have duplicate values.  This is handled internally, and
    the original index is automatically replaced after missing values are
    reintroduced, leaving it unchanged.  This also allows non-homogenous series
    data to be processed, with each group being successively joined into the
    final result based on index.
*   **No sparse/categorical input**.  If a sparse or categorical extension
    series is provided to ``SeriesWrapper``, it will be densified and stripped
    of categorical labels before being passed to dispatch logic.  This
    increases coverage for edge cases where these representations may present
    inconsistent behavior.  If a dispatched method is invoked on one of these
    objects and it does not change the element type of the series, then the
    wrappers will be reapplied programmatically, in the same way as an
    equivalent ``cast()`` operation.  Again, this is done automatically in the
    background whenever a dispatched method is called.

In addition, ``SeriesWrapper``\s provide some utilities to make writing
conversions and dispatch methods easier.  These include:

*   **Caching for extreme values**, allowing ``SeriesWrapper``\s to skip
    ``min()`` and ``max()`` calculations to evaluate range.
*   **Boundschecks** for bounded data types, which include standard
    error-handling rules.
*   **Integer coercion** for converting real numbers to integer format, again
    with built-in error-handling rules.
*   **Loops for applying custom logic** over series values while accounting
    for errors.

In every other respect, they are identical to normal ``pd.Series`` objects,
and can be used as such.
