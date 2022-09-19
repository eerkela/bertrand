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

A particular representation can be either chosen beforehand using one of
the specialized conversion functions or determined dynamically using the
generic equivalent.  In the generic case, the datetime representation is chosen
to fit the observed data, with higher resolution representations being
preferred over lower resolution alternatives.  If the input is vectorized
(either `numpy.ndarray` or `pandas.Series`), then native representations
(`numpy.datetime64` or `pandas.Timestamp`, respectively) are also prioritized,
as follows:
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



TODO: exported modules/functions
"""
from .from_ns import (
    ns_to_pandas_timestamp, ns_to_pydatetime, ns_to_numpy_datetime64,
    ns_to_datetime
)
from .from_string import (
    string_to_pandas_timestamp, string_to_pydatetime,
    string_to_numpy_datetime64, string_to_datetime
)
from .to_ns import datetime_to_ns
from .to_datetime import (
    datetime_to_pandas_timestamp, datetime_to_pydatetime,
    datetime_to_numpy_datetime64, datetime_to_datetime
)
