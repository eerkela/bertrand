from __future__ import annotations
from typing import Any, Callable

import pytz

import pdcast.convert.standalone as standalone
import pdcast.resolve as resolve
import pdcast.types as types

from pdcast.util.round import Tolerance, valid_rules
from pdcast.util.time import timezone, valid_units, Epoch, epoch_aliases
from pdcast.util.type_hints import numeric, datetime_like, type_specifier


# ignore this file when doing string-based object lookups in resolve_type()
_ignore_frame_objects = True


#######################
####    PRIVATE    ####
#######################


valid_errors = ("raise", "coerce", "ignore")


def as_string_set(val: str | set[str]) -> set[str]:
    """Create a set of strings from a scalar or iterable."""
    # scalar string
    if isinstance(val, str):
        return {val}

    # iterable
    if hasattr(val, "__iter__"):
        if not all(isinstance(x, str) for x in val):
            raise TypeError(
                f"input must consist only of strings: {val}"
            )
        return set(val)

    # everything else
    return {str(val)}


def assert_sets_are_disjoint(true: set, false: set) -> None:
    """Raise a `ValueError` if the sets have any overlap."""
    if not true.isdisjoint(false):
        err_msg = f"`true` and `false` must be disjoint"

        intersection = true.intersection(false)
        if len(intersection) == 1:  # singular
            err_msg += (
                f"({repr(intersection.pop())} is present in both sets)"
            )
        else:  # plural
            err_msg += f"({intersection} are present in both sets)"

        raise ValueError(err_msg)


#########################
####    ARGUMENTS    ####
#########################


@standalone.cast.register_arg(default=1e-6)
def tol(val: numeric, defaults: dict) -> Tolerance:
    """The maximum amount of precision loss that can occur before an error
    is raised.

    Returns
    -------
    Tolerance
        A ``Tolerance`` object that consists of two ``Decimal`` values, one
        for both the real and imaginary components.  This maintains the
        highest possible precision in both cases.  Default is ``1e-6``.

    Notes
    -----
    Precision loss is defined using a 2-sided window around each of the
    observed values.  The size of this window is directly controlled by
    this argument.  If a conversion causes any value to be coerced outside
    this window, then a ``ValueError`` will be raised.

    This argument only affects numeric conversions.

    Examples
    --------
    The input to this argument must be a positive numeric that is
    coercible to ``Decimal``.

    .. doctest::

        >>> pdcast.cast(1.001, "int", tol=0.01)
        0    1
        dtype: int64
        >>> pdcast.cast(1.001, "int", tol=0)
        Traceback (most recent call last):
            ...
        ValueError: precision loss exceeds tolerance 0 at index [0]

    If a complex value is given, then its real and imaginary components
    will be considered separately.

    .. doctest::

        >>> pdcast.cast(1.001+0.001j, "int", tol=0.01+0.01j)
        0    1
        dtype: int64
        >>> pdcast.cast(1.001+0.001j, "int", tol=0.01+0j)
        Traceback (most recent call last):
            ...
        ValueError: imaginary component exceeds tolerance 0 at index [0]

    This argument also has special behavior around the min/max of bounded
    numerics, like integers and booleans.  If a value would normally
    overflow, but falls within tolerance of these bounds, then it will be
    clipped to fit rather than raise an ``OverflowError``.

    .. doctest::

        >>> pdcast.cast(129, "int8", tol=2)
        0    127
        dtype: int8
        >>> pdcast.cast(129, "int8", tol=0)
        Traceback (most recent call last):
            ...
        OverflowError: values exceed int8 range at index [0]

    Additionally, this argument controls the maximum amount of precision
    loss that can occur when :attr:`downcasting <CastDefaults.downcast>`
    numeric values.

    .. doctest::

        >>> pdcast.cast(1.1, "float", tol=0, downcast=True)
        0    1.1
        dtype: float64
        >>> pdcast.cast(1.1, "float", tol=0.001, downcast=True)
        0    1.099609
        dtype: float16

    Setting this to infinity ignores precision loss entirely.

    .. doctest::

        >>> pdcast.cast(1.5, "int", tol=np.inf)
        0    2
        dtype: int64
        >>> pdcast.cast(np.inf, "int64", tol=np.inf)
        0    9223372036854775807
        dtype: int64

    .. note::

        For integer conversions, this is equivalent to setting
        :attr:`rounding <CastDefaults.rounding>` to ``"half_even"``, with
        additional clipping around the minimum and maximum values.
    """
    return Tolerance(val)


