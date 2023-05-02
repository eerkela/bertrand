# pylint: disable=redefined-outer-name, unused-argument
from __future__ import annotations
import datetime
from functools import partial

import pandas as pd
import pytz
import tzlocal

from pdcast.decorators.attachable import VirtualAttribute
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func
from pdcast.decorators.wrapper import SeriesWrapper


# TODO: naive_tz should be eliminated and this file should be cythonized.


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


#########################
####    ARGUMENTS    ####
#########################


@localize.register_arg
@localize.register_arg(name="naive_tz")
def tz(tz: str | pytz.BaseTzInfo | None, state: dict) -> pytz.BaseTzInfo:
    """Convert a time zone specifier into a ``datetime.tzinfo`` object."""
    if tz is None:
        return None

    # trivial case
    if isinstance(tz, pytz.BaseTzInfo):
        return tz

    # local specifier
    if isinstance(tz, str) and tz.lower() == "local":
        return pytz.timezone(tzlocal.get_localzone_name())

    # IANA string
    return pytz.timezone(tz)


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
    if isinstance(loc, VirtualAttribute):
        loc = loc.original
    if isinstance(conv, VirtualAttribute):
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


# TODO: delete scalar conv and put localize_pandas_timestamp into patch/dt/


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


######################
####    PYTHON    ####
######################


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
