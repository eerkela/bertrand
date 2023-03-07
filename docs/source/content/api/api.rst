.. currentmodule:: pdcast

API Reference
=============
This page gives an overfiew of all public ``pdcast`` objects, functions, and
methods.  All class and functions exposed in the base ``pdcast`` namespace are
considered public.


.. toctree::
    :maxdepth: 1

    Detect & Resolve <check>
    Cast <cast>
    Types <types>
    Attach <attach>
    AtomicType <atomic>
    AdapterType <adapter>
    CompositeType <composite>
    SeriesWrapper <series>
    TypeRegistry <registry>






.. note::

    .. TODO: move this into API docs

    Precision loss checks can be distinguished from overflow by providing
    ``np.inf`` to the optional ``tol`` argument, rather than supplying
    ``errors="coerce"``.  For instance:

    .. doctest::

        >>> series.cast(float, tol=np.inf)
        0    9.223372e+18
        1    9.223372e+18
        2    9.223372e+18
        dtype: float64

    matches the original pandas output while simultaneously rejecting overflow.
