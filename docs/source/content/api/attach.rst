.. currentmodule:: pdcast

Attach
======
If :func:`pdcast.attach() <attach>` is invoked, each of these methods will be
attached directly to ``pandas.Series`` and ``pandas.DataFrame`` objects under
the given endpoints.  The only exception is the :func:`@dispatch() <dispatch>`
decorator itself, which allows users to add new methods to this list.

These methods may not be defined for all types.  If a type does not define a
method, it will default to the standard pandas implementation if one exists.
See each method's description for a list of types that it applies to.  If there
is any doubt, an explicit check can be performed by testing whether a series'
``.element_type`` has an attribute of the same name.  If this is the case, then
the overloaded definition will be used for that series.

.. doctest:: attach

    >>> import pandas as pd
    >>> import pdcast; pdcast.attach()
    >>> hasattr(pd.Series([1, 2, 3]).element_type, "round")
    True
    >>> hasattr(pd.Series(["a", "b", "c"]).element_type, "round")
    False

If a series contains elements of more than one type, then each type must be
checked individually.

.. doctest:: attach

    >>> series = pd.Series([True, 2, 3., "4"], dtype="O")
    >>> series.element_type   # doctest: +SKIP
    CompositeType({bool, int, float, string})
    >>> [hasattr(x, "round") for x in series.element_type]  # doctest: +SKIP
    [True, True, True, False]

If :func:`pdcast.detach() <detach>` is invoked, all of the above functionality
will be removed, leaving ``pandas.Series`` and ``pandas.DataFrame`` objects in
their original form, the same as they were before
:func:`pdcast.attach() <attach>` was called.

.. doctest:: attach

    >>> pdcast.detach()
    >>> pd.Series([1, 2, 3]).cast("float")
    Traceback (most recent call last):
        ...
    AttributeError: 'Series' object has no attribute 'cast'. Did you mean: 'last'?

.. _attach:

.. autofunction:: attach

.. _detach:

.. autofunction:: detach

.. _dispatch:

.. autodecorator:: dispatch

.. method:: pandas.Series.cast(*args, **kwargs) -> pandas.Series:

    An attached version of :func:`cast` that allows users to omit the ``data``
    argument.

    This method is defined for every type.

.. method:: pandas.Series.typecheck(*args, **kwargs) -> pandas.Series:

    An attached version of :func:`typecheck` that allows users to omit the
    ``data`` argument.

    This method is defined for every type.

.. property:: pandas.Series.element_type

    The inferred element type of the series.  This is equivalent to running
    :func:`detect_type` on the series.

    This property is defined for every type.

.. method:: pandas.Series.round(decimals: int = 0, rule: str = "half_even") -> pandas.Series:
    :abstractmethod:

    Customizable rounding for numeric series objects.

    :param decimals:
        The number of decimal places to round to.  0 indicates rounding in
        the ones position.  Positive values count to the right of the decimal
        point (if one exists) and negative values count to the left.
    :type decimals: int, default 0
    :param rule:
        The `rounding strategy <https://en.wikipedia.org/wiki/Rounding#Rounding_to_integer>`_
        to use.  Must be one of "floor", "ceiling", "down", "up", "half_floor",
        "half_ceiling", "half_down", "half_up", or "half_even".
    :type rule: str, default "half_even"
    :return:
        A series of the same type as the input, rounded to the given number of
        decimal places.
    :rtype: pandas.Series
    :raise ValueError:
        If ``rule`` does not match one of the available options.

    **Notes**

    This is an abstract method that is dispatched using the
    :func:`@dispatch() <dispatch>` decorator.  It is defined for all
    :ref:`integer <integer_types>`, :ref:`float <float_types>`,
    :ref:`complex <complex_types>`, and :ref:`decimal <decimal_types>` types.
    If a type does not implement this method, it will default back to the
    original pandas implementation.

    The available rounding rules are as follows:

        #.  ``"floor"`` - round toward negative infinity.
        #.  ``"ceiling"``` - round toward positive infinity.
        #.  ``"down"`` - round toward zero.
        #.  ``"up"`` - round away from zero.
        #.  ``"half_floor"`` - round to nearest with halves toward negative
            infinity.
        #.  ``"half_ceiling"`` - round to nearest with halves toward positive
            infinity.
        #.  ``"half_down"`` - round to nearest with halves toward zero.
        #.  ``"half_up"`` - round to nearest with halves away from zero.
        #.  ``"half_even"`` - Round to nearest with halves toward the nearest
            even value.  This is the default, matching the behavior of the
            original ``pandas.Series.round()`` implementation.  Also known as
            banker's rounding.  

    **Examples**

    The standard ``pandas.Series.round()`` implementation fails in some cases:

    .. doctest::

        >>> import pandas as pd
        >>> pd.Series([-1.2, -2.5, 3.5, 4.7], dtype="O").round()
        Traceback (most recent call last):
            ...
        TypeError: loop of ufunc does not support argument 0 of type float which has no callable rint method

    Attaching this method corrects this:

    .. doctest::

        >>> import pdcast; pdcast.attach()
        >>> pd.Series([-1.2, -2.5, 3.5, 4.7], dtype="O").round()
        0   -1.0
        1   -2.0
        2    4.0
        3    5.0
        dtype: float[python]

    Here are some examples of the various rounding rules:

    .. doctest::

        >>> series = pd.Series([-1.2, -2.5, 3.5, 4.7])
        >>> series.round(rule="floor")
        0   -2.0
        1   -3.0
        2    3.0
        3    4.0
        dtype: float64
        >>> series.round(rule="ceiling")
        0   -1.0
        1   -2.0
        2    4.0
        3    5.0
        dtype: float64
        >>> series.round(rule="down")
        0   -1.0
        1   -2.0
        2    3.0
        3    4.0
        dtype: float64
        >>> series.round(rule="up")
        0   -2.0
        1   -3.0
        2    4.0
        3    5.0
        dtype: float64
        >>> series.round(rule="half_floor")
        0   -1.0
        1   -3.0
        2    3.0
        3    5.0
        dtype: float64
        >>> series.round(rule="half_ceiling")
        0   -1.0
        1   -2.0
        2    4.0
        3    5.0
        dtype: float64
        >>> series.round(rule="half_down")
        0   -1.0
        1   -2.0
        2    3.0
        3    5.0
        dtype: float64
        >>> series.round(rule="half_up")
        0   -1.0
        1   -3.0
        2    4.0
        3    5.0
        dtype: float64
        >>> series.round(rule="half_even")
        0   -1.0
        1   -2.0
        2    4.0
        3    5.0
        dtype: float64

    For integers, the ``decimals`` argument must be negative for this method
    to have any effect.

    .. doctest::

        >>> pd.Series([1, 2, 3]).round(-1, rule="up")
        0    10
        1    10
        2    10
        dtype: int64

