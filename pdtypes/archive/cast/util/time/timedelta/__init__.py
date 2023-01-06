"""Utilities for converting to/from various timedelta representations.

There are 3 possible timedelta representations recognized by this package:
`pandas.Timedelta`, `datetime.timedelta`, and `numpy.timedelta64`.  Each one
covers a different range of possible times.  From narrowest to widest, the
ranges are:
    #. `pandas.Timedelta` - [`'-106752 days +00:12:43.145224193'` -
        `'106751 days 23:47:16.854775807'`]
    #. `datetime.timedelta` - [`'-999999999 days, 0:00:00'` -
        `'999999999 days, 23:59:59.999999'`].
    #. `numpy.timedelta64` - up to [`'-9223372036854775807 years'` -
        `'9223372036854775807 years'`] depending on choice of unit.

    Note: `numpy.timedelta64` objects have reduced resolution as their
    underlying unit increases in size.  Maximum range is achieved with
    unit='Y'.  If unit='ns', the range is the same as `pandas.Timedelta`.

A particular representation can be either chosen beforehand using a specialized
conversion function, or determined dynamically using the generic equivalent.
In the generic case, the timedelta representation is chosen to fit the observed
data, with higher resolution representations being preferred over lower
resolution alternatives.  If the input is vectorized (either `numpy.ndarray` or
`pandas.Series`), then native representations (`numpy.timedelta64` or
`pandas.Timedelta`, respectively) are also prioritized as follows:
    #. `pandas.Timedelta` (pandas.Series, scalar) or `numpy.timedelta64` with
        unit='ns' (numpy.ndarray).
    #. `datetime.timedelta` (pandas.Series, scalar) or `numpy.timedelta64` with
        unit='us' (numpy.ndarray).
    #. `numpy.timedelta64` with unit 'ms'.
    #. `numpy.timedelta64` with unit 's'.
    #. `numpy.timedelta64` with unit 'm'.
    #. `numpy.timedelta64` with unit 'h'.
    #. `numpy.timedelta64` with unit 'D'.
    #. `numpy.timedelta64` with unit 'M'.
    #. `numpy.timedelta64` with unit 'Y'.

Overflow between each of these categories is handled gracefully, leading to
a promotion in either the timedelta representation (`datetime.timedelta` ->
`numpy.timedelta64`) or the underlying unit ('m8[ms]' -> 'm8[s]').

    Note: when promoting timedelta types, it is possible for data to be lost if
    it would underflow past the limits of the returned unit.

Modules
-------
    from_ns
        Convert integer nanosecond offsets from a given origin to their
        corresponding timedelta representation.

    from_string
        Convert timedelta strings to their corresponding timedelta
        representation.

    to_timedelta
        Convert timedelta objects to a different timedelta representation.

    to_ns
        Convert timedelta objects into nanosecond offsets from a given origin.

Functions
---------
    ns_to_pandas_timedelta(
        arg: int | np.ndarray | pd.Series,
        *,
        min_ns: int = None,
        max_ns: int = None
    ) -> pd.Timedelta | np.ndarray | pd.Series:
        Convert nanosecond offsets into `pandas.Timedelta` objects.

    ns_to_pytimedelta(
        arg: int | np.ndarray | pd.Series,
        *,
        min_ns: int = None,
        max_ns: int = None
    ) -> datetime.timedelta | np.ndarray | pd.Series:
        Convert nanosecond offsets into `datetime.timedelta` objects.

    ns_to_numpy_timedelta64(
        arg: int | np.ndarray | pd.Series,
        unit: str = None,
        since: str | datetime_like = "2001-01-01 00:00:00+0000",
        rounding: str = "down",
        *,
        min_ns: int = None,
        max_ns: int = None
    ) -> np.timedelta64 | np.ndarray | pd.Series:
        Convert nanosecond offsets into `numpy.timedelta64` objects with the
        given unit.

    ns_to_timedelta(
        arg: int | np.ndarray | pd.Series,
        since: str | datetime_like = "2001-01-01 00:00:00+0000",
        *,
        min_ns: int = None,
        max_ns: int = None
    ) -> timedelta_like | np.ndarray | pd.Series:
        Convert nanosecond offsets into arbitrary timedelta objects.

    numpy_timedelta64_to_ns(
        arg: np.timedelta64 | np.ndarray | pd.Series,
        since: str | datetime_like = "2001-01-01 00:00:00+0000"
    ) -> int | np.ndarray | pd.Series:
        Convert `numpy.timedelta64` objects into an equivalent number of
        nanoseconds.

    pandas_timedelta_to_ns(
        arg: pd.Timedelta | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Convert `pandas.Timedelta` objects into an equivalent number of
        nanoseconds.

    pytimedelta_to_ns(
        arg: datetime.timedelta | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Convert `datetime.timedelta` objects into an equivalent number of
        nanoseconds.

    string_to_numpy_timedelta64(
        arg: str | np.ndarray | pd.Series,
        as_hours: bool = False,
        unit: str = None,
        since: str | datetime_like = "2001-01-01 00:00:00+0000",
        rounding: str = "down",
        errors: str = "raise"
    ) -> np.timedelta64 | np.ndarray | pd.Series:
        Parse a timedelta string, returning it as a `numpy.timedelta64` object.

    string_to_pandas_timedelta(
        arg: str | np.ndarray | pd.Series,
        as_hours: bool = False,
        since: str | datetime_like = "2001-01-01 00:00:00+0000",
        errors: str = "raise"
    ) -> pd.Timedelta | np.ndarray | pd.Series:
        Parse a timedelta string, returning it as a `pandas.Timedelta` object.

    string_to_pytimedelta(
        arg: str | np.ndarray | pd.Series,
        as_hours: bool = False,
        since: str | datetime_like = "2001-01-01 00:00:00+0000",
        errors: str = "raise"
    ) -> datetime.timedelta | np.ndarray | pd.Series:
        Parse a timedelta string, returning it as a `datetime.timedelta` object.

    string_to_timedelta(
        arg: str | np.ndarray | pd.Series,
        as_hours: bool = False,
        since: str | datetime_like = "2001-01-01 00:00:00+0000",
        errors: str = "raise"
    ) -> timedelta_like | np.ndarray | pd.Series:
        Parse a timedelta string, returning it as an arbitrary timedelta object.

    timedelta_string_to_ns(
        arg: str | np.ndarray | pd.Series,
        as_hours: bool = False,
        since: str | datetime_like = "2001-01-01 00:00:00+0000",
        errors: str = "raise"
    ) -> tuple[int | np.ndarray | pd.Series, bool]:
        Parse a timedelta string, returning it as an integer number of
        nanoseconds.

    timedelta_to_ns(
        arg: timedelta_like | np.ndarray | pd.Series,
        since: str | datetime_like = "2001-01-01"
    ) -> int | np.ndarray | pd.Series:
        Convert arbitrary timedelta objects into an equivalent number of
        nanoseconds.

    timedelta_to_numpy_timedelta64(
        arg: timedelta_like | np.ndarray | pd.Series,
        since: str | datetime_like = "2001-01-01 00:00:00",
        unit: str = None,
        rounding: str = "down"
    ) -> np.timedelta64 | np.ndarray | pd.Series:
        Convert timedelta objects into `numpy.timedelta64` representation.

    timedelta_to_pandas_timedelta(
        arg: timedelta_like | np.ndarray | pd.Series,
        since: str | datetime_like = "2001-01-01 00:00:00"
    ) -> pd.Timedelta | np.ndarray | pd.Series:
        Convert timedelta objects into `pandas.Timedelta` representation.

    timedelta_to_pytimedelta(
        arg: timedelta_like | np.ndarray | pd.Series,
        since: str | datetime_like = "2001-01-01 00:00:00"
    ) -> datetime.timedelta | np.ndarray | pd.Series:
        Convert timedelta objects into `datetime.timedelta` representation.

    timedelta_to_timedelta(
        arg: timedelta_like | np.ndarray | pd.Series,
        since: str | datetime_like = "2001-01-01 00:00:00"
    ) -> timedelta_like | np.ndarray | pd.Series:
        Convert timedelta objects into the highest available resolution
        representation.
"""
from .from_ns import (
    ns_to_pandas_timedelta, ns_to_pytimedelta, ns_to_numpy_timedelta64,
    ns_to_timedelta
)
from .from_string import (
    string_to_pandas_timedelta, string_to_pytimedelta,
    string_to_numpy_timedelta64, string_to_timedelta, timedelta_string_to_ns
)
from .to_ns import (
    pandas_timedelta_to_ns, pytimedelta_to_ns, numpy_timedelta64_to_ns,
    timedelta_to_ns
)
from .to_timedelta import (
    timedelta_to_pandas_timedelta, timedelta_to_pytimedelta,
    timedelta_to_numpy_timedelta64, timedelta_to_timedelta
)
