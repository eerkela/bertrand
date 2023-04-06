.. currentmodule:: pdcast

pdcast.cast
===========

.. autosummary::
    :toctree: ../../generated/

    cast
    to_boolean
    to_integer
    to_float
    to_complex
    to_decimal
    to_datetime
    to_timedelta
    to_string
    to_object

Defaults
--------

.. autoclass:: CastDefaults


Arguments
---------

tol
---
This argument represents the maximum amount of precision loss that can occur
before a ``ValueError`` is thrown.

rounding
--------








.. TODO: insert an Options section here with headings for every argument to
    the various to_x functions.  In the docstrings of each function, just
    link to this index for more information.

.. note::

    .. TODO: put this somewhere

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