.. method:: pandas.Series.dt.tz_localize(tz: str | pytz.timezone | None) -> pandas.Series:
    :abstractmethod:

    Localize timezone-naive datetimes to the given timezone.

    :param tz:
        The timezone to localize to.  If this is ``None``, datetimes will be
        converted to naive format.
    :type tz: str | pytz.timezone | None
    :return:
        A series of the same type as the input, localized to the given
        timezone.
    :rtype: pandas.Series
    :raise pytz.exceptions.UnknownTimeZoneError:
        If ``tz`` is invalid.
    :raise TypeError:
        If the datetimes are already aware.  Use
        :meth:`Series.dt.tz_convert() <pandas.Series.dt.tz_convert>` to convert
        between timezones instead.

    .. seealso::

        :meth:`Series.dt.tz_convert <pandas.Series.dt.tz_convert>`
            Conversion between timezones.

    **Notes**

    This is an abstract method that is dispatched using the
    :func:`@dispatch() <dispatch>` decorator.  It is defined for
    :class:`PandasTimestampType` and :class:`PythonDatetimeType`.  If a type
    does not implement this method, it will default back to the original pandas
    implementation.

    This method allows python datetimes to use the same localization syntax as
    ``pandas.Timestamp`` objects.

    **Examples**

    .. doctest::

        >>> from datetime import datetime
        >>> import pandas as pd
        >>> import pdcast; pdcast.attach()
        >>> series = pd.Series([datetime(2022, 1, 12), datetime(2022, 3, 27)], dtype="O")
        >>> series.dt.tz_localize("US/Pacific")
        0    2022-01-12 00:00:00-08:00
        1    2022-03-27 00:00:00-07:00
        dtype: datetime[python, US/Pacific]
        >>> _[0]
        datetime.datetime(2022, 1, 12, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)

.. method:: pandas.Series.dt.tz_convert(tz: str | pytz.timezone | None) -> pandas.Series:
    :abstractmethod:

    Convert timezone-aware datetimes to the given timezone.

    :param tz:
        The timezone to convert to.  If this is ``None``, datetimes will be
        converted to UTC and then returned in naive format.
    :type tz: str | pytz.timezone | None
    :return:
        A series of the same type as the input, converted to the given
        timezone.
    :rtype: pandas.Series
    :raise pytz.exceptions.UnknownTimeZoneError:
        If ``tz`` is invalid.
    :raise TypeError:
        If the datetimes are naive.  Use
        :meth:`Series.dt.tz_localize() <pandas.Series.dt.tz_localize>` to
        localize to a given timezone instead.

    .. seealso::

        :meth:`Series.dt.tz_localize <pandas.Series.dt.tz_localize>`
            Localize to a given timezone.

    **Notes**

    This is an abstract method that is dispatched using the
    :func:`@dispatch() <dispatch>` decorator.  It is defined for
    :class:`PandasTimestampType` and :class:`PythonDatetimeType`.  If a type
    does not implement this method, it will default back to the original pandas
    implementation.

    This method allows python datetimes to use the same conversion syntax as
    ``pandas.Timestamp`` objects.

    **Examples**

    .. doctest::

        >>> from datetime import datetime
        >>> import pandas as pd
        >>> import pdcast; pdcast.attach()
        >>> series = pd.Series([datetime(2022, 1, 12), datetime(2022, 3, 27)], dtype="O")
        >>> series.dt.tz_localize("US/Pacific")
        0    2022-01-12 00:00:00-08:00
        1    2022-03-27 00:00:00-07:00
        dtype: datetime[python, US/Pacific]
        >>> _.dt.tz_convert("US/Eastern")
        0    2022-01-12 03:00:00-05:00
        1    2022-03-27 03:00:00-04:00
        dtype: datetime[python, US/Eastern]
        >>> _[0]
        datetime.datetime(2022, 1, 12, 3, 0, tzinfo=<DstTzInfo 'US/Eastern' EST-1 day, 19:00:00 STD>)
