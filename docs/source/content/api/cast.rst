.. currentmodule:: pdcast

.. testsetup::

    import numpy as np
    import pandas as pd
    import pdcast

.. TODO: remove arg descriptions from cast(), just link to arguments section.

pdcast.cast
===========

.. autofunction:: cast

.. _cast.arguments:

Arguments
---------
The behavior of each conversion can be customized using the following
arguments.  Default values for each are stored in a global
:class:`pdcast.defaults <CastDefaults>` configuration object, which can be
updated at run time.

.. list-table::

    * - :attr:`tol <CastDefaults.tol>`
      - The maximum amount of precision loss that can occur before an error is
        raised.
    * - :attr:`rounding <CastDefaults.rounding>`
      - The rounding rule to use for numeric conversions.
    * - :attr:`unit <CastDefaults.unit>`
      - The unit to use for numeric <-> datetime/timedelta conversions.
    * - :attr:`step_size <CastDefaults.step_size>`
      - The step size to use for each :attr:`unit <CastDefaults.unit>`.
    * - :attr:`since <CastDefaults.since>`
      - The epoch to use for datetime/timedelta conversions.
    * - :attr:`tz <CastDefaults.tz>`
      - TODO
    * - :attr:`naive_tz <CastDefaults.naive_tz>`
      - TODO
    * - :attr:`day_first <CastDefaults.day_first>`
      - TODO
    * - :attr:`year_first <CastDefaults.year_first>`
      - TODO
    * - :attr:`as_hours <CastDefaults.as_hours>`
      - TODO
    * - :attr:`true <CastDefaults.true>`
      - TODO
    * - :attr:`false <CastDefaults.false>`
      - TODO
    * - :attr:`ignore_case <CastDefaults.ignore_case>`
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

.. toctree::
    :hidden:

    tol <abstract/cast/tol>
    rounding <abstract/cast/rounding>
    unit <abstract/cast/unit>
    step_size <abstract/cast/step_size>
    since <abstract/cast/since>
    tz <abstract/cast/tz>
    naive_tz <abstract/cast/naive_tz>
    day_first <abstract/cast/day_first>
    year_first <abstract/cast/year_first>
    as_hours <abstract/cast/as_hours>
    true <abstract/cast/true>
    false <abstract/cast/false>
    ignore_case <abstract/cast/ignore_case>
    format <abstract/cast/format>
    base <abstract/cast/base>
    call <abstract/cast/call>
    downcast <abstract/cast/downcast>
    errors <abstract/cast/errors>

.. _cast.stand_alone:

Stand-alone conversions
-----------------------
Internally, :func:`cast` calls a selection of stand-alone conversion functions,
similar to ``pandas.to_datetime()``, ``pandas.to_numeric()``, etc.  These are
available for public use at the package level, mirroring their pandas
equivalents.  They inherit the same :ref:`arguments <cast.arguments>` as above.

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


.. _cast.mixed:

Mixed data
----------

.. _cast.adapters:

Adapters
--------

.. _cast.inference:

Inference
---------

