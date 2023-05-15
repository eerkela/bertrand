# pylint: disable=redefined-outer-name, unused-argument
from __future__ import annotations
import datetime
from functools import partial

from pdcast.decorators.attachable import attachable, VirtualAttribute
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func
from pdcast.decorators.wrapper import SeriesWrapper
from pdcast.detect import detect_type
from pdcast.util import time



@attachable
@extension_func
@dispatch("series")
def tz_localize(
    series: SeriesWrapper,
    tz: str | datetime.tzinfo | None,
    **unused
) -> SeriesWrapper:
    """TODO"""
    raise NotImplementedError(
        f"{detect_type(series)} objects do not carry timezone information"
    )


#########################
####    ARGUMENTS    ####
#########################


tz_localize.register_arg(time.tz)


#######################
####    PRIVATE    ####
#######################


@tz_localize.overload("datetime[pandas]")
def localize_pandas_timestamp(
    series: SeriesWrapper,
    tz: datetime.tzinfo | None,
    **unused
) -> SeriesWrapper:
    """TODO"""
    series = series.rectify()
    original = getattr(series.dt.tz_localize, "original", series.dt.tz_localize)
    return original(series.series, tz, **unused)


@tz_localize.overload("datetime[python]")
def localize_python_datetime(
    series: SeriesWrapper,
    tz: datetime.tzinfo | None,
    **unused
) -> SeriesWrapper:
    """TODO"""
    # emulate pandas tz_localize limitation
    if detect_type(series).tz:
        raise TypeError("Already tz-aware, use tz_convert to convert.")

    return series.apply_with_errors(
        partial(time.localize_pydatetime_scalar, tz=tz),
        errors="raise",
        element_type=detect_type(series).replace(tz=tz)
    )
