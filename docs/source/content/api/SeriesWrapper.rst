.. currentmodule:: pdcast

pdcast.SeriesWrapper
====================

.. autoclass:: SeriesWrapper

Properties
----------

.. autosummary::
    :toctree: ../../generated/

    SeriesWrapper.series
    SeriesWrapper.hasnans
    SeriesWrapper.element_type
    SeriesWrapper.max
    SeriesWrapper.min
    SeriesWrapper.real
    SeriesWrapper.imag

Wrapped Methods
---------------
These methods are overridden by :class:`SeriesWrapper`.

.. autosummary::
    :toctree: ../../generated/

    SeriesWrapper.astype
    SeriesWrapper.copy

Additional Methods
------------------
These methods are added on top of existing ``pandas.Series`` functionality

.. autosummary::
    :toctree: ../../generated/

    SeriesWrapper.__enter__
    SeriesWrapper.__exit__
    SeriesWrapper.apply_with_errors
    SeriesWrapper.boundscheck
    SeriesWrapper.isinf
    SeriesWrapper.rectify
    SeriesWrapper.within_tol

Every other method/attribute is delegated to the wrapped
:attr:`series <SeriesWrapper.series>`.

.. _SeriesWrapper_description:

Notes
-----
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

