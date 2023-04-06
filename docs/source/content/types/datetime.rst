.. currentmodule:: pdcast

.. _datetime_types:

Datetime
========

Generic
-------

.. _datetime_type:

.. autoclass:: DatetimeType

Pandas
------

.. _pandas_timestamp_type:

.. autoclass:: PandasTimestampType

Python
------

.. _python_datetime_type:

.. autoclass:: PythonDatetimeType

Numpy
-----

.. _numpy_datetime64_type:

.. autoclass:: NumpyDatetime64Type


For datetimes and timedeltas, this mechanism is used to specify units, step
sizes, and/or timezones, enabling the following forms:

.. doctest::

    >>> pdcast.resolve_type("Timestamp[US/Pacific]")
    PandasTimestampType(tz=<DstTzInfo 'US/Pacific' LMT-1 day, 16:07:00 STD>)
    >>> pdcast.resolve_type("pydatetime[UTC]")
    PythonDatetimeType(tz=<UTC>)
    >>> pdcast.resolve_type("M8[5ns]")
    NumpyDatetime64Type(unit='ns', step_size=5)
    >>> pdcast.resolve_type("m8[s]")
    NumpyTimedelta64Type(unit='s', step_size=1)

