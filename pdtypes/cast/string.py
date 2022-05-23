from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz

from pdtypes.error import error_trace


# def string_to_integer(series: pd.Series,
#                       force: bool = False,
#                       round: bool = False,
#                       tol: float = 1e-6,
#                       dayfirst: bool = False,
#                       ) -> pd.Series:
#     try:
#         return series.astype(pd.Int64Dtype())
#     except ValueError:
#         pass

#     try:
#         return float_to_integer(series.astype(np.float64),
#                                 force=force, round=round, tol=tol)
#     except ValueError:
#         pass

#     try:
#         return complex_to_integer(series.astype(np.complex128),
#                                   force=force, round=round, tol=tol)
#     except ValueError:
#         pass

#     try:
#         return boolean_to_integer(series.astype(pd.BooleanDtype()))
#     except ValueError:
#         pass

#     try:
#         return datetime_to_integer(pd.to_datetime(series, ))


def string_to_float(series: pd.Series) -> pd.Series:
    return series.astype(np.dtype(np.float64))


def string_to_complex(series: pd.Series) -> pd.Series:
    return series.astype(np.dtype(np.complex128))


def string_to_boolean(series: pd.Series) -> pd.Series:
    return series.astype(pd.BooleanDtype())


def string_to_datetime(series: pd.Series,
                       format: str | None = None,
                       exact: bool = True,
                       infer_datetime_format: bool = False,
                       dayfirst: bool = False,
                       yearfirst: bool = False,
                       cache: bool = True,
                       timezone: str | None = "local") -> pd.Series:
    return pd.to_datetime(series,
                          format=format,
                          exact=exact,
                          infer_datetime_format=infer_datetime_format,
                          dayfirst=dayfirst,
                          yearfirst=yearfirst,
                          cache=cache)


def string_to_timedelta(series: pd.Series,
                        unit: str = "s") -> pd.Series:
    return pd.to_timedelta(series, unit=unit)
