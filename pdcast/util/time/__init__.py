"""This package provides utilities for converting to, from, and between various
datetime and timedelta representations.

Modules
-------
calendar
    Vectorized math for conversions involving the proleptic Gregorian calendar.

datetime
    Utilities for converting to and from various datetime representations.

epoch
    Customizable epochs for datetime/timedelta calculations.

timedelta
    Utilities for converting to and from various timedelta representations.

unit
    Datetime and timedelta unit conversions.
"""
from .calendar import (
    date_to_days, days_in_month, days_to_date, is_leap_year, leaps_between
)
from .datetime import (
    filter_dateutil_parser_error, is_iso_8601_format_string, iso_8601_to_ns,
    is_utc, localize_pydatetime_scalar, numpy_datetime64_to_ns,
    ns_to_pydatetime, pandas_timestamp_to_ns, pydatetime_to_ns,
    string_to_pydatetime
)
from .epoch import Epoch
from .epoch import epoch_aliases_public as epoch_aliases
from .timedelta import (
    numpy_timedelta64_to_ns, pandas_timedelta_to_ns, pytimedelta_to_ns,
    timedelta_string_to_ns
)
from .timezone import tz
from .unit import convert_unit, round_months_to_ns, round_years_to_ns
from .unit import as_ns_public as as_ns
from .unit import valid_units_public as valid_units
