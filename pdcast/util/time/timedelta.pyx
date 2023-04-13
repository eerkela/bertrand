"""This module contains utility functions for manipulating timedelta-like data
in a variety of formats.

Functions
---------
pandas_timedelta_to_ns()
    Convert ``pandas.Timedelta`` objects into an integer number of nanoseconds.

pytimedelta_to_ns()
    Convert python ``datetime.timedelta`` objects into an integer number of
    nanoseconds.

numpy_timedelta64_to_ns()
    Convert ``numpy.timedelta64`` objects into an integer number of nanoseconds
    from the given epoch.

timedelta_string_to_ns()
    Convert a vector of timedelta strings into an integer number of nanoseconds
    from the given epoch.
"""
import datetime
import decimal
import re

import numpy as np
cimport numpy as np
import pandas as pd

cimport pdcast.util.time.epoch as epoch
from pdcast.util.type_hints import datetime_like

from .unit cimport as_ns
from .unit import convert_unit, round_months_to_ns, round_years_to_ns


################################
####    PANDAS TIMEDELTA    ####
################################


def pandas_timedelta_to_ns(object delta) -> int:
    """Convert a pandas Timedelta into an integer number of nanoseconds."""
    return delta.value


###############################
####    PYTHON DATETIME    ####
###############################


def pytimedelta_to_ns(object delta) -> int:
    """Convert a python timedelta into an integer number of nanoseconds."""
    return (
        delta.days * as_ns["D"] +
        delta.seconds * as_ns["s"] +
        delta.microseconds * as_ns["us"]
    )


################################
####    NUMPY DATETIME64    ####
################################


def numpy_timedelta64_to_ns(
    object delta,
    epoch.Epoch since,
    str unit = None,
    long int step_size = -1
) -> int:
    """Convert a numpy timedelta64 into an integer number of nanoseconds."""
    if unit is None or step_size < 0:
        unit, step_size = np.datetime_data(delta)
    result = int(delta.view(np.int64)) * step_size
    return convert_unit(result, unit, "ns", since=since)


######################
####    STRING    ####
######################


cdef dict timedelta_formats_regex():
    """Compile a list of regular expressions to capture and parse recognized
    timedelta strings.

    Matches both abbreviated ('1h22m', '1 hour, 22 minutes', etc.) and clock
    format ('01:22:00', '1:22', '00:01:22:00') strings, with precision up to
    years and months and down to nanoseconds.
    """
    # abbreviated capture groups.  NOTE: group names match unit.as_ns.
    cdef str Y = r"(?P<Y>[\d.]+)(?:ys?|yrs?.?|years?)"
    cdef str M = r"(?P<M>[\d.]+)(?:mos?.?|mths?.?|months?)"
    cdef str W = r"(?P<W>[\d.]+)(?:w|wks?|weeks?)"
    cdef str D = r"(?P<D>[\d.]+)(?:d|dys?|days?)"
    cdef str h = r"(?P<h>[\d.]+)(?:h|hrs?|hours?)"
    cdef str m = r"(?P<m>[\d.]+)(?:m|(mins?)|(minutes?))"
    cdef str s = r"(?P<s>[\d.]+)(?:s|secs?|seconds?)"
    cdef str ms = r"(?P<ms>[\d.]+)(?:ms|msecs?|millisecs?|milliseconds?)"
    cdef str us = r"(?P<us>[\d.]+)(?:us|usecs?|microsecs?|microseconds?)"
    cdef str ns = r"(?P<ns>[\d.]+)(?:ns|nsecs?|nanosecs?|nanoseconds?)"

    # clock format capture groups
    cdef str day_clock = (
        r"(?P<D>\d+):(?P<h>\d{2}):(?P<m>\d{2}):(?P<s>\d{2}(?:\.\d+)?)"
    )
    cdef str hour_clock = r"(?P<h>\d+):(?P<m>\d{2}):(?P<s>\d{2}(?:\.\d+)?)"
    cdef str minute_clock = r"(?P<m>\d{1,2}):(?P<s>\d{2}(?:\.\d+)?)"
    cdef str second_clock = r":(?P<s>\d{2}(?:\.\d+)?)"

    # mark optional groups with included separators
    opt = lambda x: rf"(?:{x}(?:[,/])?)?"

    # compiled timedelta formats
    return {
        "abbrev": re.compile(
            rf"^(?P<sign>[+-])?{opt(Y)}{opt(M)}{opt(W)}{opt(D)}{opt(h)}"
            rf"{opt(m)}{opt(s)}{opt(ms)}{opt(us)}{opt(ns)}$"
        ),
        "day": re.compile(rf"^(?P<sign>[+-])?{opt(W)}{opt(D)}{hour_clock}$"),
        "hour": re.compile(rf"^(?P<sign>[+-])?{day_clock}$"),
        "minute": re.compile(rf"^(?P<sign>[+-])?{minute_clock}$"),
        "second": re.compile(rf"^(?P<sign>[+-])?{second_clock}$")
    }


# compiled timedelta string regex
cdef dict timedelta_regex = timedelta_formats_regex()


def timedelta_string_to_ns(
    str input_string,
    bint as_hours,
    epoch.Epoch since
) -> int:
    """Convert a timedelta string into an integer number of nanoseconds."""
    cdef object match
    cdef char sign
    cdef str time_format
    cdef object regex
    cdef dict groups
    cdef object result
    cdef str k
    cdef str v

    # preprocess - remove whitespace and case
    input_string = re.sub(r"\s+", "", input_string).lower()

    # test all possible formats
    for time_format, regex in timedelta_regex.items():
        match = regex.match(input_string)
        if match and match.group():  # match found and not empty
            groups = match.groupdict()
            sign = -1 if groups["sign"] == "-" else 1

            # strings of the form '1:22' are ambiguous; do they represent
            # hours and minutes or minutes and seconds?  By default, we assume
            # the latter, but if `as_hours=True`, we reverse that assumption
            if as_hours and time_format == "minute":
                groups["h"] = groups["m"]
                groups["m"] = groups["s"]
                groups.pop("s")

            # build result
            result = 0
            for k, v in groups.items():
                if not v or k == "sign":
                    continue
                if k == "M":
                    result += int(round_months_to_ns(
                        decimal.Decimal(v),
                        since=since
                    ))
                elif k == "Y":
                    result += int(round_years_to_ns(
                        decimal.Decimal(v),
                        since=since
                    ))
                else:
                    result += int(as_ns[k] * decimal.Decimal(v))
            return sign * result

    # string could not be matched
    raise ValueError(f"could not parse timedelta string: {repr(input_string)}")