@standalone.cast.register_arg(default=None)
def rounding(val: str | None, defaults: dict) -> str:
    """The rounding rule to use for numeric conversions.

    Returns
    -------
    str | None
        A string describing the rounding rule to apply, or ``None`` to
        indicate that no rounding will be applied.  Default is ``None``.

    Notes
    -----
    The available options for this argument are as follows:

        *   ``None`` - do not round.
        *   ``"floor"`` - round toward negative infinity.
        *   ``"ceiling"`` - round toward positive infinity.
        *   ``"down"`` - round toward zero.
        *   ``"up"`` - round away from zero.
        *   ``"half_floor"`` - round to nearest with ties toward positive infinity.
        *   ``"half_ceiling"`` - round to nearest with ties toward negative
            infinity.
        *   ``"half_down"`` - round to nearest with ties toward zero.
        *   ``"half_up"`` - round to nearest with ties away from zero.
        *   ``"half_even"`` - round to nearest with ties toward the `nearest even
            value <https://en.wikipedia.org/wiki/Rounding#Rounding_half_to_even>`_.
            Also known as *convergent rounding*, *statistician's rounding*, or
            *banker's rounding*.

    This argument is applied **after** :attr:`tol <CastDefaults.tol>`.

    Examples
    --------
    .. doctest::

        >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="floor")
        0   -2
        1   -1
        2    0
        3    1
        dtype: int64
        >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="ceiling")
        0   -1
        1    0
        2    1
        3    2
        dtype: int64
        >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="down")
        0   -1
        1    0
        2    0
        3    1
        dtype: int64
        >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="up")
        0   -2
        1   -1
        2    1
        3    2
        dtype: int64
        >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_floor")
        0   -2
        1   -1
        2    0
        3    2
        dtype: int64
        >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_ceiling")
        0   -1
        1    0
        2    0
        3    2
        dtype: int64
        >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_down")
        0   -1
        1    0
        2    0
        3    2
        dtype: int64
        >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_up")
        0   -2
        1   -1
        2    0
        3    2
        dtype: int64
        >>> pdcast.cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_even")
        0   -2
        1    0
        2    0
        3    2
        dtype: int64

    """
    if val is not None and val not in valid_rules:
        raise ValueError(
            f"`rounding` must be one of {valid_rules}, not {repr(val)}"
        )
    return val


@standalone.cast.register_arg(default="ns")
def unit(val: str, defaults: dict) -> str:
    """The unit to use for numeric <-> datetime/timedelta conversions.

    Returns
    -------
    str
        A string describing the unit to use.  Default is ``"ns"``

    Notes
    -----
    The available options for this argument are as follows (from smallest
    to largest):

        * ``"ns"`` - nanoseconds
        * ``"us"`` - microseconds
        * ``"ms"`` - milliseconds
        * ``"s"`` - seconds
        * ``"m"`` - minutes
        * ``"h"`` - hours
        * ``"D"`` - days
        * ``"W"`` - weeks
        * ``"M"`` - months
        * ``"Y"`` - years

    Examples
    --------
    .. doctest::

        >>> pdcast.cast(1, "datetime", unit="ns")
        0   1970-01-01 00:00:00.000000001
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="us")
        0   1970-01-01 00:00:00.000001
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="ms")
        0   1970-01-01 00:00:00.001
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="s")
        0   1970-01-01 00:00:01
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="m")
        0   1970-01-01 00:01:00
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="h")
        0   1970-01-01 01:00:00
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="D")
        0   1970-01-02
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="W")
        0   1970-01-08
        dtype: datetime64[ns]

    Units ``"M"`` and ``"Y"`` have irregular lengths.  Rather than average
    these like ``pandas.to_datetime()``, :func:`cast` gives
    calendar-accurate results.

    .. doctest::

        >>> pdcast.cast(1, "datetime", unit="M")
        0   1970-02-01
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="Y")
        0   1971-01-01
        dtype: datetime64[ns]

    This accounts for leap years as well, following the `Gregorian calendar
    <https://en.wikipedia.org/wiki/Gregorian_calendar>`_.

    .. doctest::

        >>> pdcast.cast(1, "datetime", unit="M", since="1972-02")
        0   1972-03-01
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="Y", since="1972")
        0   1973-01-01
        dtype: datetime64[ns]
    """
    if val not in valid_units:
        raise ValueError(
            f"`unit` must be one of {valid_units}, not {repr(val)}"
        )
    return val


