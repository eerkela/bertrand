.. currentmodule:: pdcast

.. testsetup::

    import numpy as np
    import pandas as pd
    import pdcast

pdcast.cast
===========

.. autofunction:: cast

.. _cast.arguments:

Arguments
---------
The behavior of this function can be customized using the following arguments.
Default values for each are stored in a thread-local
:class:`pdcast.defaults <CastDefaults>` configuration object, which can be
:meth:`updated <CastDefaults.register_arg>` at run time to add additional
entries to this list.

.. TODO: add a default column to this table and include a header.

.. list-table::
    :header-rows: 1

    * - Argument
      - Description
      - Default
    * - :attr:`tol <CastDefaults.tol>`
      - The maximum amount of precision loss that can occur before an error is
        raised.
      - ``1e-6``
    * - :attr:`rounding <CastDefaults.rounding>`
      - The rounding rule to use for numeric conversions.
      - ``None``
    * - :attr:`unit <CastDefaults.unit>`
      - The unit to use for numeric <-> datetime/timedelta conversions.
      - ``"ns"``
    * - :attr:`step_size <CastDefaults.step_size>`
      - The step size to use for each :attr:`unit <CastDefaults.unit>`.
      - ``1``
    * - :attr:`since <CastDefaults.since>`
      - The epoch to use for datetime/timedelta conversions.
      - ``"utc"``
    * - :attr:`tz <CastDefaults.tz>`
      - TODO
      - ``None``
    * - :attr:`naive_tz <CastDefaults.naive_tz>`
      - TODO
      - ``None``
    * - :attr:`day_first <CastDefaults.day_first>`
      - TODO
      - ``False``
    * - :attr:`year_first <CastDefaults.year_first>`
      - TODO
      - ``False``
    * - :attr:`as_hours <CastDefaults.as_hours>`
      - TODO
      - ``False``
    * - :attr:`true <CastDefaults.true>`
      - TODO
      - ``{"true", "t", "yes", "y", "on", "1"}``
    * - :attr:`false <CastDefaults.false>`
      - TODO
      - ``{"false", "f", "no", "n", "off", "0"}``
    * - :attr:`ignore_case <CastDefaults.ignore_case>`
      - TODO
      - ``True``
    * - :attr:`format <CastDefaults.format>`
      - TODO
      - ``None``
    * - :attr:`base <CastDefaults.base>`
      - TODO
      - ``0`` [#base]_
    * - :attr:`call <CastDefaults.call>`
      - TODO
      - ``None``
    * - :attr:`downcast <CastDefaults.downcast>`
      - TODO
      - ``False``
    * - :attr:`errors <CastDefaults.errors>`
      - TODO
      - ``"raise"``

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

.. [#base] as supplied to `python
    <https://docs.python.org/3/library/functions.html#int>`_ ``int()``.

:class:`pdcast.defaults <CastDefaults>` also allows users to dynamically modify
the default values of these arguments, altering the behavior of :func:`cast` on
a global basis.

.. doctest::

  >>> pdcast.cast(1, "datetime")
  0   1970-01-01 00:00:00.000000001
  dtype: datetime64[ns]
  >>> pdcast.defaults.unit = "s"
  >>> pdcast.cast(1, "datetime")
  0   1970-01-01 00:00:01
  dtype: datetime64[ns]

.. _cast.stand_alone:

Stand-alone conversions
-----------------------
Internally, :func:`cast` calls a selection of stand-alone conversion functions,
similar to ``pandas.to_datetime()``, ``pandas.to_numeric()``, etc.  These are
available for use under the ``pdcast`` namespace if desired, mirroring their
pandas equivalents.  They inherit the same :ref:`arguments <cast.arguments>` as
above.

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

.. _cast.anonymous:

Anonymous conversions
---------------------
:func:`cast` can be used without an explicit ``dtype`` argument.  In this case,
the :func:`inferred <detect_type>` type of the data will be used instead,
allowing users to seamlessly convert existing data structures into the
``pdcast`` type system.

.. doctest::

    >>> from decimal import Decimal
    >>> series = pd.Series([Decimal(1), Decimal(2), Decimal(3)])
    >>> series
    0    1
    1    2
    2    3
    dtype: object
    >>> pdcast.cast(series)
    0    1
    1    2
    2    3
    dtype: decimal

This also applies to naked :class:`adapters <AdapterType>`, which are those
without an associated :class:`AtomicType`.  In this case, the adapter will be
wrapped around the inferred data type.

.. doctest::

    >>> series = pd.Series([1, 2, 3])
    >>> series
    0    1
    1    2
    2    3
    dtype: int64
    >>> pdcast.cast(series, "sparse")
    0    1
    1    2
    2    3
    dtype: Sparse[int64, <NA>]

.. _cast.pandas:

Pandas integration
------------------
If :func:`pdcast.attach() <attach>` is invoked, this function is attached
directly to ``pandas.Series`` objects under :meth:`pandas.Series.cast`,
allowing users to omit the ``data`` argument.

.. doctest::

    >>> pdcast.attach()
    >>> pd.Series([1, 2, 3]).cast("datetime")
    0   1970-01-01 00:00:00.000000001
    1   1970-01-01 00:00:00.000000002
    2   1970-01-01 00:00:00.000000003
    dtype: datetime64[ns]

A similar attribute is attached to ``pandas.DataFrame`` objects under
:meth:`pandas.DataFrame.cast`, except that it can also accept dictionaries
mapping column names to type specifiers as its ``dtype`` argument.  Any column
that does not appear in such a mapping will be ignored during conversion.

.. doctest::

    >>> df = pd.DataFrame({"a": [1, 0], "b": [1.0, 0.0], "c": ["y", "n"]})
    >>> df
       a    b  c
    0  1  1.0  y
    1  0  0.0  n
    >>> df.cast({"a": "datetime", "c": "bool"}, unit="D")
               a    b      c
    0 1970-01-02  1.0   True
    1 1970-01-01  0.0  False
