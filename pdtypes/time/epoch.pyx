"""Customizable epochs for datetime/timedelta calculations.

Functions
---------
    epoch(arg: str | datetime_like) -> datetime_like:
        Convert an epoch specifier into a corresponding datetime object.

    epoch_date(arg: str | datetime_like) -> dict[str, int]:
        Return the date components `(year, month, day)` of the given epoch
        specifier according to the proleptic Gregorian calendar.

    epoch_ns(arg: str | datetime_like) -> int:
        Return a nanosecond offset from the UTC epoch
        ('1970-01-01 00:00:00+0000') for a given epoch specifier.

Examples
--------
    Epoch specifiers can be shorthand strings.
    >>> epoch("utc")
    numpy.datetime64('1970-01-01T00:00:00')
    >>> epoch("unix")
    numpy.datetime64('1970-01-01T00:00:00')
    >>> epoch("julian")
    numpy.datetime64('-4713-11-24T12:00:00')
    >>> epoch("reduced julian")
    numpy.datetime64('1858-11-16T12:00:00')

    They can also be datetime strings.
    >>> epoch("2022-01-04 00:00:00+0000")
    Timestamp('2001-01-04 00:00:00')
    >>> epoch("4 Jan 2022")
    Timestamp('2022-01-04 00:00:00')
    >>> epoch("4Q2023")
    Timestamp('2023-10-01 00:00:00')

    Or datetime objects directly.
    >>> epoch(pd.Timestamp("2022-01-04 00:00:00+0000"))
    Timestamp('2022-01-04 00:00:00+0000', tz='UTC')
    >>> epoch(datetime.datetime.fromisoformat("2022-01-04 00:00:00"))
    datetime.datetime(2022, 1, 4, 0, 0)
    >>> epoch(np.datetime64("2022-01-04 00:00:00"))
    numpy.datetime64('2022-01-04T00:00:00')

    They can also be used to create **kwargs dicts for ``date_to_days()``
    >>> epoch_date("utc")
    {'year': 1970, 'month': 1, 'day': 1}
    >>> epoch_date("julian")
    {'year': -4713, 'month': 11, 'day': 24}
    >>> epoch_date("reduced julian")
    {'year': 1858, 'month': 11, 'day': 16}
    >>> epoch_date("2022-01-04 00:00:00+0000")
    {'year': 2022, 'month': 1, 'day': 4}
    >>> epoch_date("4 Jan 2022")
    {'year': 2022, 'month': 1, 'day': 4}
    >>> epoch_date("4Q2023")
    {'year': 2023, 'month': 10, 'day': 1}
    >>> epoch_date(pd.Timestamp("2022-01-04 00:00:00+0000"))
    {'year': 2022, 'month': 1, 'day': 4}
    >>> epoch_date(datetime.datetime.fromisoformat("2022-01-04 00:00:00"))
    {'year': 2022, 'month': 1, 'day': 4}
    >>> epoch_date(np.datetime64("2022-01-04 00:00:00"))
    {'year': 2022, 'month': 1, 'day': 4}

    Or nanosecond offsets for ``ns_to_datetime()``
    >>> epoch_ns("utc")
    0
    >>> epoch_ns("julian")
    -210866760000000000000
    >>> epoch_ns("reduced julian")
    -3506760000000000000
    >>> epoch_ns("2022-01-04 00:00:00+0000")
    1641254400000000000
    >>> epoch_ns("4 Jan 2022")
    1641254400000000000
    >>> epoch_ns("4Q2023")
    1696118400000000000
    >>> epoch_ns(pd.Timestamp("2022-01-04 00:00:00+0000"))
    1641254400000000000
    >>> epoch_ns(datetime.datetime.fromisoformat("2022-01-04 00:00:00"))
    1641254400000000000
    >>> epoch_ns(np.datetime64("2022-01-04 00:00:00"))
    1641254400000000000
"""
import numpy as np
cimport numpy as np

from pdtypes.check import check_dtype
from pdtypes.util.array import is_scalar
from pdtypes.util.type_hints import datetime_like

from .calendar import decompose_date
from .datetime import datetime_to_ns, string_to_datetime


#########################
####    Constants    ####
#########################


# aliases for commonly-encountered computing epochs
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


