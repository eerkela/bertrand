"""Utilities for converting to, from, and between various datetime and
timedelta representations.

This library encapsulates all the required functionality to convert each of
the default types that are recognized by `pdtypes` into datetime or timedelta
format, or to do the reverse.

Subpackages
--------
    datetime
        Utilities for converting to/from various datetime representations.

    timedelta
        Utilities for converting to/from various timedelta representations.

Modules
-------
    calendar
        Gregorian calendar utility functions.

    epoch
        Customizable epochs for datetime/timedelta calculations.

    timezone
        Timezone interface for datetime localizations.

    unit
        Datetime and timedelta unit conversions.
"""
from .calendar import (
    date_to_days, days_in_month, days_to_date, is_leap_year, leaps_between
)
from .datetime import (
    iso_8601_to_ns, pandas_timestamp_to_ns, pydatetime_to_ns,
    numpy_datetime64_to_ns, ns_to_pydatetime, string_to_pydatetime
)
from .epoch import Epoch
from .timedelta import (
    numpy_timedelta64_to_ns, pandas_timedelta_to_ns, pytimedelta_to_ns,
    timedelta_string_to_ns
)
from .unit import convert_unit
from .unit import as_ns_public as as_ns
from .unit import valid_units_public as valid_units


# # modules
# from .calendar import (
#     date_to_days, days_in_month, days_to_date, decompose_date, is_leap_year,
#     leaps_between
# )
# from .epoch import epoch, epoch_date, epoch_ns
# from .timezone import is_utc, localize, timezone
# from .unit import convert_unit_float, convert_unit_integer
# from .unit import as_ns_public as as_ns
# from .unit import valid_units_public as valid_units


# # datetime
# from .datetime import (
#     # ns to datetime
#     ns_to_pandas_timestamp,
#     ns_to_pydatetime,
#     ns_to_numpy_datetime64,
#     ns_to_datetime,

#     # datetime to ns
#     pandas_timestamp_to_ns,
#     pydatetime_to_ns,
#     numpy_datetime64_to_ns,
#     datetime_to_ns,

#     # string to datetime
#     is_iso_8601,
#     iso_8601_to_ns,
#     string_to_pandas_timestamp,
#     string_to_pydatetime,
#     string_to_numpy_datetime64,
#     string_to_datetime,

#     # datetime to datetime
#     datetime_to_pandas_timestamp,
#     datetime_to_pydatetime,
#     datetime_to_numpy_datetime64,
#     datetime_to_datetime
# )

# # timedelta
# from .timedelta import (
#     # ns to timedelta
#     ns_to_pandas_timedelta,
#     ns_to_pytimedelta,
#     ns_to_numpy_timedelta64,
#     ns_to_timedelta,

#     # timedelta to ns
#     pandas_timedelta_to_ns,
#     pytimedelta_to_ns,
#     numpy_timedelta64_to_ns,
#     timedelta_to_ns,

#     # string to timedelta
#     string_to_pandas_timedelta,
#     string_to_pytimedelta,
#     string_to_numpy_timedelta64,
#     string_to_timedelta,

#     # timedelta to timedelta
#     timedelta_to_pandas_timedelta,
#     timedelta_to_pytimedelta,
#     timedelta_to_numpy_timedelta64,
#     timedelta_to_timedelta
# )
