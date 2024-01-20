# pylint: disable=redefined-outer-name, unused-argument
from __future__ import annotations
import datetime
from functools import partial

from pdcast.decorators.attachable import attachable
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func
from pdcast.detect import detect_type
from pdcast.util import time
from pdcast.util.vector import apply_with_errors


@attachable
@extension_func
@dispatch("series")
def tz_convert(
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


tz_convert.argument(time.tz)


#######################
####    PRIVATE    ####
#######################


@tz_convert.overload("datetime[pandas]")
def localize_pandas_timestamp(
    series: pd.Series,
    tz: datetime.tzinfo | None,
    **unused
) -> pd.Series:
    """TODO"""
    series = series.astype(detect_type(series).dtype, copy=False)
    original = getattr(series.dt.tz_convert, "original", series.dt.tz_convert)
    return original(tz, **unused)


@tz_convert.overload("datetime[python]")
def localize_python_datetime(
    series: pd.Series,
    tz: datetime.tzinfo | None,
    **unused
) -> pd.Series:
    """TODO"""
    # emulate pandas tz_convert limitation
    if not detect_type(series).tz:
        raise TypeError(
            "Cannot convert tz-naive Timestamp, use tz_localize to localize"
        )

    series_type = detect_type(series)
    call = partial(time.localize_pydatetime_scalar, tz=tz)
    series = apply_with_errors(series, call, errors="raise")
    return series.astype(series_type.replace(tz=tz).dtype)
