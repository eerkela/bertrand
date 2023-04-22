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

.. list-table::
    :header-rows: 1

    * - Argument
      - Description
      - Default
    * - :attr:`tol <convert.arguments.tol>`
      - The maximum amount of precision loss that can occur before an error is
        raised.
      - ``1e-6``
    * - :attr:`rounding <convert.arguments.rounding>`
      - The rounding rule to use for numeric conversions.
      - ``None``
    * - :attr:`unit <convert.arguments.unit>`
      - The unit to use for numeric <-> datetime/timedelta conversions.
      - ``"ns"``
    * - :attr:`step_size <convert.arguments.step_size>`
      - The step size to use for each :attr:`unit <convert.arguments.unit>`.
      - ``1``
    * - :attr:`since <convert.arguments.since>`
      - The epoch to use for datetime/timedelta conversions.
      - ``"utc"``
    * - :attr:`tz <convert.arguments.tz>`
      - Specifies a time zone to use for datetime conversions.
      - ``None``
    * - :attr:`naive_tz <convert.arguments.naive_tz>`
      - The assumed time zone when localizing naive datetimes.
      - ``None``
    * - :attr:`day_first <convert.arguments.day_first>`
      - Indicates whether to interpret the first value in an ambiguous
        3-integer date (e.g. 01/05/09) as the day (``True``) or month
        (``False``).
      - ``False``
    * - :attr:`year_first <convert.arguments.year_first>`
      - Indicates whether to interpret the first value in an ambiguous
        3-integer date (e.g. 01/05/09) as the year.
      - ``False``
    * - :attr:`as_hours <convert.arguments.as_hours>`
      - Indicates whether to interpret ambiguous MM:SS timedeltas as HH:MM.
      - ``False``
    * - :attr:`true <convert.arguments.true>`
      - A set of truthy strings to use for boolean conversions.
      - ``{"true", "t", "yes", "y", "on", "1"}``
    * - :attr:`false <convert.arguments.false>`
      - A set of falsy strings to use for boolean conversions.
      - ``{"false", "f", "no", "n", "off", "0"}``
    * - :attr:`ignore_case <convert.arguments.ignore_case>`
      - Indicates whether to ignore differences in case during string
        conversions.
      - ``True``
    * - :attr:`format <convert.arguments.format>`
      - A :ref:`format specifier <python:formatspec>` to use for string
        conversions.
      - ``None``
    * - :attr:`base <convert.arguments.base>`
      - Base to use for integer <-> string conversions, as supplied to
        :class:`int() <python:int>`.
      - ``0``
    * - :attr:`call <convert.arguments.call>`
      - TODO
      - ``None``
    * - :attr:`downcast <convert.arguments.downcast>`
      - TODO
      - ``False``
    * - :attr:`errors <convert.arguments.errors>`
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

.. note::

    :func:`cast` is an :func:`extension_func <extension_func>`, meaning that
    the default values listed above can be dynamically modified at run time.

    .. doctest::

        >>> pdcast.cast(1, "datetime")
        0   1970-01-01 00:00:00.000000001
        dtype: datetime64[ns]
        >>> pdcast.cast.unit = "s"
        >>> pdcast.cast(1, "datetime")
        0   1970-01-01 00:00:01
        dtype: datetime64[ns]
        >>> del pdcast.cast.unit
        >>> pdcast.cast(1, "datetime")
        0   1970-01-01 00:00:00.000000001
        dtype: datetime64[ns]

    Additionally, new arguments can be added by calling
    :meth:`@cast.register_arg <ExtensionFunc.register_arg>`.

    .. doctest::

        >>> @pdcast.cast.register_arg(default="bar")
        ... def foo(val: str, state: dict) -> str:
        ...     '''docstring for `foo`.'''
        ...     if val not in ("bar", "baz"):
        ...         raise ValueError(f"`foo` must be one of ('bar', 'baz')")
        ...     return val

    This allows the type's :ref:`delegated <atomic_type.conversions>`
    conversion methods to access ``foo`` simply by adding it to their call
    signature.

    .. code:: python

        def to_integer(
            self,
            series: pdcast.SeriesWrapper,
            dtype: pdcast.AtomicType,
            foo: str,
            **unused
        ) -> pdcast.SeriesWrapper:
            ...

    :func:`extension_func <extension_func>` ensures that ``foo`` is always
    passed to the conversion method, either with the default value specified in
    :meth:`register_arg <ExtensionFunc.register_arg>` or a dynamic value from
    the global configuration.

.. _cast.stand_alone:

Stand-alone conversions
-----------------------
Pandas offers a few simple convenience functions for quick conversions to
predefined data types, like :func:`pandas.to_datetime`,
:func:`pandas.to_numeric`, etc.  :func:`cast` can be used to replicate the
behavior of these functions in a more abstract way, but sometimes the simple
convenience of writing a :func:`to_datetime <pandas.to_datetime>` call is hard
to beat.

To this end, ``pdcast`` exposes its own suite of analogous methods that operate
within its expanded :doc:`type system <../types/types>`.  They inherit the same
:ref:`arguments <cast.arguments>` as above.

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

.. _cast.special_cases:

Special cases
-------------


.. _cast.mixed:

Mixed data
^^^^^^^^^^

.. _cast.adapters:

Adapters
^^^^^^^^

.. _cast.anonymous:

Anonymous conversions
^^^^^^^^^^^^^^^^^^^^^
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
directly to :class:`pandas.Series` objects under :meth:`pandas.Series.cast`,
allowing users to omit the ``data`` argument.

.. doctest::

    >>> pdcast.attach()
    >>> pd.Series([1, 2, 3]).cast("datetime")
    0   1970-01-01 00:00:00.000000001
    1   1970-01-01 00:00:00.000000002
    2   1970-01-01 00:00:00.000000003
    dtype: datetime64[ns]

A similar attribute is attached to :class:`pandas.DataFrame` objects under
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
