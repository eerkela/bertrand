"""Convert datetime objects into different datetime representations.

Functions
---------
datetime_to_pandas_timestamp(
    arg: datetime_like | np.ndarray | pd.Series
) -> pd.Timestamp | np.ndarray | pd.Series:
    Convert datetime objects to `pandas.Timestamp`.

datetime_to_pydatetime(
    arg: datetime_like | np.ndarray | pd.Series
) -> datetime.datetime | np.ndarray | pd.Series:
    Convert datetime objects to `datetime.datetime`.

datetime_to_numpy_datetime64(
    arg: datetime_like | np.ndarray | pd.Series,
    unit: str = None,
    rounding: str = "down"
) -> np.datetime64 | np.ndarray | pd.Series:
    Convert datetime objects to `numpy.datetime64`.

datetime_to_datetime(
    arg: datetime_like | np.ndarray | pd.Series
) -> datetime_like | np.ndarray | pd.Series:
    Convert datetime objects to their highest resolution representation.

Examples
--------
Converting arbitrary datetimes into `pandas.Timestamp` objects:

>>> import pytz
>>> datetime_to_pydatetime(pd.Timestamp("1970-01-01 00:00:00"))
datetime.datetime(1970, 1, 1, 0, 0)
>>> datetime_to_pydatetime(
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00")
... )
datetime.datetime(1970, 1, 1, 0, 0)
>>> datetime_to_pydatetime(np.datetime64("1970-01-01 00:00:00"))
datetime.datetime(1970, 1, 1, 0, 0)
>>> datetime_to_pydatetime(
...     pd.Timestamp("1970-01-01 00:00:00", tz="US/Pacific")
... )
datetime.datetime(1970, 1, 1, 8, 0)
>>> datetime_to_pydatetime(
...     pytz.timezone("US/Pacific").localize(
...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
...     )
... )
datetime.datetime(1970, 1, 1, 8, 0)
>>> datetime_to_pydatetime(
...     pd.Timestamp("1970-01-01 00:00:00"),
...     tz="US/Pacific"
... )
datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     tz="US/Pacific"
... )
datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     pd.Timestamp("1970-01-01 00:00:00"),
...     tz="US/Pacific",
...     utc=True
... )
datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     tz="US/Pacific",
...     utc=True
... )
datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     np.datetime64("1970-01-01 00:00:00"),
...     tz="US/Pacific"
... )
datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     pd.Timestamp("1970-01-01 00:00:00", tz="Europe/Berlin"),
...     tz="US/Pacific"
... )
datetime.datetime(1969, 12, 31, 15, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     pytz.timezone("Europe/Berlin").localize(
...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
...     ),
...     tz="US/Pacific"
... )
datetime.datetime(1969, 12, 31, 15, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_objects = [
...     pd.Timestamp("1970-01-01 00:00:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     np.datetime64("1970-01-01 00:00:00")
... ]
>>> datetime_objects
[Timestamp('1970-01-01 00:00:00'), datetime.datetime(1970, 1, 1, 0, 0), numpy.datetime64('1970-01-01T00:00:00')]
>>> datetime_to_pydatetime(
...     pd.Series(datetime_objects, dtype="O"),
...     tz="US/Pacific"
... )
0    1970-01-01 00:00:00-08:00
1    1970-01-01 00:00:00-08:00
2    1969-12-31 16:00:00-08:00
dtype: object
>>> datetime_to_pydatetime(
...     pd.Series(datetime_objects, dtype="O"),
...     tz="US/Pacific",
...     utc=True
... )
0    1969-12-31 16:00:00-08:00
1    1969-12-31 16:00:00-08:00
2    1969-12-31 16:00:00-08:00
dtype: object
>>> datetime_to_pydatetime(
...     np.array(datetime_objects),
...     tz="US/Pacific"
... )
array([datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
    dtype=object)
>>> datetime_to_pydatetime(
...     np.array(datetime_objects),
...     tz="US/Pacific",
...     utc=True
... )
array([datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
    dtype=object)
>>> mixed = [
...     pd.Timestamp("1970-01-01 00:00:00"),
...     pd.Timestamp("1970-01-01 00:00:00", tz="Asia/Hong_Kong"),
...     pd.Timestamp("1970-01-01 00:00:00", tz="America/Sao_Paulo"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00+01:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00-05:00"),
...     np.datetime64(0, "ns"),
...     np.datetime64(0, "Y")
... ]
>>> datetime_to_pydatetime(pd.Series(mixed, dtype="O"))
0    1970-01-01 00:00:00
1    1969-12-31 16:00:00
2    1970-01-01 03:00:00
3    1970-01-01 00:00:00
4    1969-12-31 23:00:00
5    1970-01-01 05:00:00
6    1970-01-01 00:00:00
7    1970-01-01 00:00:00
dtype: object
>>> datetime_to_pydatetime(
...     pd.Series(mixed, dtype="O"),
...     tz="US/Pacific"
... )
0    1970-01-01 00:00:00-08:00
1    1969-12-31 08:00:00-08:00
2    1969-12-31 19:00:00-08:00
3    1970-01-01 00:00:00-08:00
4    1969-12-31 15:00:00-08:00
5    1969-12-31 21:00:00-08:00
6    1969-12-31 16:00:00-08:00
7    1969-12-31 16:00:00-08:00
dtype: object
>>> datetime_to_pydatetime(
...     pd.Series(mixed, dtype="O"),
...     tz="US/Pacific",
...     utc=True
... )
0    1969-12-31 16:00:00-08:00
1    1969-12-31 08:00:00-08:00
2    1969-12-31 19:00:00-08:00
3    1969-12-31 16:00:00-08:00
4    1969-12-31 15:00:00-08:00
5    1969-12-31 21:00:00-08:00
6    1969-12-31 16:00:00-08:00
7    1969-12-31 16:00:00-08:00
dtype: object

Converting arbitrary datetimes into `datetime.datetime` objects:

>>> import pytz
>>> datetime_to_pydatetime(pd.Timestamp("1970-01-01 00:00:00"))
datetime.datetime(1970, 1, 1, 0, 0)
>>> datetime_to_pydatetime(
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00")
... )
datetime.datetime(1970, 1, 1, 0, 0)
>>> datetime_to_pydatetime(np.datetime64("1970-01-01 00:00:00"))
datetime.datetime(1970, 1, 1, 0, 0)
>>> datetime_to_pydatetime(
...     pd.Timestamp("1970-01-01 00:00:00", tz="US/Pacific")
... )
datetime.datetime(1970, 1, 1, 8, 0)
>>> datetime_to_pydatetime(
...     pytz.timezone("US/Pacific").localize(
...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
...     )
... )
datetime.datetime(1970, 1, 1, 8, 0)
>>> datetime_to_pydatetime(
...     pd.Timestamp("1970-01-01 00:00:00"),
...     tz="US/Pacific"
... )
datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     tz="US/Pacific"
... )
datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     pd.Timestamp("1970-01-01 00:00:00"),
...     tz="US/Pacific",
...     utc=True
... )
datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     tz="US/Pacific",
...     utc=True
... )
datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     np.datetime64("1970-01-01 00:00:00"),
...     tz="US/Pacific"
... )
datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     pd.Timestamp("1970-01-01 00:00:00", tz="Europe/Berlin"),
...     tz="US/Pacific"
... )
datetime.datetime(1969, 12, 31, 15, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_to_pydatetime(
...     pytz.timezone("Europe/Berlin").localize(
...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
...     ),
...     tz="US/Pacific"
... )
datetime.datetime(1969, 12, 31, 15, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
>>> datetime_objects = [
...     pd.Timestamp("1970-01-01 00:00:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     np.datetime64("1970-01-01 00:00:00")
... ]
>>> datetime_objects
[Timestamp('1970-01-01 00:00:00'), datetime.datetime(1970, 1, 1, 0, 0), numpy.datetime64('1970-01-01T00:00:00')]
>>> datetime_to_pydatetime(
...     pd.Series(datetime_objects, dtype="O"),
...     tz="US/Pacific"
... )
0    1970-01-01 00:00:00-08:00
1    1970-01-01 00:00:00-08:00
2    1969-12-31 16:00:00-08:00
dtype: object
>>> datetime_to_pydatetime(
...     pd.Series(datetime_objects, dtype="O"),
...     tz="US/Pacific",
...     utc=True
... )
0    1969-12-31 16:00:00-08:00
1    1969-12-31 16:00:00-08:00
2    1969-12-31 16:00:00-08:00
dtype: object
>>> datetime_to_pydatetime(
...     np.array(datetime_objects),
...     tz="US/Pacific"
... )
array([datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
    dtype=object)
>>> datetime_to_pydatetime(
...     np.array(datetime_objects),
...     tz="US/Pacific",
...     utc=True
... )
array([datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
    dtype=object)
>>> mixed = [
...     pd.Timestamp("1970-01-01 00:00:00"),
...     pd.Timestamp("1970-01-01 00:00:00", tz="Asia/Hong_Kong"),
...     pd.Timestamp("1970-01-01 00:00:00", tz="America/Sao_Paulo"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00+01:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00-05:00"),
...     np.datetime64(0, "ns"),
...     np.datetime64(0, "Y")
... ]
>>> datetime_to_pydatetime(pd.Series(mixed, dtype="O"))
0    1970-01-01 00:00:00
1    1969-12-31 16:00:00
2    1970-01-01 03:00:00
3    1970-01-01 00:00:00
4    1969-12-31 23:00:00
5    1970-01-01 05:00:00
6    1970-01-01 00:00:00
7    1970-01-01 00:00:00
dtype: object
>>> datetime_to_pydatetime(
...     pd.Series(mixed, dtype="O"),
...     tz="US/Pacific"
... )
0    1970-01-01 00:00:00-08:00
1    1969-12-31 08:00:00-08:00
2    1969-12-31 19:00:00-08:00
3    1970-01-01 00:00:00-08:00
4    1969-12-31 15:00:00-08:00
5    1969-12-31 21:00:00-08:00
6    1969-12-31 16:00:00-08:00
7    1969-12-31 16:00:00-08:00
dtype: object
>>> datetime_to_pydatetime(
...     pd.Series(mixed, dtype="O"),
...     tz="US/Pacific",
...     utc=True
... )
0    1969-12-31 16:00:00-08:00
1    1969-12-31 08:00:00-08:00
2    1969-12-31 19:00:00-08:00
3    1969-12-31 16:00:00-08:00
4    1969-12-31 15:00:00-08:00
5    1969-12-31 21:00:00-08:00
6    1969-12-31 16:00:00-08:00
7    1969-12-31 16:00:00-08:00
dtype: object

Converting arbitrary datetimes into `numpy.datetime64` objects:

>>> import pytz
>>> datetime_to_numpy_datetime64(pd.Timestamp("1970-01-01 00:00:00"))
numpy.datetime64('1970-01-01T00:00:00.000000000')
>>> datetime_to_numpy_datetime64(
...    datetime.datetime.fromisoformat("1970-01-01 00:00:00")
... )
numpy.datetime64('1970-01-01T00:00:00.000000000')
>>> datetime_to_numpy_datetime64(np.datetime64("1970-01-01 00:00:00"))
numpy.datetime64('1970-01-01T00:00:00.000000000')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00", tz="US/Pacific")
... )
numpy.datetime64('1970-01-01T08:00:00.000000000')
>>> datetime_to_numpy_datetime64(
...     pytz.timezone("US/Pacific").localize(
...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
...     )
... )
numpy.datetime64('1970-01-01T08:00:00.000000000')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
...     unit="ns"
... )
numpy.datetime64('1970-01-01T00:00:00.123456789')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
...     unit="us"
... )
numpy.datetime64('1970-01-01T00:00:00.123456')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
...     unit="ms"
... )
numpy.datetime64('1970-01-01T00:00:00.123')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
...     unit="s"
... )
numpy.datetime64('1970-01-01T00:00:00')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
...     unit="m"
... )
numpy.datetime64('1970-01-01T00:00')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
...     unit="h"
... )
numpy.datetime64('1970-01-01T00','h')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
...     unit="D"
... )
numpy.datetime64('1970-01-01')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
...     unit="W"
... )
numpy.datetime64('1970-01-01')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
...     unit="M"
... )
numpy.datetime64('1970-01')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
...     unit="Y"
... )
numpy.datetime64('1970')
>>> datetime_to_numpy_datetime64(
...     pd.Timestamp("1970-01-01 00:00:00.123456789")
...     unit = "s",
...     rounding="up"    
... )
numpy.datetime64('1970-01-01T00:00:01')
>>> datetime_objects = [
...     pd.Timestamp("1970-01-01 00:00:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     np.datetime64("1970-01-01 00:00:00")
... ]
>>> datetime_objects
[Timestamp('1970-01-01 00:00:00'), datetime.datetime(1970, 1, 1, 0, 0), numpy.datetime64('1970-01-01T00:00:00')]
>>> datetime_to_numpy_datetime64(pd.Series(datetime_objects, dtype="O"))
0    1970-01-01T00:00:00.000000000
1    1970-01-01T00:00:00.000000000
2    1970-01-01T00:00:00.000000000
dtype: object
>>> datetime_to_numpy_datetime64(np.array(datetime_objects))
array(['1970-01-01T00:00:00.000000000', '1970-01-01T00:00:00.000000000',
    '1970-01-01T00:00:00.000000000'], dtype='datetime64[ns]')
>>> datetime_to_numpy_datetime64(np.array(datetime_objects), unit="m")
array(['1970-01-01T00:00', '1970-01-01T00:00', '1970-01-01T00:00'],
    dtype='datetime64[m]')
>>> mixed = [
...     pd.Timestamp("1970-01-01 00:00:00"),
...     pd.Timestamp("1970-01-01 00:00:00", tz="Asia/Hong_Kong"),
...     pd.Timestamp("1970-01-01 00:00:00", tz="America/Sao_Paulo"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00+01:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00-05:00"),
...     np.datetime64(0, "ns"),
...     np.datetime64(0, "Y")
... ]
>>> datetime_to_numpy_datetime64(pd.Series(mixed, dtype="O"))
0    1970-01-01T00:00:00.000000000
1    1969-12-31T16:00:00.000000000
2    1970-01-01T03:00:00.000000000
3    1970-01-01T00:00:00.000000000
4    1969-12-31T23:00:00.000000000
5    1970-01-01T05:00:00.000000000
6    1970-01-01T00:00:00.000000000
7    1970-01-01T00:00:00.000000000
dtype: object
>>> datetime_to_numpy_datetime64(np.array(mixed))
array(['1970-01-01T00:00:00.000000000', '1969-12-31T16:00:00.000000000',
    '1970-01-01T03:00:00.000000000', '1970-01-01T00:00:00.000000000',
    '1969-12-31T23:00:00.000000000', '1970-01-01T05:00:00.000000000',
    '1970-01-01T00:00:00.000000000', '1970-01-01T00:00:00.000000000'],
    dtype='datetime64[ns]')

Converting arbitrary datetimes into the highest available resolution:

>>> import pytz
>>> datetime_to_datetime(pd.Timestamp("1970-01-01 00:00:00"))
Timestamp('1970-01-01 00:00:00')
>>> datetime_to_datetime(
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00")
... )
Timestamp('1970-01-01 00:00:00')
>>> datetime_to_datetime(np.datetime64("1970-01-01 00:00:00"))
Timestamp('1970-01-01 00:00:00')
>>> datetime_to_datetime(
...     pd.Timestamp("1970-01-01 00:00:00", tz="US/Pacific")
... )
Timestamp('1970-01-01 08:00:00')
>>> datetime_to_datetime(
...     pytz.timezone("US/Pacific").localize(
...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
...     )
... )
Timestamp('1970-01-01 08:00:00')
>>> datetime_to_datetime(
...     pd.Timestamp("1970-01-01 00:00:00"),
...     tz="US/Pacific"
... )
Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific')
>>> datetime_to_datetime(
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     tz="US/Pacific"
... )
Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific')
>>> datetime_to_datetime(
...     pd.Timestamp("1970-01-01 00:00:00"),
...     tz="US/Pacific",
...     utc=True
... )
Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
>>> datetime_to_datetime(
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     tz="US/Pacific",
...     utc=True
... )
Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
>>> datetime_to_datetime(
...     np.datetime64("1970-01-01 00:00:00"),
...     tz="US/Pacific"
... )
Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
>>> datetime_to_datetime(
...     pd.Timestamp("1970-01-01 00:00:00", tz="Europe/Berlin"),
...     tz="US/Pacific"
... )
Timestamp('1969-12-31 15:00:00-0800', tz='US/Pacific')
>>> datetime_to_datetime(
...     pytz.timezone("Europe/Berlin").localize(
...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
...     ),
...     tz="US/Pacific"
... )
Timestamp('1969-12-31 15:00:00-0800', tz='US/Pacific')
>>> datetime_objects = [
...     pd.Timestamp("1970-01-01 00:00:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     np.datetime64("1970-01-01 00:00:00")
... ]
>>> datetime_objects
[Timestamp('1970-01-01 00:00:00'), datetime.datetime(1970, 1, 1, 0, 0), numpy.datetime64('1970-01-01T00:00:00')]
>>> datetime_to_datetime(
...     pd.Series(datetime_objects, dtype="O"),
...     tz="US/Pacific"
... )
0   1970-01-01 00:00:00-08:00
1   1970-01-01 00:00:00-08:00
2   1969-12-31 16:00:00-08:00
dtype: datetime64[ns, US/Pacific]
>>> datetime_to_datetime(
...     pd.Series(datetime_objects, dtype="O"),
...     tz="US/Pacific",
...     utc=True
... )
0   1969-12-31 16:00:00-08:00
1   1969-12-31 16:00:00-08:00
2   1969-12-31 16:00:00-08:00
dtype: datetime64[ns, US/Pacific]
>>> datetime_to_datetime(
...     np.array(datetime_objects),
...     tz="US/Pacific"
... )
array([Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific'),
    Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific'),
    Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')],
    dtype=object)
>>> datetime_to_datetime(np.array(datetime_objects))
array(['1970-01-01T00:00:00.000000000', '1970-01-01T00:00:00.000000000',
    '1970-01-01T00:00:00.000000000'], dtype='datetime64[ns]')
>>> mixed = [
...     pd.Timestamp("1970-01-01 00:00:00"),
...     pd.Timestamp("1970-01-01 00:00:00", tz="Asia/Hong_Kong"),
...     pd.Timestamp("1970-01-01 00:00:00", tz="America/Sao_Paulo"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00+01:00"),
...     datetime.datetime.fromisoformat("1970-01-01 00:00:00-05:00"),
...     np.datetime64(0, "ns"),
...     np.datetime64(0, "Y")
... ]
>>> datetime_to_datetime(pd.Series(mixed, dtype="O"))
0   1970-01-01 00:00:00
1   1969-12-31 16:00:00
2   1970-01-01 03:00:00
3   1970-01-01 00:00:00
4   1969-12-31 23:00:00
5   1970-01-01 05:00:00
6   1970-01-01 00:00:00
7   1970-01-01 00:00:00
dtype: datetime64[ns]
>>> datetime_to_datetime(np.array(mixed))
array(['1970-01-01T00:00:00.000000000', '1969-12-31T16:00:00.000000000',
    '1970-01-01T03:00:00.000000000', '1970-01-01T00:00:00.000000000',
    '1969-12-31T23:00:00.000000000', '1970-01-01T05:00:00.000000000',
    '1970-01-01T00:00:00.000000000', '1970-01-01T00:00:00.000000000'],
    dtype='datetime64[ns]')
>>> datetime_to_datetime(
...     pd.Series(mixed, dtype="O"),
...     tz="US/Pacific"
... )
0   1970-01-01 00:00:00-08:00
1   1969-12-31 08:00:00-08:00
2   1969-12-31 19:00:00-08:00
3   1970-01-01 00:00:00-08:00
4   1969-12-31 15:00:00-08:00
5   1969-12-31 21:00:00-08:00
6   1969-12-31 16:00:00-08:00
7   1969-12-31 16:00:00-08:00
dtype: datetime64[ns, US/Pacific]
>>> datetime_to_datetime(
...     pd.Series(mixed, dtype="O"),
...     tz="US/Pacific",
...     utc=True
... )
0   1969-12-31 16:00:00-08:00
1   1969-12-31 08:00:00-08:00
2   1969-12-31 19:00:00-08:00
3   1969-12-31 16:00:00-08:00
4   1969-12-31 15:00:00-08:00
5   1969-12-31 21:00:00-08:00
6   1969-12-31 16:00:00-08:00
7   1969-12-31 16:00:00-08:00
dtype: datetime64[ns, US/Pacific]
"""
import datetime
from cpython cimport datetime

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import check_dtype
from pdtypes.util.type_hints import datetime_like

