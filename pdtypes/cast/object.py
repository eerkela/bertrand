from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz

from pdtypes.error import error_trace


def to_boolean(series: pd.Series, dtype: type = bool) -> pd.Series:
    object_dtypes = ("bytes", "mixed", "mixed-integer", "empty")
    if pd.api.types.infer_dtype(series) not in object_dtypes:
        err_msg = (f"[{error_trace()}] `series` must contain raw object data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    if series.hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)
