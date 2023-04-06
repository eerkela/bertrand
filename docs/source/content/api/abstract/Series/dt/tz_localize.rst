pandas.Series.dt.tz_localize
============================

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