def epoch(arg: str | datetime_like) -> datetime_like:
    """Convert an epoch specifier into a corresponding datetime object.

    If a string input can be interpreted as a datetime (of any kind), then it
    will be returned as such.  Additionally, a number of alias strings are
    allowed, as follows (earliest - latest):
        * ``'julian'``: refers to the start of the Julian period, which
            corresponds to a historical date of January 1st, 4713 BC (according
            to the proleptic Julian calendar) or November 24, 4714 BC
            (according to the proleptic Gregorian calendar).  Commonly used in
            astronomical applications.
        * ``'gregorian'``: refers to October 14th, 1582, the date when Pope
            Gregory XIII first instituted the Gregorian calendar.
        * ``'ntfs'``: refers to January 1st, 1601.  Used by Microsoft's NTFS
            file management system.
        * ``'modified julian'``: equivalent to `'reduced julian'` except that
            it increments at midnight rather than noon.  This was originally
            introduced by the Smithsonian Astrophysical Observatory to track
            the orbit of Sputnik, the first man-made satellite to orbit Earth.
        * ``'reduced julian'``: refers to November 16th, 1858, which drops the
            first two leading digits of the corresponding ``'julian'`` day
            number.
        * ``'lotus'``: refers to December 31st, 1899, which was incorrectly
            identified as January 0, 1900 in the original Lotus 1-2-3
            implementation.  Still used internally in a variety of spreadsheet
            applications, including Microsoft Excel, Google Sheets, and
            LibreOffice.
        * ``'risc'``: refers to January 1st, 1900, which is used for Network
            Time Protocol (NTP) synchronization, IBM CICS, Mathematica, Common
            Lisp, and the RISC operating system.
        * ``'labview'``: refers to January 1st, 1904, which is used by the
            LabVIEW laboratory control software.
        * ``'sas'``: refers to January 1st, 1960, which is used by the SAS
            statistical analysis suite.
        * ``'utc'``: refers to January 1st, 1970, the universal Unix/POSIX
            epoch.  Equivalent to `'unix'`/`'posix'`.
        * ``'fat'``: refers to January 1st, 1980, which is used by the FAT
            file management system, as well as ZIP and its derivatives.
            Equivalent to `'zip'`.
        * ``'gps'``: refers to January 6th, 1980, which is used in most GPS
            systems.
        * ``'j2000'``: refers to January 1st, 2000, which is commonly used in
            astronomical applications, as well as in PostgreSQL.
        * ``'cocoa'``: refers to January 1st, 2001, which is used in Apple's
            Cocoa framework for macOS and related mobile devices.

        Note: by convention, ``'julian'``, ``'reduced_julian'``, and
        ``'j2000'`` dates increment at noon (12:00:00 UTC) on the corresponding
        day.

    Parameters
    ----------
    arg : str | datetime-like
        The epoch specifier to use.  Can be one of the above shorthand
        specifiers, a direct datetime object, or a datetime string that can
        be parsed by ``string_to_datetime()``.

    Returns
    -------
    datetime-like
        A datetime object that corresponds to the given epoch.

    Raises
    ------
    ValueError
        If ``arg`` is vectorized.
    TypeError
        If ``arg`` is not a string or datetime object.

    Examples
    --------
    Epoch specifiers can be shorthand strings.
    >>> epoch("utc")
    numpy.datetime64('1970-01-01T00:00:00')
    >>> epoch("unix")
    numpy.datetime64('1970-01-01T00:00:00')
    >>> epoch("julian")
    numpy.datetime64('-4713-11-24T12:00:00')
    >>> epoch("reduced julian")
    numpy.datetime64('1858-11-16T12:00:00')

    They can also be datetime strings.
    >>> epoch("2022-01-04 00:00:00+0000")
    Timestamp('2001-01-04 00:00:00')
    >>> epoch("4 Jan 2022")
    Timestamp('2022-01-04 00:00:00')
    >>> epoch("4Q2023")
    Timestamp('2023-10-01 00:00:00')

    Or datetime objects directly.
    >>> epoch(pd.Timestamp("2022-01-04 00:00:00+0000"))
    Timestamp('2022-01-04 00:00:00+0000', tz='UTC')
    >>> epoch(datetime.datetime.fromisoformat("2022-01-04 00:00:00"))
    datetime.datetime(2022, 1, 4, 0, 0)
    >>> epoch(np.datetime64("2022-01-04 00:00:00"))
    numpy.datetime64('2022-01-04T00:00:00')
    """
    if not is_scalar(arg):
        raise ValueError(f"`epoch` must be scalar, not {epoch}")

    # datetime-like (trivial)
    if check_dtype(arg, "datetime"):
        return arg

    # string
    if isinstance(arg, str):
        lower = arg.lower()
        if lower in epoch_aliases:
            return epoch_aliases[lower]
        return string_to_datetime(arg)

    # epoch type not recognized
    raise TypeError(f"`epoch` must be a datetime string or object, not "
                    f"{type(arg)}")


