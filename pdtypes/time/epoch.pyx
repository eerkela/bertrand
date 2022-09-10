import numpy as np
cimport numpy as np

from pdtypes.check import check_dtype
from pdtypes.util.array import is_scalar
from pdtypes.util.type_hints import datetime_like

from .date import decompose_date
from .datetime import datetime_to_ns, string_to_datetime


#########################
####    Constants    ####
#########################


cdef dict epoch_aliases = {  # earliest - latest
    "julian":           np.datetime64("-4713-11-24 12:00:00"),
    "gregorian":        np.datetime64("1582-10-14 00:00:00"),
    "ntfs":             np.datetime64("1601-01-01 00:00:00"),
    "modified julian":  np.datetime64("1858-11-16 00:00:00"),
    "reduced julian":   np.datetime64("1858-11-16 12:00:00"),
    "lotus":            np.datetime64("1899-12-30 00:00:00"),
    "risc":             np.datetime64("1900-01-01 00:00:00"),
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


######################
####    Public    ####
######################


def epoch(arg: str | datetime_like):
    """Convert an epoch specifier into a corresponding datetime object.

    If a string input can be interpreted as a datetime (of any kind), then it
    will be returned as such.  Additionally, a number of alias strings are
    allowed, as follows (earliest - latest):
        - `'julian'`: refers to the start of the Julian period, which
            corresponds to a historical date of January 1st, 4713 BC (according
            to the proleptic Julian calendar) or November 24, 4714 BC
            (according to the proleptic Gregorian calendar).  Commonly used in
            astronomical applications.
        - `'gregorian'`: refers to October 14th, 1582, the date when Pope
            Gregory XIII first instituted the Gregorian calendar.
        - `'ntfs'`: refers to January 1st, 1601.  Used by Microsoft's NTFS file
            management system.
        - `'modified julian'`: equivalent to `'reduced julian'` except that
            it increments at midnight rather than noon.  This was originally
            introduced by the Smithsonian Astrophysical Observatory to track
            the orbit of Sputnik, the first man-made satellite to orbit Earth.
        - `'reduced julian'`: refers to November 16th, 1858, which drops the
            first two leading digits of the corresponding `'julian'` day
            number.
        - `'lotus'`: refers to December 31st, 1899, which was incorrectly
            identified as January 0, 1900 in the original Lotus 1-2-3
            implementation.  Still used internally in a variety of spreadsheet
            applications, including Microsoft Excel, Google Sheets, and
            LibreOffice.
        - `'risc'`: refers to January 1st, 1900, which is used for Network
            Time Protocol (NTP) synchronization, IBM CICS, Mathematica, Common
            Lisp, and the RISC operating system.
        - `'labview'`: refers to January 1st, 1904, which is used by the
            LabVIEW laboratory control software.
        - `'sas'`: refers to January 1st, 1960, which is used by the SAS
            statistical analysis suite.
        - `'utc'`: refers to January 1st, 1970, the universal Unix/POSIX epoch.
            Equivalent to `'unix'`/`'posix'`.
        - `'fat'`: refers to January 1st, 1980, which is used by the FAT
            file management system, as well as ZIP and its derivatives.
            Equivalent to `'zip'`.
        - `'gps'`: refers to January 6th, 1980, which is used in most GPS
            systems.
        - `'j2000'`: refers to January 1st, 2000, which is commonly used in
            astronomical applications, as well as in PostgreSQL.
        - `'cocoa'`: refers to January 1st, 2001, which is used in Apple's
            Cocoa framework for macOS and related mobile devices.

    Note: by convention, `'julian'`, `'reduced_julian'`, and `'j2000'` dates
    increment at noon (12:00:00 UTC) on the corresponding day.
    """
    if not is_scalar(arg):
        raise ValueError(f"`epoch` must be scalar, not {epoch}")

    # datetime-like (trivial)
    if check_dtype(arg, "datetime"):
        return arg

    # string
    if isinstance(arg, str):
        return epoch_aliases.get(arg, string_to_datetime(arg))

    # epoch type not recognized
    raise TypeError(f"`epoch` must be a datetime string or object, not "
                    f"{type(arg)}")


def epoch_date(arg: str | datetime_like):
    """Return the date components `(year, month, day)` of the given epoch
    specifier according to the proleptic Gregorian calendar.
    """
    return decompose_date(epoch(arg))


def epoch_ns(arg: str | datetime_like):
    """Return the nanosecond offset from utc for the given epoch specifier."""
    return datetime_to_ns(epoch(arg))
