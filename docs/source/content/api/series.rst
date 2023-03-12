.. currentmodule:: pdcast

SeriesWrapper
=============

Constructor
-----------

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