def epoch_date(arg: str | datetime_like) -> dict[str, int]:
    """Return the date components `(year, month, day)` of the given epoch
    specifier according to the proleptic Gregorian calendar.

    See ``epoch()`` for a list of available specifiers.

    Parameters
    ----------
    arg : str | datetime-like
        The epoch specifier to use.  Can be one of the shorthand specifiers
        listed in ``epoch()``, a direct datetime object, or a datetime string
        that can be parsed by ``string_to_datetime()``.

    Returns
    -------
    dict[str, int]
        A dictionary with the following keys/values:
            * `'year'`: Proleptic Gregorian calendar year.
            * `'month'`: Proleptic Gregorian calendar month, indexed from 1.
            * `'day'`: Proleptic Gregorian calendar day, indexed from 1.

        This dictionary can be used as a **kwargs dict for ``date_to_days()``.

            Note: This function assumes the existence of a year 0, which does
            not correspond to real-world historical dates.  In order to convert
            a historical BC year (`-1 BC`, `-2 BC`, ...) to a negative year
            (`0`, `-1`, ...), simply add one to the BC year.  AD years are
            unaffected.

    Raises
    ------
    ValueError
        If ``arg`` is vectorized.
    TypeError
        If ``arg`` is not a string or datetime object.

    Examples
    --------
    Epochs can be in any form accepted by ``epoch()``
    >>> epoch_date("utc")
    {'year': 1970, 'month': 1, 'day': 1}
    >>> epoch_date("julian")
    {'year': -4713, 'month': 11, 'day': 24}
    >>> epoch_date("reduced julian")
    {'year': 1858, 'month': 11, 'day': 16}
    >>> epoch_date("2022-01-04 00:00:00+0000")
    {'year': 2022, 'month': 1, 'day': 4}
    >>> epoch_date("4 Jan 2022")
    {'year': 2022, 'month': 1, 'day': 4}
    >>> epoch_date("4Q2023")
    {'year': 2023, 'month': 10, 'day': 1}
    >>> epoch_date(pd.Timestamp("2022-01-04 00:00:00+0000"))
    {'year': 2022, 'month': 1, 'day': 4}
    >>> epoch_date(datetime.datetime.fromisoformat("2022-01-04 00:00:00"))
    {'year': 2022, 'month': 1, 'day': 4}
    >>> epoch_date(np.datetime64("2022-01-04 00:00:00"))
    {'year': 2022, 'month': 1, 'day': 4}
    """
    return decompose_date(epoch(arg))


def epoch_ns(arg: str | datetime_like) -> int:
    """Return a nanosecond offset from the UTC epoch
    ('1970-01-01 00:00:00+0000') for a given epoch specifier.

    See ``epoch()`` for a list of available specifiers.

    Parameters
    ----------
    arg : str | datetime-like
        The epoch specifier to use.  Can be one of the shorthand specifiers
        listed in ``epoch()``, a direct datetime object, or a datetime string
        that can be parsed by ``string_to_datetime()``.

    Returns
    -------
    int
        The utc nanosecond offset associated with the given epoch.

    Raises
    ------
    ValueError
        If ``arg`` is vectorized.
    TypeError
        If ``arg`` is not a string or datetime object.

    Examples
    --------
    Epochs can be in any form accepted by ``epoch()``
    >>> epoch_ns("utc")
    0
    >>> epoch_ns("julian")
    -210866760000000000000
    >>> epoch_ns("reduced julian")
    -3506760000000000000
    >>> epoch_ns("2022-01-04 00:00:00+0000")
    1641254400000000000
    >>> epoch_ns("4 Jan 2022")
    1641254400000000000
    >>> epoch_ns("4Q2023")
    1696118400000000000
    >>> epoch_ns(pd.Timestamp("2022-01-04 00:00:00+0000"))
    1641254400000000000
    >>> epoch_ns(datetime.datetime.fromisoformat("2022-01-04 00:00:00"))
    1641254400000000000
    >>> epoch_ns(np.datetime64("2022-01-04 00:00:00"))
    1641254400000000000
    """
    return datetime_to_ns(epoch(arg))
