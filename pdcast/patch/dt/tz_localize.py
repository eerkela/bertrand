# pylint: disable=redefined-outer-name, unused-argument
from __future__ import annotations
import datetime
from functools import partial

import pandas as pd

from pdcast.decorators.attachable import attachable
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func
from pdcast.detect import detect_type
from pdcast.util import time
from pdcast.util.vector import apply_with_errors


@attachable
@extension_func
@dispatch("series")
def tz_localize(
    series: pd.Series,
    tz: str | datetime.tzinfo | None,
    **unused
) -> pd.Series:
    """TODO"""
    raise NotImplementedError(
        f"{detect_type(series)} objects do not carry timezone information"
    )


#########################
####    ARGUMENTS    ####
#########################


tz_localize.argument(time.tz)


#######################
####    PRIVATE    ####
#######################


@tz_localize.overload("datetime[pandas]")
def localize_pandas_timestamp(
    series: pd.Series,
    tz: datetime.tzinfo | None,
    **unused
) -> pd.Series:
    """TODO"""
    series = series.astype(detect_type(series).dtype, copy=False)
    original = getattr(series.dt.tz_localize, "original", series.dt.tz_localize)
    return original(tz, **unused)


@tz_localize.overload("datetime[python]")
def localize_python_datetime(
    series: pd.Series,
    tz: datetime.tzinfo | None,
    **unused
) -> pd.Series:
    """TODO"""
    # emulate pandas tz_localize limitation
    if detect_type(series).tz:
        raise TypeError("Already tz-aware, use tz_convert to convert.")

    series_type = detect_type(series)
    call = partial(time.localize_pydatetime_scalar, tz=tz)
    series = apply_with_errors(series, call, errors="raise")
    return series.astype(series_type.replace(tz=tz).dtype)