@standalone.cast.register_arg(default=1)
def step_size(val: int, defaults: dict) -> int:
    """The step size to use for each :attr:`unit <CastDefaults.unit>`.

    Returns
    -------
    int
        A positive integer >= 1.  This is effectively a multiplier for
        :attr:`unit <CastDefaults.unit>`.  Default is ``1``.

    Examples
    --------
    .. doctest::

        >>> pdcast.cast(1, "datetime", unit="ns", step_size=5)
        0   1970-01-01 00:00:00.000000005
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="s", step_size=30)
        0   1970-01-01 00:00:30
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="M", step_size=3)
        0   1970-04-01
        dtype: datetime64[ns]
    """
    if not isinstance(val, int) or val < 1:
        raise ValueError(f"`step_size` must be an integer >= 1, not {val}")
    return val


@standalone.cast.register_arg(default="utc")
def since(val: str | datetime_like, defaults: dict) -> Epoch:
    """The epoch to use for datetime/timedelta conversions.

    Returns
    -------
    Epoch
        An ``Epoch`` object that represents a nanosecond offset from the
        UTC epoch (1970-01-01 00:00:00).  Defaults to UTC.

    Notes
    -----
    This can accept any datetime-like value, including datetime strings,
    actual datetime objects, or one of the special values listed below
    (from earliest to latest):

        *   ``"julian"``: refers to the start of the `Julian
            <https://en.wikipedia.org/wiki/Julian_calendar>`_ period, which
            corresponds to a historical date of January 1st, 4713 BC
            (according to the `proleptic Julian calendar
            <https://en.wikipedia.org/wiki/Proleptic_Julian_calendar>`_) or
            November 24, 4714 BC (according to the `proleptic Gregorian
            calendar <https://en.wikipedia.org//wiki/Proleptic_Gregorian_calendar>`_)
            at noon.
        *   ``"gregorian"``: refers to October 14th, 1582, the date when
            Pope Gregory XIII first instituted the `Gregorian calendar
            <https://en.wikipedia.org/wiki/Gregorian_calendar>`_.
        *   ``"ntfs"``: refers to January 1st, 1601.  Used by Microsoft's
            `NTFS <https://en.wikipedia.org/wiki/NTFS>`_ file management
            system.
        *   ``"modified julian"``: equivalent to ``"reduced julian"``
            except that it increments at midnight rather than noon.  This
            was originally introduced by the `Smithsonian Astrophysical
            Observatory <https://en.wikipedia.org/wiki/Smithsonian_Astrophysical_Observatory>`_
            to track the orbit of Sputnik, the first man-made satellite to
            orbit Earth.
        *   ``"reduced julian"``: refers to noon on November 16th, 1858,
            which drops the first two leading digits of the corresponding
            ``"julian"`` day number.
        *   ``"lotus"``: refers to December 31st, 1899, which was
            incorrectly identified as January 0, 1900 in the original
            `Lotus 1-2-3 <https://en.wikipedia.org/wiki/Lotus_1-2-3>`_
            implementation.  Still used internally in a variety of
            spreadsheet applications, including Microsoft Excel, Google
            Sheets, and LibreOffice.
        *   ``"ntp"``: refers to January 1st, 1900, which is used for
            `Network Time Protocol (NTP)
            <https://en.wikipedia.org/wiki/Network_Time_Protocol>`_
            synchronization, `IBM CICS
            <https://en.wikipedia.org/wiki/CICS>`_, `Mathematica
            <https://en.wikipedia.org/wiki/Wolfram_Mathematica>`_,
            `Common Lisp <https://en.wikipedia.org/wiki/Common_Lisp>`_, and
            the `RISC <https://en.wikipedia.org/wiki/RISC_OS>`_ operating
            system.
        *   ``"labview"``: refers to January 1st, 1904, which is used by
            the `LabVIEW <https://en.wikipedia.org/wiki/LabVIEW>`_
            laboratory control software.
        *   ``"sas"``: refers to January 1st, 1960, which is used by the
            `SAS <https://en.wikipedia.org/wiki/SAS_(software)>`_
            statistical analysis suite.
        *   ``"utc"``: refers to January 1st, 1970, the universal
            `Unix <https://en.wikipedia.org/wiki/Unix>`_\ /\ `POSIX
            <https://en.wikipedia.org/wiki/POSIX>`_ epoch.  Can also be
            referred to through the ``"unix"`` and ``"posix"`` aliases.
        *   ``"fat"``: refers to January 1st, 1980, which is used by the
            `FAT <https://en.wikipedia.org/wiki/File_Allocation_Table>`_
            file management system, as well as `ZIP
            <https://en.wikipedia.org/wiki/ZIP_(file_format)>`_ and its
            derivatives.  Equivalent to ``"zip"``.
        *   ``"gps"``: refers to January 6th, 1980, which is used in most
            `GPS <https://en.wikipedia.org/wiki/Global_Positioning_System>`_
            systems.
        *   ``"j2000"``: refers to noon on January 1st, 2000, which is
            commonly used in astronomical applications, as well as in
            `PostgreSQL <https://en.wikipedia.org/wiki/PostgreSQL>`_.
        *   ``"cocoa"``: refers to January 1st, 2001, which is used in
            Apple's `Cocoa <https://en.wikipedia.org/wiki/Cocoa_(API)>`_
            framework for macOS and related mobile devices.
        *   ``"now"``: refers to the current `system time
            <https://en.wikipedia.org/wiki/System_time>`_ at the time
            :class:`cast` was invoked.

    .. note::

        By convention, ``"julian"``, ``"reduced_julian"``, and ``"j2000"``
        dates increment at noon (12:00:00 UTC) on the corresponding day.

    Examples
    --------
    Using epoch aliases:

    .. doctest::

        >>> pdcast.cast(1, "datetime", unit="s", since="j2000")
        0   2000-01-01 12:00:01
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="s", since="gregorian")
        0    1582-10-14 00:00:01
        dtype: datetime[python]
        >>> pdcast.cast(1, "datetime", unit="s", since="julian")
        0    -4713-11-24T12:00:01.000000
        dtype: object

    Using datetime strings:

    .. doctest::

        >>> pdcast.cast(1, "datetime", unit="s", since="2022-03-27")
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="s", since="27 mar 2022")
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="s", since="03/27/22")
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]

    Using datetime objects:

    .. doctest::

        >>> pdcast.cast(1, "datetime", unit="s", since=pd.Timestamp("2022-03-27"))
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="s", since=datetime.datetime(2022, 3, 27))
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]
        >>> pdcast.cast(1, "datetime", unit="s", since=np.datetime64("2022-03-27"))
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]
    """
    if isinstance(val, str) and val not in epoch_aliases:
        val = standalone.cast(val, "datetime")
        if len(val) != 1:
            raise ValueError(f"`since` must be scalar")
        val = val[0]

    return Epoch(val)


