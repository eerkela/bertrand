import datetime
import zoneinfo

import dateutil
import numpy as np
import pandas as pd
import pytz


category_types = {
    "datetime": pd.Timestamp,
    "datetime[pandas]": pd.Timestamp,
    "datetime[python]": datetime.datetime,
    "datetime[numpy]": np.datetime64,
}



def interpret_iso_8601_string(datetime_string, expected_dtype, timezone=None):
    # pd.Timestamp
    if expected_dtype is pd.Timestamp:
        return pd.Timestamp(datetime_string, tz=timezone)

    # datetime.datetime
    if expected_dtype is datetime.datetime:
        result = datetime.datetime.fromisoformat(datetime_string[:26])
        if timezone is None:
            return result
        if isinstance(timezone, pytz.BaseTzInfo):
            return timezone.localize(result)
        if isinstance(timezone, zoneinfo.ZoneInfo):
            return result.replace(tzinfo=timezone)
        if isinstance(timezone, dateutil.tz.tzfile):
            return result.replace(tzinfo=timezone)
        return pytz.timezone(timezone).localize(result)

    # np.datetime64
    if expected_dtype is np.datetime64:
        result = np.datetime64(datetime_string)
        if timezone is None:
            return result
        raise RuntimeError(
            f"numpy.datetime64 objects do not carry timezone information"
        )

    raise RuntimeError(
        f"could not interpret expected dtype: {expected_dtype}"
    )