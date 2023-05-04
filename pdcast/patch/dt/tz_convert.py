# pylint: disable=redefined-outer-name, unused-argument
from __future__ import annotations
import datetime
from functools import partial

from pdcast.decorators.attachable import attachable, VirtualAttribute
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func
from pdcast.decorators.wrapper import SeriesWrapper
from pdcast.util import time


@attachable
@extension_func
@dispatch
def tz_convert(
    series: SeriesWrapper,
    tz: str | datetime.tzinfo | None,
    **unused
) -> SeriesWrapper:
    """TODO"""
    raise NotImplementedError(
        f"{series.element_type} objects do not carry timezone information"
    )


#########################
####    ARGUMENTS    ####
#########################


tz_convert.register_arg(time.tz)


#######################
####    PRIVATE    ####
#######################


@tz_convert.overload("datetime[pandas]")
def localize_pandas_timestamp(
    series: SeriesWrapper,
    tz: datetime.tzinfo | None,
    **unused
) -> SeriesWrapper:
    """TODO"""
    series = series.rectify()

    # delegate to original pandas tz_localize implementation
    orig = series.dt.tz_convert
    if isinstance(orig, VirtualAttribute):
        orig = orig.original

    return SeriesWrapper(
        orig(series.series, tz, **unused),
        hasnans=series.hasnans,
        element_type=series.element_type.replace(tz=tz)
    )


@tz_convert.overload("datetime[python]")
def localize_python_datetime(
    series: SeriesWrapper,
    tz: datetime.tzinfo | None,
    **unused
) -> SeriesWrapper:
    """TODO"""
    # emulate pandas tz_convert limitation
    if not series.element_type.tz:
        raise TypeError(
            "Cannot convert tz-naive Timestamp, use tz_localize to localize"
        )

    return series.apply_with_errors(
        partial(time.localize_pydatetime_scalar, tz=tz),
        errors="raise",
        element_type=series.element_type.replace(tz=tz)
    )
