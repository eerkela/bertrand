import datetime
from typing import Any, Union

import numpy as np
import pandas as pd


# array-like
array_like = Union[list, np.ndarray, pd.Series]

# dtype-like
atomic_type = Union[type, pd.api.extensions.ExtensionDtype]
dtype_like = Union[type, str, np.dtype, pd.api.extensions.ExtensionDtype]
scalar = Any

# time-like
datetime_like = Union[pd.Timestamp, datetime.datetime, np.datetime64]
timedelta_like = Union[pd.Timedelta, datetime.timedelta, np.timedelta64]
