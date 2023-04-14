"""Customizable epochs for datetime/timedelta calculations.
"""
import numpy as np
cimport numpy as np
import pandas as pd

cimport pdcast.util.time.calendar as calendar
import pdcast.util.time.calendar as calendar
import pdcast.util.time.datetime as datetime_util
cimport pdcast.util.time.unit as unit
import pdcast.util.time.unit as unit
from pdcast.util.type_hints import datetime_like


#########################
####    CONSTANTS    ####
#########################


# aliases for commonly-encountered computing epochs
cdef dict epoch_aliases = {  # earliest - latest
    "julian":           np.datetime64("-4713-11-24 12:00:00"),
    "gregorian":        np.datetime64("1582-10-14 00:00:00"),
    "ntfs":             np.datetime64("1601-01-01 00:00:00"),
    "modified julian":  np.datetime64("1858-11-16 00:00:00"),
    "reduced julian":   np.datetime64("1858-11-16 12:00:00"),
    "lotus":            np.datetime64("1899-12-30 00:00:00"),
    "ntp":              np.datetime64("1900-01-01 00:00:00"),
    "risc":             np.datetime64("1900-01-01 00:00:00"),  # alias
    "labview":          np.datetime64("1904-01-01 00:00:00"),
    "sas":              np.datetime64("1960-01-01 00:00:00"),
    "utc":              np.datetime64("1970-01-01 00:00:00"),
    "unix":             np.datetime64("1970-01-01 00:00:00"),  # alias
    "posix":            np.datetime64("1970-01-01 00:00:00"),  # alias
    "fat":              np.datetime64("1980-01-01 00:00:00"),
    "zip":              np.datetime64("1980-01-01 00:00:00"),  # alias
    "gps":              np.datetime64("1980-01-06 00:00:00"),
    "j2000":            np.datetime64("2000-01-01 12:00:00"),
    "cocoa":            np.datetime64("2001-01-01 00:00:00")
}


epoch_aliases_public = epoch_aliases


######################
####    PUBLIC    ####
######################


cdef class Epoch:
    """Convert an epoch specifier into an integer nanosecond offset from the
    UTC epoch (1970-01-01 00:00:00).

    Parameters
    ----------
    arg : str | datetime-like
        The epoch specifier to use.  Can be one of the shorthand specifiers
        given in ``epoch_aliases`` or a direct datetime object.

    Returns
    -------
    Epoch
        An integer nanosecond offset from the UTC epoch.
    """

    def __init__(self, date: str | datetime_like):
        if isinstance(date, str):
            date = epoch_aliases[date.lower()]

        # separate calendar date components
        if isinstance(date, np.datetime64):
            Y, M, D = [date.astype(f"M8[{x}]") for x in "YMD"]
            self.year = Y.astype(np.int64) + 1970
            self.month = (M - Y).astype(np.uint8) + 1
            self.day = (D - M).astype(np.uint8) + 1
        else:  # duck type
            self.year = date.year
            self.month = date.month
            self.day = date.day

        # get nanosecond offset from UTC
        if isinstance(date, pd.Timestamp):
            self.offset = datetime_util.pandas_timestamp_to_ns(date)
        elif isinstance(date, np.datetime64):
            self.offset = datetime_util.numpy_datetime64_to_ns(date)
        else:  # duck type (datetime.datetime)
            self.offset = datetime_util.pydatetime_to_ns(date)

        # compute ordinal day
        ordinal = calendar.date_to_days(self.year, self.month, self.day)
        ordinal -= calendar.date_to_days(self.year, 1, 1)
        self.ordinal = ordinal + 1

    def __bool__(self) -> bool:
        return bool(self.offset)

    def __int__(self) -> int:
        return self.offset

    def __repr__(self) -> str:
        return f"{np.datetime64(self.offset // 10**9, 's')}"

    def __str__(self) -> str:
        return f"{np.datetime64(self.offset // 10**9, 's')}"

    def __eq__(self, other) -> bool:
        return isinstance(other, Epoch) and self.offset == other.offset

    def __lt__(self, other) -> bool:
        return isinstance(other, Epoch) and self.offset < other.offset

    def __gt__(self, other) -> bool:
        return isinstance(other, Epoch) and self.offset > other.offset
