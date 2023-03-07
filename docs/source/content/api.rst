API Reference
=============

.. currentmodule:: pdcast


Detect & Infer
--------------

.. autosummary::
    :toctree: ../generated/

    typecheck
    detect_type
    resolve_type

Cast
----

.. autosummary::
    :toctree: ../generated/

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
