"""Utilities for converting to/from various datetime representations.

There are 3 possible datetime representations recognized by this package:
`pandas.Timestamp`, `datetime.datetime`, and `numpy.datetime64`.  Each one
covers a different range of possible dates, and may come with or without
timezone information.  From narrowest to widest, the ranges are:
    #. `pandas.Timestamp` - [`'1677-09-21 00:12:43.145224193'` -
        `'2262-04-11 23:47:16.854775807'`]
    #. `datetime.datetime` - [`'0001-01-01 00:00:00'` -
        `'9999-12-31 23:59:59.999999'`].
    #. `numpy.datetime64` - up to [`'-9223372036854773837-01-01 00:00:00'` -
        `'9223372036854775807-01-01 00:00:00'`] depending on choice of unit.

    Note: `numpy.datetime64` objects do not carry timezone information, and
    have reduced resolution as their underlying unit increases in size.
    Maximum range is achieved with unit='Y'.  If unit='ns', the range is the
    same as `pandas.Timestamp`.

A particular representation can be either chosen beforehand using a specialized
conversion function, or determined dynamically using the generic equivalent.
In the generic case, the datetime representation is chosen to fit the observed
data, with higher resolution representations being preferred over lower
resolution alternatives.  If the input is vectorized (either `numpy.ndarray` or
`pandas.Series`), then native representations (`numpy.datetime64` or
`pandas.Timestamp`, respectively) are also prioritized as follows:
    #. `pandas.Timestamp` (pandas.Series, scalar) or `numpy.datetime64` with
        unit='ns' (numpy.ndarray).  If a timezone is given, arrays are coerced
        to `pandas.Timestamp`.
    #. `datetime.datetime` (pandas.Series, scalar) or `numpy.datetime64` with
        unit='us' (numpy.ndarray).  If a timezone is given, arrays are coerced
        to `datetime.datetime`.
    #. `numpy.datetime64` with unit 'ms'.
    #. `numpy.datetime64` with unit 's'.
    #. `numpy.datetime64` with unit 'm'.
    #. `numpy.datetime64` with unit 'h'.
    #. `numpy.datetime64` with unit 'D'.
    #. `numpy.datetime64` with unit 'M'.
    #. `numpy.datetime64` with unit 'Y'.

Overflow between each of these categories is handled gracefully, leading to
a promotion in either the datetime representation (`datetime.datetime` ->
`numpy.datetime64`) or the underlying unit ('M8[ms]' -> 'M8[s]').

    Note: when promoting datetime types, it is possible for data to be lost if
    it would underflow past the limits of the returned unit.

Modules
-------
    from_ns
        Convert integer nanosecond offsets (from the utc epoch) to their
        corresponding datetime representation.

    from_string
        Convert datetime strings to their corresponding datetime
        representation.

    to_datetime
        Convert datetime objects to a different datetime representation.

    to_ns
        Convert datetime objects to nanosecond offsets from the UTC epoch.

Functions
---------
    datetime_to_datetime(
        arg: datetime_like | np.ndarray | pd.Series
    ) -> datetime_like | np.ndarray | pd.Series:
        Convert datetime objects to their highest resolution representation.

    datetime_to_ns(
        arg: datetime_like | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Convert arbitrary datetime objects into ns offsets from UTC.

    datetime_to_numpy_datetime64(
        arg: datetime_like | np.ndarray | pd.Series,
        unit: str = None,
        rounding: str = "down"
    ) -> np.datetime64 | np.ndarray | pd.Series:
        Convert datetime objects to `numpy.datetime64`.

    datetime_to_pandas_timestamp(
        arg: datetime_like | np.ndarray | pd.Series
    ) -> pd.Timestamp | np.ndarray | pd.Series:
        Convert datetime objects to `pandas.Timestamp`.

    datetime_to_pydatetime(
        arg: datetime_like | np.ndarray | pd.Series
    ) -> datetime.datetime | np.ndarray | pd.Series:
        Convert datetime objects to `datetime.datetime`.

    is_iso_8601(string: str) -> bool:
        Infer whether a scalar string can be interpreted as ISO 8601-compliant.

    iso_8601_to_ns(
        arg: str | np.ndarray | pd.Series,
        errors: str = "raise"
    ) -> tuple[int | np.ndarray | pd.Series, bool]:
        Convert ISO 8601 strings into nanosecond offsets from the utc epoch.

    ns_to_datetime(
        arg: int | np.ndarray | pd.Series,
        tz: str | datetime.tzinfo | None = None
    ) -> datetime_like | np.ndarray | pd.Series:
        Convert nanosecond offsets into dynamic datetime objects.

    ns_to_numpy_datetime64(
        arg: int | np.ndarray | pd.Series,
        unit: str = None,
        rounding: str = "down",
        *,
        min_ns: int = None,
        max_ns: int = None
    ) -> np.datetime64 | np.ndarray | pd.Series:
        Convert nanosecond offsets into `numpy.datetime64` objects.

    ns_to_pandas_timestamp(
        arg: int | np.ndarray | pd.Series,
        tz: str | datetime.tzinfo | None = None,
        *,
        min_ns: int = None,
        max_ns: int = None
    ) -> pd.Timestamp | np.ndarray | pd.Series:
        Convert nanosecond offsets into `pandas.Timestamp` objects.

    ns_to_pydatetime(
        arg: int | np.ndarray | pd.Series,
        tz: str | datetime.tzinfo | None = None,
        min_ns: int = None,
        max_ns: int = None
    ) -> datetime.datetime | np.ndarray | pd.Series:
        Convert nanosecond offsets into `datetime.datetime` objects.

    numpy_datetime64_to_ns(
        arg: np.datetime64 | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Convert `numpy.datetime64` objects into ns offsets from UTC.

    pandas_timestamp_to_ns(
        arg: pd.Timestamp | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Convert `pandas.Timestamp` objects into ns offsets from UTC.

    pydatetime_to_ns(
        arg: datetime.datetime | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Convert `datetime.datetime` objects into ns offsets from UTC.

    string_to_datetime(
        arg: str | np.ndarray | pd.Series,
        tz: str | datetime.tzinfo = None,
        format: str = None,
        day_first: bool = False,
        year_first: bool = False,
        errors: str = "raise"
    ) -> datetime_like | np.ndarray | pd.Series:
        Convert datetime strings into dynamic datetime objects.

    string_to_numpy_datetime64(
        arg: str | np.ndarray | pd.Series,
        unit: str = None,
        rounding: str = "down",
        errors: str = "raise"
    ) -> np.datetime64 | np.ndarray | pd.Series:
        Convert datetime strings to `numpy.datetime64` objects.

    string_to_pandas_timestamp(
        arg: str | np.ndarray | pd.Series,
        tz: str | datetime.tzinfo = None,
        format: str = None,
        day_first: bool = False,
        year_first: bool = False,
        errors: str = "raise"
    ) -> pd.Timestamp | np.ndarray | pd.Series:
        Convert datetime strings to `pandas.Timestamp` objects.

    string_to_pydatetime(
        arg: str | np.ndarray | pd.Series,
        tz: str | datetime.tzinfo = None,
        format: str = None,
        day_first: bool = False,
        year_first: bool = False,
        errors: str = "raise"
    ) -> datetime.datetime | np.ndarray | pd.Series:
        Convert datetime strings to `datetime.datetime` objects.
"""
from .from_ns import (
    ns_to_pandas_timestamp, ns_to_pydatetime, ns_to_numpy_datetime64,
    ns_to_datetime
)
from .from_string import (
    is_iso_8601, iso_8601_to_ns, string_to_pandas_timestamp,
    string_to_pydatetime, string_to_numpy_datetime64, string_to_datetime
)
from .to_datetime import (
    datetime_to_pandas_timestamp, datetime_to_pydatetime,
    datetime_to_numpy_datetime64, datetime_to_datetime
)
from .to_ns import (
    pandas_timestamp_to_ns, pydatetime_to_ns, numpy_datetime64_to_ns,
    datetime_to_ns
)