@standalone.cast.register_arg(default=None)
def tz(
    val: str | pytz.BaseTzInfo | None,
    defaults: dict
) -> pytz.BaseTzInfo:
    """Specifies a time zone to use for datetime conversions.

    Returns
    --------
    pytz.timezone | None
        A `pytz <https://pypi.org/project/pytz/>`_ timezone object
        corresponding to the input.  ``None`` indicates naive output.
        Defaults to ``None``.

    Notes
    ------
    In addition to the standard IANA time zone codes, this argument can
    accept the special string ``"local"``.  This refers to the local time
    zone for the current system at the time of execution.

    Examples
    ---------
    Time zone localization is a somewhat complicated process, with
    different behavior depending on the input data type.

    Numerics (boolean, integer, float, complex, decimal) and timedeltas are
    always computed in UTC relative to the
    :attr:`since <CastDefaults.since>` argument.  When a time zone is
    supplied via :attr:`tz <CastDefaults.tz>`, the resulting datetimes
    will be *converted* from UTC to the specified time zone.

    Strings and datetimes on the other hand are interpreted according to
    the :attr:`naive_tz <CastDefaults.naive_tz>` argument.  Any naive
    inputs will first be localized to
    :attr:`naive_tz <CastDefaults.naive_tz>` and then converted to the
    final :attr`tz <CastDefaults.tz>`.
    """
    return timezone(val)