from ..timezone import (
    is_utc, localize_pandas_timestamp, localize_pydatetime, timezone
)

from .from_ns import (
    ns_to_pandas_timestamp, ns_to_pydatetime, ns_to_numpy_datetime64,
    ns_to_datetime
)
from .to_ns import datetime_to_ns


#######################
####    Private    ####
#######################


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[char, cast=True] is_aware_vector(
    np.ndarray[object] arr
):
    """Return a boolean mask indicating which elements of `arr` are
    timezone-aware.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object dt
    cdef np.ndarray[char, cast=True] result = np.full(arr_length, False)

    for i in range(arr_length):
        dt = arr[i]
        result[i] = isinstance(dt, np.datetime64) or dt.tzinfo is not None

    return result


#######################
####    Helpers    ####
#######################


def _is_aware(
    arg: datetime_like | np.ndarray | pd.Series
) -> bool | np.ndarray:
    """Helper to identify timezone-aware inputs from arbitrary data."""
    # np.ndarray
    if isinstance(arg, np.ndarray):
        # M8 dtype
        if np.issubdtype(arg.dtype, "M8"):
            return True

        # object dtype
        return is_aware_vector(arg)

    # pd.Series
    if isinstance(arg, pd.Series):
        # M8[ns] dtype
        if pd.api.types.is_datetime64_ns_dtype(arg):
            return arg.dt.tz is not None

        # object dtype
        return is_aware_vector(arg.to_numpy())

    # scalar
    return isinstance(arg, np.datetime64) or arg.tzinfo is not None


######################
####    Public    ####
######################


def datetime_to_pandas_timestamp(
    arg: datetime_like | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    utc: bool = False
) -> pd.Timestamp | np.ndarray | pd.Series:
    """Convert arbitrary datetime objects into `pandas.Timestamp`
    representation.

    Parameters
    ----------
    arg : datetime-like | array-like
        A datetime object or vector of datetime objects to convert.
    tz : str | datetime.tzinfo, default None
        The timezone to localize results to.  This can be `None`, indicating a
        naive return type, an instance of `datetime.tzinfo` or one of its
        derivatives (from `pytz`, `zoneinfo`, etc.), or an IANA timezone
        database string ('US/Eastern', 'UTC', etc.).  The special value
        `'local'` is also accepted, referencing the system's local time zone.
    utc : bool, default False
        Controls the localization behavior of timezone-naive datetime inputs.
        If this is set to `True`, naive datetimes will be interpreted as UTC
        times, and will be *converted* from UTC to the specified `tz`.  If this
        is `False` (the default), naive datetime strings will be *localized*
        directly to `tz` instead.

    Returns
    -------
    pd.Timestamp | array-like
        The homogenous `pandas.Timestamp` equivalent of `arg`, localized to
        `tz`.

    Raises
    ------
    OverflowError
        If one or more datetime objects in `arg` exceed the available range of
        `pandas.Timestamp` objects ([`'1677-09-21 00:12:43.145224193'` -
        `'2262-04-11 23:47:16.854775807'`]).

    Notes
    -----
    This function can be used to repair improperly-formatted datetime
    sequences, such as those with mixed datetime representations, including
    mixed timezone information and/or mixed aware/naive status.  In this case,
    it will convert each input element into a standardized `pandas.Timestamp`
    format, localized (or converted) to the given timezone.

    If the input is a pandas series, this function will always return an
    `'M8[ns]'` dtype output series, with the `.dt` namespace enabled and the
    appropriate timezone information attached.

    Examples
    --------
    Datetimes can be timezone-naive:

    >>> datetime_to_pandas_timestamp(pd.Timestamp("1970-01-01 00:00:00"))
    Timestamp('1970-01-01 00:00:00')
    >>> datetime_to_pandas_timestamp(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00")
    ... )
    Timestamp('1970-01-01 00:00:00')
    >>> datetime_to_pandas_timestamp(np.datetime64("1970-01-01 00:00:00"))
    Timestamp('1970-01-01 00:00:00')

    Or aware:

    >>> import pytz
    >>> datetime_to_pandas_timestamp(
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="US/Pacific")
    ... )
    Timestamp('1970-01-01 08:00:00')
    >>> datetime_to_pandas_timestamp(
    ...     pytz.timezone("US/Pacific").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
    ...     )
    ... )
    Timestamp('1970-01-01 08:00:00')

    They can be localized to any timezone:

    >>> datetime_to_pandas_timestamp(
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     tz="US/Pacific"
    ... )
    Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific')
    >>> datetime_to_pandas_timestamp(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     tz="US/Pacific"
    ... )
    Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific')
    >>> datetime_to_pandas_timestamp(
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
    >>> datetime_to_pandas_timestamp(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
    >>> datetime_to_pandas_timestamp(
    ...     np.datetime64("1970-01-01 00:00:00"),
    ...     tz="US/Pacific"
    ... )
    Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')

    Or converted to another timezone:

    >>> datetime_to_pandas_timestamp(
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="Europe/Berlin"),
    ...     tz="US/Pacific"
    ... )
    Timestamp('1969-12-31 15:00:00-0800', tz='US/Pacific')
    >>> datetime_to_pandas_timestamp(
    ...     pytz.timezone("Europe/Berlin").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
    ...     ),
    ...     tz="US/Pacific"
    ... )
    Timestamp('1969-12-31 15:00:00-0800', tz='US/Pacific')

    They can also be vectorized:

    >>> datetime_objects = [
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     np.datetime64("1970-01-01 00:00:00")
    ... ]
    >>> datetime_objects
    [Timestamp('1970-01-01 00:00:00'), datetime.datetime(1970, 1, 1, 0, 0), numpy.datetime64('1970-01-01T00:00:00')]
    >>> datetime_to_pandas_timestamp(
    ...     pd.Series(datetime_objects, dtype="O"),
    ...     tz="US/Pacific"
    ... )
    0   1970-01-01 00:00:00-08:00
    1   1970-01-01 00:00:00-08:00
    2   1969-12-31 16:00:00-08:00
    dtype: datetime64[ns, US/Pacific]
    >>> datetime_to_pandas_timestamp(
    ...     pd.Series(datetime_objects, dtype="O"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    0   1969-12-31 16:00:00-08:00
    1   1969-12-31 16:00:00-08:00
    2   1969-12-31 16:00:00-08:00
    dtype: datetime64[ns, US/Pacific]
    >>> datetime_to_pandas_timestamp(
    ...     np.array(datetime_objects),
    ...     tz="US/Pacific"
    ... )
    array([Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific'),
       Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific'),
       Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')],
      dtype=object)
    >>> datetime_to_pandas_timestamp(
    ...     np.array(datetime_objects),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    array([Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific'),
       Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific'),
       Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')],
      dtype=object)

    With potentially mixed aware/naive and/or mixed timezone input:

    >>> import pytz
    >>> mixed = [
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="Asia/Hong_Kong"),
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="America/Sao_Paulo"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00+01:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00-05:00"),
    ...     np.datetime64(0, "ns"),
    ...     np.datetime64(0, "Y")
    ... ]
    >>> datetime_to_pandas_timestamp(pd.Series(mixed, dtype="O"))
    0   1970-01-01 00:00:00
    1   1969-12-31 16:00:00
    2   1970-01-01 03:00:00
    3   1970-01-01 00:00:00
    4   1969-12-31 23:00:00
    5   1970-01-01 05:00:00
    6   1970-01-01 00:00:00
    7   1970-01-01 00:00:00
    dtype: datetime64[ns]
    >>> datetime_to_pandas_timestamp(
    ...     pd.Series(mixed, dtype="O"),
    ...     tz="US/Pacific"
    ... )
    0   1970-01-01 00:00:00-08:00
    1   1969-12-31 08:00:00-08:00
    2   1969-12-31 19:00:00-08:00
    3   1970-01-01 00:00:00-08:00
    4   1969-12-31 15:00:00-08:00
    5   1969-12-31 21:00:00-08:00
    6   1969-12-31 16:00:00-08:00
    7   1969-12-31 16:00:00-08:00
    dtype: datetime64[ns, US/Pacific]
    >>> datetime_to_pandas_timestamp(
    ...     pd.Series(mixed, dtype="O"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    0   1969-12-31 16:00:00-08:00
    1   1969-12-31 08:00:00-08:00
    2   1969-12-31 19:00:00-08:00
    3   1969-12-31 16:00:00-08:00
    4   1969-12-31 15:00:00-08:00
    5   1969-12-31 21:00:00-08:00
    6   1969-12-31 16:00:00-08:00
    7   1969-12-31 16:00:00-08:00
    dtype: datetime64[ns, US/Pacific]
    """
    # resolve timezone
    tz = timezone(tz)

    # trivial case: no conversion necessary
    if check_dtype(arg, pd.Timestamp):
        arg = localize_pandas_timestamp(arg, tz=tz, utc=utc)
        if isinstance(arg, pd.Series) and pd.api.types.is_object_dtype(arg):
            arg = arg.infer_objects()
        return arg

    # convert inputs into nanosecond offsets, then ns to `pandas.Timestamp`
    if utc or tz is None or is_utc(tz):  # interpret naive inputs as UTC
        return ns_to_pandas_timestamp(datetime_to_ns(arg), tz=tz)

    # localize naive inputs directly to `tz`
    has_offset = _is_aware(arg)
    arg = ns_to_pandas_timestamp(datetime_to_ns(arg), tz=None)
    return localize_pandas_timestamp(arg, tz=tz, utc=has_offset)


def datetime_to_pydatetime(
    arg: datetime_like | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    utc: bool = False
) -> datetime.datetime | np.ndarray | pd.Series:
    """Convert arbitrary datetime objects into `datetime.datetime`
    representation.

    Parameters
    ----------
    arg : datetime-like | array-like
        A datetime object or vector of datetime objects to convert.
    tz : str | datetime.tzinfo, default None
        The timezone to localize results to.  This can be `None`, indicating a
        naive return type, an instance of `datetime.tzinfo` or one of its
        derivatives (from `pytz`, `zoneinfo`, etc.), or an IANA timezone
        database string ('US/Eastern', 'UTC', etc.).  The special value
        `'local'` is also accepted, referencing the system's local time zone.
    utc : bool, default False
        Controls the localization behavior of timezone-naive datetime inputs.
        If this is set to `True`, naive datetimes will be interpreted as UTC
        times, and will be *converted* from UTC to the specified `tz`.  If this
        is `False` (the default), naive datetime strings will be *localized*
        directly to `tz` instead.

    Returns
    -------
    datetime.datetime | array-like
        The homogenous `datetime.datetime` equivalent of `arg`.

    Raises
    ------
    OverflowError
        If one or more datetime objects in `arg` exceed the available range of
        `datetime.datetime` objects ([`'0001-01-01 00:00:00'` -
        `'9999-12-31 23:59:59.999999'`]).

    Notes
    -----
    This function can be used to repair improperly-formatted datetime
    sequences, such as those with mixed datetime representations, including
    mixed timezone information and/or mixed aware/naive status.  In this case,
    it will convert each input element into a standardized `datetime.datetime`
    format, localized (or converted) to the given timezone.

    Examples
    --------
    Datetimes can be timezone-naive:

    >>> datetime_to_pydatetime(pd.Timestamp("1970-01-01 00:00:00"))
    datetime.datetime(1970, 1, 1, 0, 0)
    >>> datetime_to_pydatetime(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00")
    ... )
    datetime.datetime(1970, 1, 1, 0, 0)
    >>> datetime_to_pydatetime(np.datetime64("1970-01-01 00:00:00"))
    datetime.datetime(1970, 1, 1, 0, 0)

    Or aware:

    >>> import pytz
    >>> datetime_to_pydatetime(
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="US/Pacific")
    ... )
    datetime.datetime(1970, 1, 1, 8, 0)
    >>> datetime_to_pydatetime(
    ...     pytz.timezone("US/Pacific").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
    ...     )
    ... )
    datetime.datetime(1970, 1, 1, 8, 0)

    They can be localized to any timezone:

    >>> datetime_to_pydatetime(
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     tz="US/Pacific"
    ... )
    datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
    >>> datetime_to_pydatetime(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     tz="US/Pacific"
    ... )
    datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
    >>> datetime_to_pydatetime(
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
    >>> datetime_to_pydatetime(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
    >>> datetime_to_pydatetime(
    ...     np.datetime64("1970-01-01 00:00:00"),
    ...     tz="US/Pacific"
    ... )
    datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)

    Or converted to another timezone:

    >>> datetime_to_pydatetime(
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="Europe/Berlin"),
    ...     tz="US/Pacific"
    ... )
    datetime.datetime(1969, 12, 31, 15, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)
    >>> datetime_to_pydatetime(
    ...     pytz.timezone("Europe/Berlin").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
    ...     ),
    ...     tz="US/Pacific"
    ... )
    datetime.datetime(1969, 12, 31, 15, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)

    They can also be vectorized:

    >>> datetime_objects = [
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     np.datetime64("1970-01-01 00:00:00")
    ... ]
    >>> datetime_objects
    [Timestamp('1970-01-01 00:00:00'), datetime.datetime(1970, 1, 1, 0, 0), numpy.datetime64('1970-01-01T00:00:00')]
    >>> datetime_to_pydatetime(
    ...     pd.Series(datetime_objects, dtype="O"),
    ...     tz="US/Pacific"
    ... )
    0    1970-01-01 00:00:00-08:00
    1    1970-01-01 00:00:00-08:00
    2    1969-12-31 16:00:00-08:00
    dtype: object
    >>> datetime_to_pydatetime(
    ...     pd.Series(datetime_objects, dtype="O"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    0    1969-12-31 16:00:00-08:00
    1    1969-12-31 16:00:00-08:00
    2    1969-12-31 16:00:00-08:00
    dtype: object
    >>> datetime_to_pydatetime(
    ...     np.array(datetime_objects),
    ...     tz="US/Pacific"
    ... )
    array([datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1970, 1, 1, 0, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
      dtype=object)
    >>> datetime_to_pydatetime(
    ...     np.array(datetime_objects),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    array([datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>),
       datetime.datetime(1969, 12, 31, 16, 0, tzinfo=<DstTzInfo 'US/Pacific' PST-1 day, 16:00:00 STD>)],
      dtype=object)

    With potentially mixed aware/naive and/or mixed timezone input:

    >>> import pytz
    >>> mixed = [
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="Asia/Hong_Kong"),
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="America/Sao_Paulo"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00+01:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00-05:00"),
    ...     np.datetime64(0, "ns"),
    ...     np.datetime64(0, "Y")
    ... ]
    >>> datetime_to_pydatetime(pd.Series(mixed, dtype="O"))
    0    1970-01-01 00:00:00
    1    1969-12-31 16:00:00
    2    1970-01-01 03:00:00
    3    1970-01-01 00:00:00
    4    1969-12-31 23:00:00
    5    1970-01-01 05:00:00
    6    1970-01-01 00:00:00
    7    1970-01-01 00:00:00
    dtype: object
    >>> datetime_to_pydatetime(
    ...     pd.Series(mixed, dtype="O"),
    ...     tz="US/Pacific"
    ... )
    0    1970-01-01 00:00:00-08:00
    1    1969-12-31 08:00:00-08:00
    2    1969-12-31 19:00:00-08:00
    3    1970-01-01 00:00:00-08:00
    4    1969-12-31 15:00:00-08:00
    5    1969-12-31 21:00:00-08:00
    6    1969-12-31 16:00:00-08:00
    7    1969-12-31 16:00:00-08:00
    dtype: object
    >>> datetime_to_pydatetime(
    ...     pd.Series(mixed, dtype="O"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    0    1969-12-31 16:00:00-08:00
    1    1969-12-31 08:00:00-08:00
    2    1969-12-31 19:00:00-08:00
    3    1969-12-31 16:00:00-08:00
    4    1969-12-31 15:00:00-08:00
    5    1969-12-31 21:00:00-08:00
    6    1969-12-31 16:00:00-08:00
    7    1969-12-31 16:00:00-08:00
    dtype: object
    """
    # resolve timezone
    tz = timezone(tz)

    # trivial case: no conversion necessary
    if check_dtype(arg, datetime.datetime):
        return localize_pydatetime(arg, tz=tz, utc=utc)

    # convert inputs into nanosecond offsets, then ns to `datetime.datetime`
    if utc or tz is None or is_utc(tz):  # interpret naive inputs as UTC
        return ns_to_pydatetime(datetime_to_ns(arg), tz=tz)

    # localize naive inputs directly to `tz`
    has_offset = _is_aware(arg)
    arg = ns_to_pydatetime(datetime_to_ns(arg), tz=None)
    return localize_pydatetime(arg, tz=tz, utc=has_offset)


def datetime_to_numpy_datetime64(
    arg: datetime_like | np.ndarray | pd.Series,
    unit: str = None,
    rounding: str = "down"
) -> np.datetime64 | np.ndarray | pd.Series:
    """Convert arbitrary datetime objects into `datetime.datetime`
    representation.

    Parameters
    ----------
    arg : datetime-like | array-like
        A datetime object or vector of datetime objects to convert.
    unit : {'ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y'}, default None
        The unit to use for the returned datetime64 objects.  If `None`, this
        will attempt to automatically find the highest resolution unit that
        can fully represent all of the given datetimes.  This unit promotion is
        overflow-safe.
    rounding : {'floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
    'half_down', 'half_up', 'half_even'}, default 'down'
        The rounding rule to use when one or more datetimes contain precision
        below `unit`.  This applies equally in the case of unit promotion with
        respect to the final chosen unit.

    Returns
    -------
    np.datetime64 | array-like
        The homogenous `numpy.datetime64` equivalent of `arg`.

    Raises
    ------
    OverflowError
        If one or more datetime objects in `arg` exceed the available range of
        `numpy.datetime64` objects with the given `unit` (up to
        [`'-9223372036854773837-01-01 00:00:00'` -
        `'9223372036854775807-01-01 00:00:00'`]).

    Notes
    -----
    This function can be used to repair improperly-formatted datetime
    sequences, such as those with mixed datetime representations, including
    mixed timezone information and/or mixed aware/naive status.  In this case,
    it will convert each input element into a standardized `numpy.datetime64`
    format, with the given unit and rounding rules.

    If the input is a numpy array, this function will always return an `'M8'`
    dtype output array, with a unit chosen to fit the data, from `'M8[ns]'` to
    `'M8[Y]'`.

    Examples
    --------
    Datetimes can be naive:

    >>> datetime_to_numpy_datetime64(pd.Timestamp("1970-01-01 00:00:00"))
    numpy.datetime64('1970-01-01T00:00:00.000000000')
    >>> datetime_to_numpy_datetime64(
    ...    datetime.datetime.fromisoformat("1970-01-01 00:00:00")
    ... )
    numpy.datetime64('1970-01-01T00:00:00.000000000')
    >>> datetime_to_numpy_datetime64(np.datetime64("1970-01-01 00:00:00"))
    numpy.datetime64('1970-01-01T00:00:00.000000000')

    Or aware:

    >>> import pytz
    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="US/Pacific")
    ... )
    numpy.datetime64('1970-01-01T08:00:00.000000000')
    >>> datetime_to_numpy_datetime64(
    ...     pytz.timezone("US/Pacific").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
    ...     )
    ... )
    numpy.datetime64('1970-01-01T08:00:00.000000000')

    The units of the returned `numpy.datetime64` objects can be customized:

    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     unit="ns"
    ... )
    numpy.datetime64('1970-01-01T00:00:00.123456789')
    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     unit="us"
    ... )
    numpy.datetime64('1970-01-01T00:00:00.123456')
    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     unit="ms"
    ... )
    numpy.datetime64('1970-01-01T00:00:00.123')
    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     unit="s"
    ... )
    numpy.datetime64('1970-01-01T00:00:00')
    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     unit="m"
    ... )
    numpy.datetime64('1970-01-01T00:00')
    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     unit="h"
    ... )
    numpy.datetime64('1970-01-01T00','h')
    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     unit="D"
    ... )
    numpy.datetime64('1970-01-01')
    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     unit="W"
    ... )
    numpy.datetime64('1970-01-01')
    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     unit="M"
    ... )
    numpy.datetime64('1970-01')
    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789"),
    ...     unit="Y"
    ... )
    numpy.datetime64('1970')

    With customizable rounding:

    >>> datetime_to_numpy_datetime64(
    ...     pd.Timestamp("1970-01-01 00:00:00.123456789")
    ...     unit = "s",
    ...     rounding="up"    
    ... )
    numpy.datetime64('1970-01-01T00:00:01')

    Datetimes can also be vectorized:

    >>> datetime_objects = [
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     np.datetime64("1970-01-01 00:00:00")
    ... ]
    >>> datetime_objects
    [Timestamp('1970-01-01 00:00:00'), datetime.datetime(1970, 1, 1, 0, 0), numpy.datetime64('1970-01-01T00:00:00')]
    >>> datetime_to_numpy_datetime64(pd.Series(datetime_objects, dtype="O"))
    0    1970-01-01T00:00:00.000000000
    1    1970-01-01T00:00:00.000000000
    2    1970-01-01T00:00:00.000000000
    dtype: object
    >>> datetime_to_numpy_datetime64(np.array(datetime_objects))
    array(['1970-01-01T00:00:00.000000000', '1970-01-01T00:00:00.000000000',
       '1970-01-01T00:00:00.000000000'], dtype='datetime64[ns]')
    >>> datetime_to_numpy_datetime64(np.array(datetime_objects), unit="m")
    array(['1970-01-01T00:00', '1970-01-01T00:00', '1970-01-01T00:00'],
      dtype='datetime64[m]')

    With potentially mixed aware/naive and/or mixed timezone input:

    >>> import pytz
    >>> mixed = [
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="Asia/Hong_Kong"),
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="America/Sao_Paulo"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00+01:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00-05:00"),
    ...     np.datetime64(0, "ns"),
    ...     np.datetime64(0, "Y")
    ... ]
    >>> datetime_to_numpy_datetime64(pd.Series(mixed, dtype="O"))
    0    1970-01-01T00:00:00.000000000
    1    1969-12-31T16:00:00.000000000
    2    1970-01-01T03:00:00.000000000
    3    1970-01-01T00:00:00.000000000
    4    1969-12-31T23:00:00.000000000
    5    1970-01-01T05:00:00.000000000
    6    1970-01-01T00:00:00.000000000
    7    1970-01-01T00:00:00.000000000
    dtype: object
    >>> datetime_to_numpy_datetime64(np.array(mixed))
    array(['1970-01-01T00:00:00.000000000', '1969-12-31T16:00:00.000000000',
       '1970-01-01T03:00:00.000000000', '1970-01-01T00:00:00.000000000',
       '1969-12-31T23:00:00.000000000', '1970-01-01T05:00:00.000000000',
       '1970-01-01T00:00:00.000000000', '1970-01-01T00:00:00.000000000'],
      dtype='datetime64[ns]')
    """
    # trivial case: no conversion necessary
    if isinstance(arg, np.ndarray) and np.issubdtype(arg.dtype, "M8"):
        arg_unit, step_size = np.datetime_data(arg.dtype)
        if step_size == 1:
            if ((unit and arg_unit == unit) or
                (unit is None and arg_unit == "ns")):
                return arg

    # convert inputs into nanosecond offsets, then ns to `numpy.datetime64`
    return ns_to_numpy_datetime64(
        datetime_to_ns(arg),
        unit=unit,
        rounding=rounding
    )


def datetime_to_datetime(
    arg: datetime_like | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = None,
    utc: bool = False
) -> datetime_like | np.ndarray | pd.Series:
    """Convert arbitrary datetime objects into their highest resolution
    representation, localized to the given timezone.

    Parameters
    ----------
    arg : datetime-like | array-like
        A datetime object or vector of datetime objects to convert.

    Returns
    -------
    datetime.datetime | array-like
        The datetime equivalent of `arg` with the highest possible resolution
        homogenous element types.

    Raises
    ------
    OverflowError
        If one or more datetime objects in `arg` exceed the available range of
        `datetime.datetime` objects ([`'0001-01-01 00:00:00'` -
        `'9999-12-31 23:59:59.999999'`]).
    RuntimeError
        If `tz` is specified and is not utc, and the range of `arg` exceeds
        `datetime.datetime` range ([`'0001-01-01 00:00:00'` -
        `'9999-12-31 23:59:59.999999'`]).

    Notes
    -----
    This function can be used to repair improperly-formatted datetime
    sequences, such as those with mixed datetime representations, including
    mixed timezone information and/or mixed aware/naive status.  In this case,
    it will convert each input element into a standardized format, preferring
    higher resolution, timezone-aware datetime representations where possible.
    In general, the hierarchy is as follows:
        #. `pandas.Timestamp` if the input falls within
            [`'1677-09-21 00:12:43.145224193'` -
            `'2262-04-11 23:47:16.854775807'`]
        #. `datetime.datetime` if the input falls within
            [`'0001-01-01 00:00:00'` - `'9999-12-31 23:59:59.999999'`].
        #. `numpy.datetime64` if `tz` is `None` or UTC.

    However, if the input is a numpy array and `tz` is `None` or UTC, then
    special preference is given to `numpy.datetime64` output types.  In this
    case, the output array will always have an `'M8'` dtype, with a unit chosen
    to fit the data, from `'M8[ns]'` to `'M8[Y]'`.

    Examples
    --------
    Datetimes can be timezone-naive:

    >>> datetime_to_datetime(pd.Timestamp("1970-01-01 00:00:00"))
    Timestamp('1970-01-01 00:00:00')
    >>> datetime_to_datetime(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00")
    ... )
    Timestamp('1970-01-01 00:00:00')
    >>> datetime_to_datetime(np.datetime64("1970-01-01 00:00:00"))
    Timestamp('1970-01-01 00:00:00')

    Or aware:

    >>> import pytz
    >>> datetime_to_datetime(
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="US/Pacific")
    ... )
    Timestamp('1970-01-01 08:00:00')
    >>> datetime_to_datetime(
    ...     pytz.timezone("US/Pacific").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
    ...     )
    ... )
    Timestamp('1970-01-01 08:00:00')

    They can be localized to any timezone:

    >>> datetime_to_datetime(
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     tz="US/Pacific"
    ... )
    Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific')
    >>> datetime_to_datetime(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     tz="US/Pacific"
    ... )
    Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific')
    >>> datetime_to_datetime(
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
    >>> datetime_to_datetime(
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')
    >>> datetime_to_datetime(
    ...     np.datetime64("1970-01-01 00:00:00"),
    ...     tz="US/Pacific"
    ... )
    Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')

    Or converted to another timezone:

    >>> datetime_to_datetime(
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="Europe/Berlin"),
    ...     tz="US/Pacific"
    ... )
    Timestamp('1969-12-31 15:00:00-0800', tz='US/Pacific')
    >>> datetime_to_datetime(
    ...     pytz.timezone("Europe/Berlin").localize(
    ...         datetime.datetime.fromisoformat("1970-01-01 00:00:00")
    ...     ),
    ...     tz="US/Pacific"
    ... )
    Timestamp('1969-12-31 15:00:00-0800', tz='US/Pacific')

    They can also be vectorized:

    >>> datetime_objects = [
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     np.datetime64("1970-01-01 00:00:00")
    ... ]
    >>> datetime_objects
    [Timestamp('1970-01-01 00:00:00'), datetime.datetime(1970, 1, 1, 0, 0), numpy.datetime64('1970-01-01T00:00:00')]
    >>> datetime_to_datetime(
    ...     pd.Series(datetime_objects, dtype="O"),
    ...     tz="US/Pacific"
    ... )
    0   1970-01-01 00:00:00-08:00
    1   1970-01-01 00:00:00-08:00
    2   1969-12-31 16:00:00-08:00
    dtype: datetime64[ns, US/Pacific]
    >>> datetime_to_datetime(
    ...     pd.Series(datetime_objects, dtype="O"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    0   1969-12-31 16:00:00-08:00
    1   1969-12-31 16:00:00-08:00
    2   1969-12-31 16:00:00-08:00
    dtype: datetime64[ns, US/Pacific]
    >>> datetime_to_datetime(
    ...     np.array(datetime_objects),
    ...     tz="US/Pacific"
    ... )
    array([Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific'),
       Timestamp('1970-01-01 00:00:00-0800', tz='US/Pacific'),
       Timestamp('1969-12-31 16:00:00-0800', tz='US/Pacific')],
      dtype=object)
    >>> datetime_to_datetime(np.array(datetime_objects))
    array(['1970-01-01T00:00:00.000000000', '1970-01-01T00:00:00.000000000',
       '1970-01-01T00:00:00.000000000'], dtype='datetime64[ns]')

    With potentially mixed aware/naive and/or mixed timezone input:

    >>> import pytz
    >>> mixed = [
    ...     pd.Timestamp("1970-01-01 00:00:00"),
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="Asia/Hong_Kong"),
    ...     pd.Timestamp("1970-01-01 00:00:00", tz="America/Sao_Paulo"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00+01:00"),
    ...     datetime.datetime.fromisoformat("1970-01-01 00:00:00-05:00"),
    ...     np.datetime64(0, "ns"),
    ...     np.datetime64(0, "Y")
    ... ]
    >>> datetime_to_datetime(pd.Series(mixed, dtype="O"))
    0   1970-01-01 00:00:00
    1   1969-12-31 16:00:00
    2   1970-01-01 03:00:00
    3   1970-01-01 00:00:00
    4   1969-12-31 23:00:00
    5   1970-01-01 05:00:00
    6   1970-01-01 00:00:00
    7   1970-01-01 00:00:00
    dtype: datetime64[ns]
    >>> datetime_to_datetime(np.array(mixed))
    array(['1970-01-01T00:00:00.000000000', '1969-12-31T16:00:00.000000000',
       '1970-01-01T03:00:00.000000000', '1970-01-01T00:00:00.000000000',
       '1969-12-31T23:00:00.000000000', '1970-01-01T05:00:00.000000000',
       '1970-01-01T00:00:00.000000000', '1970-01-01T00:00:00.000000000'],
      dtype='datetime64[ns]')
    >>> datetime_to_datetime(
    ...     pd.Series(mixed, dtype="O"),
    ...     tz="US/Pacific"
    ... )
    0   1970-01-01 00:00:00-08:00
    1   1969-12-31 08:00:00-08:00
    2   1969-12-31 19:00:00-08:00
    3   1970-01-01 00:00:00-08:00
    4   1969-12-31 15:00:00-08:00
    5   1969-12-31 21:00:00-08:00
    6   1969-12-31 16:00:00-08:00
    7   1969-12-31 16:00:00-08:00
    dtype: datetime64[ns, US/Pacific]
    >>> datetime_to_datetime(
    ...     pd.Series(mixed, dtype="O"),
    ...     tz="US/Pacific",
    ...     utc=True
    ... )
    0   1969-12-31 16:00:00-08:00
    1   1969-12-31 08:00:00-08:00
    2   1969-12-31 19:00:00-08:00
    3   1969-12-31 16:00:00-08:00
    4   1969-12-31 15:00:00-08:00
    5   1969-12-31 21:00:00-08:00
    6   1969-12-31 16:00:00-08:00
    7   1969-12-31 16:00:00-08:00
    dtype: datetime64[ns, US/Pacific]
    """
    # resolve timezone
    tz = timezone(tz)

    # if `arg` is a numpy array and `tz` is utc, skip straight to np.datetime64
    if isinstance(arg, np.ndarray) and (tz is None or is_utc(tz)):
        return datetime_to_numpy_datetime64(arg)

    # pd.Timestamp
    try:
        return datetime_to_pandas_timestamp(arg, tz=tz, utc=utc)
    except OverflowError:
        pass

    # datetime.datetime
    try:
        return datetime_to_pydatetime(arg, tz=tz, utc=utc)
    except OverflowError:
        pass

    # np.datetime64
    if tz and not is_utc(tz):
        err_msg = ("`numpy.datetime64` objects do not carry timezone "
                   "information (must be utc)")
        raise RuntimeError(err_msg)
    return datetime_to_numpy_datetime64(arg)
