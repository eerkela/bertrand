from __future__ import annotations
import datetime
import decimal
import re

import numpy as np

from pdtypes.util.time import _to_ns


# TODO: All of this should be cythonized timedelta parsing should be cythonized and go in its own .pyx file


def timedelta_formats_regex():
    """Compile a set of regular expressions to capture and parse recognized
    timedelta strings.

    Matches both abbreviated ('1h22m', '1 hour, 22 minutes', etc.) and
    clock format ('01:22:00', '1:22', '00:01:22:00') strings, with precision up
    to days and/or weeks and down to nanoseconds.
    """
    # capture groups - abbreviated units ('h', 'min', 'seconds', etc.)
    # years = r"(?P<years>\d+)\s*(?:ys?|yrs?.?|years?)"
    # months = r"(?P<months>\d+)\s*(?:mos?.?|mths?.?|months?)"
    W = r"(?P<W>[\d.]+)\s*(?:w|wks?|weeks?)"
    D = r"(?P<D>[\d.]+)\s*(?:d|dys?|days?)"
    h = r"(?P<h>[\d.]+)\s*(?:h|hrs?|hours?)"
    m = r"(?P<m>[\d.]+)\s*(?:m|(mins?)|(minutes?))"
    s = r"(?P<s>[\d.]+)\s*(?:s|secs?|seconds?)"
    ms = r"(?P<ms>[\d.]+)\s*(?:ms|msecs?|millisecs?|milliseconds?)"
    us = r"(?P<us>[\d.]+)\s*(?:us|usecs?|microsecs?|microseconds?)"
    ns = r"(?P<ns>[\d.]+)\s*(?:ns|nsecs?|nanosecs?|nanoseconds?)"

    # capture groups - clock format (':' separated)
    day_clock = r"(?P<D>\d+):(?P<h>\d{2}):(?P<m>\d{2}):(?P<s>\d{2}(?:\.\d+)?)"
    hour_clock = r"(?P<h>\d+):(?P<m>\d{2}):(?P<s>\d{2}(?:\.\d+)?)"
    minute_clock =  r"(?P<m>\d{1,2}):(?P<s>\d{2}(?:\.\d+)?)"
    second_clock = r":(?P<s>\d{2}(?:\.\d+)?)"

    # wrapping functions for capture groups
    separators = r"[,/]"  # these are ignored
    optional = lambda x: rf"(?:{x}\s*(?:{separators}\s*)?)?"

    # compiled timedelta formats
    formats = [
        (rf"{optional(W)}\s*{optional(D)}\s*{optional(h)}\s*{optional(m)}\s*"
         rf"{optional(s)}\s*{optional(ms)}\s*{optional(us)}\s*{optional(ns)}"),
        rf"{optional(W)}\s*{optional(D)}\s*{hour_clock}",
        rf"{day_clock}",
        rf"{minute_clock}",
        rf"{second_clock}"
    ]
    return [re.compile(rf"\s*{fmt}\s*$") for fmt in formats]


timedelta_regex = {
    "sign": re.compile(r"\s*(?P<sign>[+|-])?\s*(?P<unsigned>.*)$"),
    "formats": timedelta_formats_regex()
}


def string_to_ns(delta: str, as_hours: bool = False) -> int:
    """Parse a timedelta string, returning its associated value as an integer
    number of nanoseconds.

    See also: https://github.com/wroberts/pytimeparse

    Parameters
    ----------
    delta (str):
        Timedelta string to parse.  Can be in either abbreviated ('1h22m',
        '1 hour, 22 minutes', ...) or clock format ('01:22:00', '1:22',
        '00:01:22:00', ...), with precision up to days/weeks and down to
        nanoseconds.  Can be either signed or unsigned.
    as_hours (bool):
        Whether to parse ambiguous timedelta strings of the form '1:22' as
        containing hours and minutes (`True`), or minutes and seconds
        (`False`).  Does not affect any other string format.

    Returns
    -------
    int:
        An integer number of nanoseconds associated with the given timedelta
        string.  If the string contains digits below nanosecond precision,
        they are destroyed.

    Raises
    ------
    ValueError:
        If the passed timedelta string does not match any of the recognized
        formats.

    Examples
    --------
    >>> parse_timedelta('1:24')
    84000000000
    >>> parse_timedelta(':22')
    22000000000
    >>> parse_timedelta('1 minute, 24 secs')
    84000000000
    >>> parse_timedelta('1m24s')
    84000000000
    >>> parse_timedelta('1.2 minutes')
    72000000000
    >>> parse_timedelta('1.2 seconds')
    1200000000

    Time expressions can be signed.
    >>> parse_timedelta('- 1 minute')
    -60000000000
    >>> parse_timedelta('+ 1 minute')
    60000000000

    If `as_hours=True`, then ambiguous digits following a colon will be
    interpreted as minutes; otherwise they are considered to be seconds.
    >>> timeparse('1:30', as_hours=False)
    90000000000
    >>> timeparse('1:30', as_hours=True)
    5400000000000
    """
    # get sign if present
    match = timedelta_regex["sign"].match(delta)
    sign = -1 if match.groupdict()["sign"] == "-" else 1

    # strip sign and test all possible formats
    delta = match.groupdict()["unsigned"]
    for time_format in timedelta_regex["formats"]:

        # attempt match
        match = time_format.match(delta)
        if match and match.group().strip():  # match found and not empty

            # get dict of named subgroups (?P<...>) and associated values
            groups = match.groupdict()

            # strings of the form '1:22' are ambiguous.  By default, we assume
            # minutes and seconds, but if `as_hours=True`, we interpret as
            # hours and minutes instead
            if (as_hours and delta.count(":") == 1 and "." not in delta and
                not any(groups.get(x, None) for x in ["h", "D", "W"])):
                groups["h"] = groups["m"]
                groups["m"] = groups["s"]
                groups.pop("s")

            # build result
            result = sum(_to_ns[k] * decimal.Decimal(v)
                         for k, v in groups.items() if v)
            return int(sign * result)

    # string could not be matched
    raise ValueError(f"could not parse timedelta string: {repr(delta)}")


def parse_iso_8601_strings(arr: np.ndarray) -> np.ndarray:
    """test"""
    arr = arr.astype("M8[us]")

    # TODO: have to manually check for overflow

    min_val = arr.min()
    max_val = arr.max()
    min_datetime = np.datetime64(datetime.datetime.min)
    max_datetime = np.datetime64(datetime.datetime.max)

    if min_val < min_datetime or max_val > max_datetime:
        raise ValueError()

    return arr.astype("O")