@standalone.cast.register_arg(default=None)
def naive_tz(
    val: str | pytz.BaseTzInfo | None,
    defaults: dict
) -> pytz.BaseTzInfo:
    """The assumed time zone when localizing naive datetimes.

    Returns
    -------
    pytz.timezone | None
        A `pytz <https://pypi.org/project/pytz/>`_ timezone object
        corresponding to the input.  ``None`` indicates direct
        localization.  Defaults to ``None``. 

    Notes
    ------
    In addition to the standard IANA time zone codes, this argument can
    accept the special string ``"local"``.  This refers to the local time
    zone for the current system at the time of execution.

    Examples
    ---------
    If a :attr:`tz <CastDefaults.tz>` is given while this is set to
    ``None``, the results will be localized directly to
    :attr:`tz <CastDefaults.tz>`.
    """
    return timezone(val)


@standalone.cast.register_arg(default=False)
def day_first(val: Any, defaults: dict) -> bool:
    """Indicates whether to interpret the first value in an ambiguous
    3-integer date (e.g. 01/05/09) as the day (``True``) or month
    (``False``).

    If year_first is set to ``True``, this distinguishes between YDM and
    YMD.
    """
    return bool(val)


@standalone.cast.register_arg(default=False)
def year_first(val: Any, defaults: dict) -> bool:
    """Indicates whether to interpret the first value in an ambiguous
    3-integer date (e.g. 01/05/09) as the year.

    If ``True``, the first number is taken to be the year, otherwise the
    last number is taken to be the year.
    """
    return bool(val)


@standalone.cast.register_arg(default=False)
def as_hours(val: Any, defaults: dict) -> bool:
    """Indicates whether to interpret ambiguous MM:SS times as HH:MM
    instead.
    """
    return bool(val)


@standalone.cast.register_arg(default={"true", "t", "yes", "y", "on", "1"})
def true(val, defaults: dict) -> set[str]:
    """A set of truthy strings to use for boolean conversions.
    """
    # convert to string sets
    true = as_string_set(val)
    false = as_string_set(getattr(defaults, "false", set()))

    # ensure sets are disjoint
    assert_sets_are_disjoint(true, false)

    # apply ignore_case
    if bool(getattr(defaults, "ignore_case", True)):
        true = {x.lower() for x in true}
    return true


@standalone.cast.register_arg(default={"false", "f", "no", "n", "off", "0"})
def false(val, defaults: dict) -> set[str]:
    """A set of falsy strings to use for string conversions.
    """
    # convert to string sets
    true = as_string_set(getattr(defaults, "true", set()))
    false = as_string_set(val)

    # ensure sets are disjoint
    assert_sets_are_disjoint(true, false)

    # apply ignore_case
    if bool(getattr(defaults, "ignore_case", True)):
        false = {x.lower() for x in false}
    return false


@standalone.cast.register_arg(default=True)
def ignore_case(val, defaults: dict) -> bool:
    """Indicates whether to ignore differences in case during string
    conversions.
    """
    return bool(val)


@standalone.cast.register_arg(default=None)
def format(val: str | None, defaults: dict) -> str:
    """f-string formatting for conversions to strings.
    
    A `format specifier <https://docs.python.org/3/library/string.html#formatspec>`_
    to use for conversions to string.
    """
    if val is not None and not isinstance(val, str):
        raise TypeError(f"`format` must be a string, not {val}")
    return val


@standalone.cast.register_arg(default=0)
def base(val: int, defaults: dict) -> int:
    """Base to use for integer <-> string conversions.
    """
    if val != 0 and not 2 <= val <= 36:
        raise ValueError(f"`base` must be 0 or >= 2 and <= 36, not {val}")
    return val


@standalone.cast.register_arg(default=None)
def call(val: Callable | None, defaults: dict) -> Callable:
    """Apply a callable over the input data, producing the desired output.

    This is only used for conversions from :class:`ObjectType`.  It allows
    users to specify a custom endpoint to perform this conversion, rather
    than relying exclusively on special methods (which is the default).
    """
    if val is not None and not callable(val):
        raise TypeError(f"`call` must be callable, not {val}")
    return val


@standalone.cast.register_arg(default=False)
def downcast(
    val: bool | type_specifier,
    defaults: dict
) -> types.CompositeType:
    """Losslessly reduce the precision of numeric data after converting.
    """
    if isinstance(val, bool):  # empty set is truthy, `None` is falsy
        return types.CompositeType() if val else None
    return resolve.resolve_type([val])


@standalone.cast.register_arg(default="raise")
def errors(val: str, defaults: dict) -> str:
    """The rule to apply if/when errors are encountered during conversion.
    """
    if val not in valid_errors:
        raise ValueError(
            f"`errors` must be one of {valid_errors}, not {repr(val)}"
        )
    return val
