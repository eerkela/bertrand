.. currentmodule:: pdcast

.. testsetup::

    import numpy as np
    import pandas as pd
    import pdcast

.. TODO: remove arg descriptions from cast(), just link to arguments section.

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

.. list-table::

    * - :attr:`tol <CastDefaults.tol>`
      - The maximum amount of precision loss that can occur before an error is
        raised.
    * - :attr:`rounding <CastDefaults.rounding>`
      - TODO
    * - :attr:`unit <CastDefaults.unit>`
      - TODO
    * - :attr:`step_size <CastDefaults.step_size>`
      - TODO
    * - :attr:`since <CastDefaults.since>`
      - TODO
    * - :attr:`tz <CastDefaults.tz>`
      - TODO
    * - :attr:`true <CastDefaults.true>`
      - TODO
    * - :attr:`false <CastDefaults.false>`
      - TODO
    * - :attr:`ignore_case <CastDefaults.ignore_case>`
      - TODO
    * - :attr:`day_first <CastDefaults.day_first>`
      - TODO
    * - :attr:`year_first <CastDefaults.year_first>`
      - TODO
    * - :attr:`utc <CastDefaults.utc>`
      - TODO
    * - :attr:`as_hours <CastDefaults.as_hours>`
      - TODO
    * - :attr:`format <CastDefaults.format>`
      - TODO
    * - :attr:`base <CastDefaults.base>`
      - TODO
    * - :attr:`call <CastDefaults.call>`
      - TODO
    * - :attr:`downcast <CastDefaults.downcast>`
      - TODO
    * - :attr:`errors <CastDefaults.errors>`
      - TODO
    * - :attr:`\*\*kwargs`
      - TODO

.. toctree::
    :hidden:

    tol <abstract/cast/tol>
    rounding <abstract/cast/rounding>
    unit <abstract/cast/unit>
    step_size <abstract/cast/step_size>
    since <abstract/cast/since>
    tz <abstract/cast/tz>
    true <abstract/cast/true>
    false <abstract/cast/false>
    ignore_case <abstract/cast/ignore_case>
    day_first <abstract/cast/day_first>
    year_first <abstract/cast/year_first>
    utc <abstract/cast/utc>
    as_hours <abstract/cast/as_hours>
    format <abstract/cast/format>
    base <abstract/cast/base>
    call <abstract/cast/call>
    downcast <abstract/cast/downcast>
    errors <abstract/cast/errors>

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
