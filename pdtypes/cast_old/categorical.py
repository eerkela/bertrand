from __future__ import annotations
import datetime
import decimal
from functools import partial

import numpy as np
import pandas as pd
import pytz

from pdtypes.error import error_trace
import pdtypes.cast_old.boolean
import pdtypes.cast_old.integer
import pdtypes.cast_old.float
import pdtypes.cast_old.complex
import pdtypes.cast_old.decimal
import pdtypes.cast_old.datetime
import pdtypes.cast_old.timedelta
import pdtypes.cast_old.object
import pdtypes.cast_old.string


def to_boolean(series: pd.Series, dtype:type = bool) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "categorical":
        err_msg = (f"[{error_trace()}] `series` must contain categorical data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    categorical_type = pd.api.types.infer_dtype(series.dtype.categories)
    if categorical_type == "mixed-integer-float":
        float_type = series.apply(lambda x: np.dtype(type(x))).max()
        series = series.astype(float_type)
    if categorical_type in ("bytes", "mixed", "mixed-integer", "empty"):
        categorical_type = "object"
    conversions = {
        "string": partial(pdtypes.cast_old.string.to_boolean)  # TODO
    }
    category_values = series.dtype.categories.values
    category_type = series.dtype.categories.values.dtype