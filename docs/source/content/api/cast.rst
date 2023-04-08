.. currentmodule:: pdcast

.. testsetup::

    import numpy as np
    import pandas as pd
    import pdcast

pdcast.cast
===========

.. autofunction:: cast

.. _cast.stand_alone:

Stand-alone conversions
-----------------------
Internally, :func:`cast` calls a selection of stand-alone conversion functions,
similar to ``pandas.to_datetime()``, ``pandas.to_numeric()``, etc.  ``pdcast``
makes these available for public use at the package level, mirroring their
pandas equivalents.  Each one explicitly lists the
:ref:`arguments <cast.arguments>` it can accept.

.. autosummary::
    :toctree: ../../generated/

    to_boolean
    to_integer
    to_float
    to_complex
    to_decimal
    to_datetime
    to_timedelta
    to_string
    to_object

.. _cast.arguments:

Arguments
---------
The behavior of each conversion can be customized using the following
arguments.

.. _cast.arguments.tol:

tol
^^^
This argument represents the maximum amount of precision loss that can occur
before a ``ValueError`` is thrown.

Precision loss is defined using a 2-sided window around each of the observed
values.  The size of this window is controlled by this argument.  If a
conversion causes any value to be coerced outside this window, then a
``ValueError`` will be raised

.. doctest::

    >>> pdcast.cast(1.001, "int", tol=0.01)
    0    1
    dtype: int64
    >>> pdcast.cast(1.001, "int", tol=0)
    Traceback (most recent call last):
        ...
    ValueError: precision loss exceeds tolerance 0 at index [0]

The input to this argument must be positive.  Supplying ``np.inf`` disables
precision loss checks entirely.

.. note::

    If ``tol`` is given as a complex value, the real and imaginary parts will
    be considered separately.

    .. doctest::

        >>> pdcast.cast(1.001+0.001j, "int", tol=0.01+0.01j)
        0    1
        dtype: int64
        >>> pdcast.cast(1.001+0.001j, "int", tol=0.01+0j)
        Traceback (most recent call last):
            ...
        ValueError: imaginary component exceeds tolerance 0 at index [0]

.. note::

    This argument also controls the maximum amount of precision loss that can
    occur when :ref:`downcasting <cast.arguments.downcast>` numeric values.

    .. doctest::

        >>> pdcast.cast(1.1, "float", tol=0, downcast=True)
        0    1.1
        dtype: float64
        >>> pdcast.cast(1.1, "float", tol=0.001, downcast=True)
        0    1.099609
        dtype: float16

.. TODO: maybe include overflow in tolerance checks?  Slightly more complicated
    than snap_round()

    .. note::

        Precision loss checks can be distinguished from overflow by providing
        ``np.inf`` to ``tol``.  For instance:

        .. doctest::

            >>> series.cast(float, tol=np.inf)
            0    9.223372e+18
            1    9.223372e+18
            2    9.223372e+18
            dtype: float64

        matches the original pandas output while simultaneously rejecting overflow.

.. _cast.arguments.rounding:

rounding
^^^^^^^^
Optional rounding for numeric conversions.  This is independent from ``tol``,
and can accept a variety of rules:

    *   ``None`` - do not round.
    *   ``"floor"`` - round toward negative infinity.
    *   ``"ceiling"`` - round toward positive infinity.
    *   ``"down"`` - round toward zero.
    *   ``"up"`` - round away from zero.
    *   ``"half_floor"`` - round to nearest with ties toward positive infinity.
    *   ``"half_ceiling"`` - round to nearest with ties toward negative
        infinity.
    *   ``"half_down"`` - round to nearest with ties toward zero.
    *   ``"half_up"`` - round to nearest with ties away from zero.
    *   ``"half_even"`` - round to nearest with ties toward the `nearest even
        value <https://en.wikipedia.org/wiki/Rounding#Rounding_half_to_even>`_.
        Also known as *convergent rounding*, *statistician's rounding*, or
        *banker's rounding*.

.. doctest::

    >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="floor")
    0   -2
    1   -1
    2    0
    3    1
    dtype: int64
    >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="ceiling")
    0   -1
    1    0
    2    1
    3    2
    dtype: int64
    >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="down")
    0   -1
    1    0
    2    0
    3    1
    dtype: int64
    >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="up")
    0   -2
    1   -1
    2    1
    3    2
    dtype: int64
    >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_floor")
    0   -2
    1   -1
    2    0
    3    2
    dtype: int64
    >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_ceiling")
    0   -1
    1    0
    2    0
    3    2
    dtype: int64
    >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_down")
    0   -1
    1    0
    2    0
    3    2
    dtype: int64
    >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_up")
    0   -2
    1   -1
    2    0
    3    2
    dtype: int64
    >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_even")
    0   -2
    1    0
    2    0
    3    2
    dtype: int64

.. _cast.arguments.unit:

unit
^^^^
The unit to use for numeric <-> datetime/timedelta conversions.  The available
options are




.. _cast.arguments.step_size:

step_size
^^^^^^^^^

.. _cast.arguments.tz:

tz
^^

.. _cast.arguments.since:

since
^^^^^

.. _cast.arguments.true:

true
^^^^

.. _cast.arguments.false:

false
^^^^^

.. _cast.arguments.utc:

utc
^^^

.. _cast.arguments.day_first:

day_first
^^^^^^^^^

.. _cast.arguments.year_first:

year_first
^^^^^^^^^^

.. _cast.arguments.as_hours:

as_hours
^^^^^^^^

.. _cast.arguments.format:

format
^^^^^^

.. _cast.arguments.base:

base
^^^^

.. _cast.arguments.downcast:

downcast
^^^^^^^^

.. _cast.arguments.call:

call
^^^^

.. _cast.arguments.errors:

errors
^^^^^^

.. _cast.arguments.\*\*kwargs:

\*\*kwargs
^^^^^^^^^^
Additional keyword arguments that are passed to the
:ref:`delegated <atomic_type.conversions>` conversion method.  This allows
types to define new arguments for :func:`cast` and its related
:ref:`stand-alone <cast.stand_alone>` equivalents, which are applied only to
those conversions that need them.

.. _cast.defaults:

Defaults
--------

.. autoclass:: CastDefaults
