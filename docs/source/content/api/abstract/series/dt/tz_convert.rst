pandas.Series.dt.tz_convert
===========================

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

