.. currentmodule:: pdcast

SeriesWrapper
=============

Properties
----------

.. autosummary::
    :toctree: ../../generated/

    SeriesWrapper.element_type
    SeriesWrapper.real
    SeriesWrapper.imag
    SeriesWrapper.series

Wrapped Methods
---------------
These methods are overridden by :class:`SeriesWrapper`.

.. autosummary::
    :toctree: ../../generated/

    SeriesWrapper.astype
    SeriesWrapper.copy
    SeriesWrapper.hasnans
    SeriesWrapper.argmax
    SeriesWrapper.argmin
    SeriesWrapper.max
    SeriesWrapper.min

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
