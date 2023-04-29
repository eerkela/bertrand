from __future__ import annotations
import datetime
from functools import partial

import pandas as pd
import pytz

from pdcast.decorators.attachable import attachable
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func
from pdcast.decorators.wrapper import SeriesWrapper


# TODO: move tz_convert, tz_localize into patch/dt


######################
####    PUBLIC    ####
######################


@extension_func
@dispatch
def localize(
    series: SeriesWrapper,
    tz: str | pytz.BaseTzInfo | None,
    naive_tz: str | pytz.BaseTzInfo | None = None,
) -> SeriesWrapper:
    """
    """
    raise NotImplementedError(
        f"{series.element_type} objects do not carry timezone information"
    )


@attachable
@extension_func
def tz_localize(
    series: SeriesWrapper,
    tz: str | pytz.BaseTzInfo | None,
    **unused
) -> SeriesWrapper:
    """TODO"""
    # emulate pandas tz_localize limitation
    if series.element_type.tz:
        raise TypeError("Already tz-aware, use tz_convert to convert.")

    return localize(series, tz=tz, naive_tz=None)


@attachable
@extension_func
def tz_convert(
    series: SeriesWrapper,
    tz: str | pytz.BaseTzInfo | None,
    **unused
) -> SeriesWrapper:
    """TODO"""
    # emulate pandas tz_convert limitation
    if not series.element_type.tz:
        raise TypeError(
            "Cannot convert tz-naive Timestamp, use tz_localize to localize"
        )

    return localize(series, tz=tz, naive_tz=None)


######################
####    PANDAS    ####
######################


# @localize.overload("datetime[pandas]")
def localize_pandas_timestamp(
    series: SeriesWrapper,
    tz: pytz.BaseTzInfo | None,
    naive_tz: pytz.BaseTzInfo | None
) -> SeriesWrapper:
    """TODO"""
    series_type = series.element_type
    series = series.rectify()

    # delegate to original pandas tz_localize, tz_convert
    loc = series.dt.tz_localize
    conv = series.dt.tz_convert
    if isinstance(loc, attachable.VirtualAttribute):
        loc = loc.original
    if isinstance(conv, attachable.VirtualAttribute):
        conv = conv.original

    # series is naive
    if series_type.tz is None:
        if naive_tz is None:
            if tz is None:  # do nothing
                result = series.series.copy()
            else:  # localize to tz
                result = loc(series.series, tz)
        else:
            result = conv(loc(series.series, naive_tz), tz)

    # series is aware
    else:
        result = conv(series.series, tz)

    return SeriesWrapper(
        result,
        hasnans=series.hasnans,
        element_type=series_type.replace(tz=tz)
    )


def localize_pandas_timestamp_scalar(
    dt: pd.Timestamp,
    tz: pytz.BaseTzInfo | None,
    naive_tz: pytz.BaseTzInfo | None
) -> datetime.datetime:
    """Localize a scalar datetime.datetime object to the given tz."""
    # datetime is naive - apply naive_tz
    if not dt.tzinfo:
        if naive_tz is None:
            if tz is None:
                return dt  # do nothing
            return dt.tz_localize(tz)  # localize directly to final tz

        dt = dt.tz_localize(naive_tz)  # localize to naive_tz

    # datetime is aware
    return dt.tz_convert(tz)


# ######################
# ####    PYTHON    ####
# ######################


# @localize.overload("datetime[python]")
def localize_pydatetime(
    series: SeriesWrapper,
    tz: pytz.BaseTzInfo | None,
    naive_tz: pytz.BaseTzInfo | None,
) -> SeriesWrapper:
    """TODO"""
    # trivial case
    if all(x is None for x in (series.element_type.tz, naive_tz, tz)):
        return series.copy()

    return series.apply_with_errors(
        partial(
            localize_pydatetime_scalar,
            tz=tz,
            naive_tz=naive_tz
        ),
        errors="raise",
        element_type=series.element_type.replace(tz=tz)
    )


def localize_pydatetime_scalar(
    dt: datetime.datetime,
    tz: pytz.BaseTzInfo | None,
    naive_tz: pytz.BaseTzInfo | None
) -> datetime.datetime:
    """Localize a scalar datetime.datetime object to the given tz."""
    # datetime is naive - apply naive_tz
    if not dt.tzinfo:
        if naive_tz is None:
            if tz is None:
                return dt  # do nothing
            return tz.localize(dt)  # localize directly to final tz

        dt = naive_tz.localize(dt)  # localize to naive_tz

    # datetime is aware
    if tz is None:  # convert to utc, then strip tzinfo
        return dt.astimezone(utc).replace(tzinfo=None)
    return dt.astimezone(tz)  # convert to final tz


#######################
####    PRIVATE    ####
#######################


utc = datetime.timezone.utc
