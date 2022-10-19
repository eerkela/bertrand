import datetime
from typing import Any, Union

import numpy as np
import pandas as pd


# TODO: dtype_like -> type_specifier


# array-like
array_like = Union[tuple, list, np.ndarray, pd.Series]

# dtype-like
dtype_like = Union[type, str, np.dtype, pd.api.extensions.ExtensionDtype]
scalar = Any

# time-like
date_like = Union[pd.Timestamp, datetime.datetime, datetime.date, np.datetime64]
datetime_like = Union[pd.Timestamp, datetime.datetime, np.datetime64]
timedelta_like = Union[pd.Timedelta, datetime.timedelta, np.timedelta64]
