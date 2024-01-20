"""This module holds argument validators for ``cast()``.

See the API docs for :func:`@extension_func <pdcast.extension_func>` for more
details.
"""
# pylint: disable=unused-argument, anomalous-backslash-in-string
# pylint: disable=redefined-outer-name
from __future__ import annotations
import datetime
import decimal
from typing import Any, Callable, Iterable

import pandas as pd

from pdcast import types
from pdcast.detect import detect_type
from pdcast.patch.round import rule as rounding_rule
from pdcast.patch.snap import tol as snap_tol
from pdcast.resolve import resolve_type
from pdcast.util.round import Tolerance
from pdcast.util import time
from pdcast.util.type_hints import datetime_like, type_specifier
from pdcast.util.vector import as_series

from .base import cast


# TODO: include context dicts in parameter lists


# ignore this file when doing string-based object lookups in resolve_type()
IGNORE_FRAME_OBJECTS = True


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


def assert_sets_are_disjoint(set_1: set, set_2: set) -> None:
    """Raise a `ValueError` if the sets have any overlap."""
    if not set_1.isdisjoint(set_2):
        err_msg = "sets must be disjoint"

        intersection = set_1.intersection(set_2)
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


# initial defaults for all conversion arguments
defaults = {
    "tol": decimal.Decimal(1) / 10**6,
    "rounding": None,
    "unit": "ns",
    "step_size": 1,
    "since": "utc",
    "tz": None,
    "day_first": False,
    "year_first": False,
    "as_hours": False,
    "true": {"true", "t", "yes", "y", "on", "1"},
    "false": {"false", "f", "no", "n", "off", "0"},
    "ignore_case": True,
    "format": None,
    "base": 0,
    "call": None,
    "downcast": False,
    "errors": "raise"
}


@cast.argument
def series(val: Any, context: dict) -> pd.Series:
    """The series to convert.

    Parameters
    ----------
    val : Any
        An object to be wrapped as a :class:`pandas.Series <pandas.Series>`.
        If this is already a series, then it is used as-is.  If it is a
        numpy array, then it is converted to a series with the same dtype.
        Other iterables are interpreted as a ``dtype: object`` series, and
        scalars are converted into a ``dtype: object`` series with a single
        element.
    context : dict
        The current settings of each argument that was supplied to the
        conversion.  This is automatically inserted by the
        :class:`ExtensionFunc <pdcast.ExtensionFunc>` machinery, but is not
        used for this argument.

    Returns
    -------
    pandas.Series
        The input object as a series.

    Notes
    -----
    :meth:`Overloaded <pdcast.DispatchFunc.overload>` implementations of
    :func:`cast() <pdcast.cast>` should note that this argument is always
    supplied as a series, even if the original value was a scalar.
    """
    return as_series(val)


@cast.argument
def dtype(
    val: type_specifier | None,
    context: dict,
    supertype: type_specifier | None = None
) -> types.VectorType:
    """The type to convert to.

    Parameters
    ----------
    val : type_specifier | None
        A type specifier that determines the output type.  This can be in any
        format recognized by
        :func:`resolve_type() <pdcast.resolve.resolve_type>` and must not be
        composite.  If it is left as :data:`None <python:None>`, then the
        inferred type of the :func:`series <pdcast.convert.arguments.series>`
        will be used instead.
    context : dict
        The current settings of each argument that was supplied to the
        conversion.  This is automatically inserted by the
        :class:`ExtensionFunc <pdcast.ExtensionFunc>` machinery, and allows
        this argument to access the series being converted.
    supertype : type_specifier | None
        A type specifier to check the output type against.  If this is given,
        then this argument will reject any output type that is not a member of
        the given supertype's hierarchy.  This can be in any format recognized
        by :func:`resolve_type() <pdcast.resolve.resolve_type>`.

    Returns
    -------
    VectorType
        The resolved output type.

    Raises
    ------
    ValueError
        If the output type is composite or not a member of the supertype's
        hierarchy.  This can also be raised if no output type is given and the
        series is empty or contains only NAs.

    TODO: update with NullType once it is implemented

    TODO: Notes/Examples
    """
    # no dtype given
    if val is None:
        if "series" in context:
            series = context["series"]
            val = detect_type(series)
            # TODO: replace with NullType
            if val is None:  # series is empty or contains only NAs
                raise ValueError(
                    "cannot interpret empty series without an explicit "
                    "`dtype` argument"
                )

    else:
        val = resolve_type(val)

        # dtype is naked decorator
        if val.unwrap() is None:
            if "series" not in context:
                raise ValueError(
                    "cannot perform anonymous conversion without data"
                )
            encountered = list(val.decorators)
            val = encountered.pop(-1).replace(
                wrapped=detect_type(context["series"])
            )
            for decorator in reversed(encountered):
                val = decorator.replace(wrapped=val)

    # reject composite
    if isinstance(val, types.CompositeType):
        raise ValueError(
            f"`dtype` cannot be composite (received: {val})"
        )

    # reject improper subtype
    if supertype is not None and val.unwrap() is not None:
        supertype = resolve_type(supertype)
        if not supertype.contains(val.unwrap()):
            raise ValueError(
                f"`dtype` must be {supertype}-like, not {val}"
            )

    return val


@cast.argument(default=defaults["tol"])
def tol(val: str, context: dict) -> Tolerance:
    """The maximum amount of precision loss that can occur before an error
    is raised.

    Parameters
    ----------
    val : numeric
        A scalar numeric that is coercible to
        :class:`Decimal <python:decimal.Decimal>`.  In the case of complex
        values, their real and imaginary components are considered separately.
        Defaults to ``1e-6``

    Returns
    -------
    Tolerance
        A ``Tolerance`` object that consists of two
        :class:`Decimal <python:decimal.Decimal>` values, one for both the real
        and imaginary components.  This maintains the highest possible
        precision in both cases.

    Raises
    ------
    TypeError
        If ``val`` could not be coerced into a
        :class:`Decimal <python:decimal.Decimal>` representation.
    ValueError
        If the real or imaginary component of ``val`` is not positive.

    Notes
    -----
    Precision loss is defined using a 2-sided window around each of the
    observed values.  The size of this window is directly controlled by
    this argument.  If a conversion causes any value to be coerced outside
    this window, then a :class:`ValueError <python:ValueError>` will be raised.

    This argument only affects numeric conversions.

    Examples
    --------
    The input to this argument must be a positive numeric that is
    coercible to :class:`Decimal <python:decimal.Decimal>`.

    .. doctest::

        >>> cast(1.001, "int", tol=0.01)
        0    1
        dtype: int64
        >>> cast(1.001, "int", tol=0)
        Traceback (most recent call last):
            ...
        ValueError: precision loss exceeds tolerance 0 at index [0]

    If a complex value is given, then its real and imaginary components
    will be considered separately.

    .. doctest::

        >>> cast(1.001+0.001j, "int", tol=0.01+0.01j)
        0    1
        dtype: int64
        >>> cast(1.001+0.001j, "int", tol=0.01+0j)
        Traceback (most recent call last):
            ...
        ValueError: imaginary component exceeds tolerance 0 at index [0]

    This argument also has special behavior around the min/max of bounded
    numerics, like integers and booleans.  If a value would normally
    overflow, but falls within tolerance of these bounds, then it will be
    clipped to fit rather than raise an
    :class:`OverflowError <python:OverflowError>`.

    .. doctest::

        >>> cast(129, "int8", tol=2)
        0    127
        dtype: int8
        >>> cast(129, "int8", tol=0)
        Traceback (most recent call last):
            ...
        OverflowError: values exceed int8 range at index [0]

    Additionally, this argument controls the maximum amount of precision
    loss that can occur when
    :func:`downcasting <pdcast.convert.arguments.downcast>` numeric values.

    .. doctest::

        >>> cast(1.1, "float", tol=0, downcast=True)
        0    1.1
        dtype: float64
        >>> cast(1.1, "float", tol=0.001, downcast=True)
        0    1.099609
        dtype: float16

    Setting this to infinity ignores precision loss entirely.

    .. doctest::

        >>> cast(1.5, "int", tol=np.inf)
        0    2
        dtype: int64
        >>> cast(np.inf, "int64", tol=np.inf)
        0    9223372036854775807
        dtype: int64

    .. note::

        For integer conversions, this is equivalent to setting
        :func:`rounding <pdcast.convert.arguments.rounding>` to
        ``"half_even"``, with additional clipping around the minimum and
        maximum values.
    """
    return snap_tol(val, context)


@cast.argument(default=defaults["rounding"])
def rounding(val: str, context: dict) -> str:
    """The rounding rule to use for numeric conversions.

    Parameters
    ----------
    val : str | None
        An optional string specifying the rounding rule to use, or :data:`None`
        to indicate that no rounding will be applied.  Defaults to :data:`None`.

    Returns
    -------
    str | None
        A validated version of the string passed to ``val`` or :data:`None`.

    Raises
    ------
    TypeError
        If ``val`` is not a string or :data:`None <python:None>`.
    ValueError
        If ``val`` does not correspond to one of the recognized rounding rules.

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

    This argument is applied **after**
    :func:`tol <pdcast.convert.arguments.tol>`.

    Examples
    --------
    .. doctest::

        >>> cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="floor")
        0   -2
        1   -1
        2    0
        3    1
        dtype: int64
        >>> cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="ceiling")
        0   -1
        1    0
        2    1
        3    2
        dtype: int64
        >>> cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="down")
        0   -1
        1    0
        2    0
        3    1
        dtype: int64
        >>> cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="up")
        0   -2
        1   -1
        2    1
        3    2
        dtype: int64
        >>> cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_floor")
        0   -2
        1   -1
        2    0
        3    2
        dtype: int64
        >>> cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_ceiling")
        0   -1
        1    0
        2    0
        3    2
        dtype: int64
        >>> cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_down")
        0   -1
        1    0
        2    0
        3    2
        dtype: int64
        >>> cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_up")
        0   -2
        1   -1
        2    0
        3    2
        dtype: int64
        >>> cast([-1.5, -0.5, 0.2, 1.7], "int", rounding="half_even")
        0   -2
        1    0
        2    0
        3    2
        dtype: int64
    """
    return rounding_rule(val, context)


@cast.argument(default=defaults["unit"])
def unit(val: str, context: dict) -> str:
    """The unit to use for numeric <-> datetime/timedelta conversions.

    Parameters
    ----------
    val : str
        A string specifying the unit to use during conversions.  Defaults to
        ``"ns"``.

    Returns
    -------
    str
        A validated version of the string passed to ``val``.

    Raises
    ------
    TypeError
        If `val` is not a string.
    ValueError
        If ``val`` does not correspond to one of the recognized units.

    See Also
    --------
    step_size <pdcast.convert.arguments.step_size> :
        The step size to use for each unit.

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

        >>> cast(1, "datetime", unit="ns")
        0   1970-01-01 00:00:00.000000001
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="us")
        0   1970-01-01 00:00:00.000001
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="ms")
        0   1970-01-01 00:00:00.001
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="s")
        0   1970-01-01 00:00:01
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="m")
        0   1970-01-01 00:01:00
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="h")
        0   1970-01-01 01:00:00
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="D")
        0   1970-01-02
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="W")
        0   1970-01-08
        dtype: datetime64[ns]

    Units ``"M"`` and ``"Y"`` have irregular lengths.  Rather than average
    these like :func:`pandas.to_datetime`, :func:`cast() <pdcast.cast>` gives
    calendar-accurate results.

    .. doctest::

        >>> cast(1, "datetime", unit="M")
        0   1970-02-01
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="Y")
        0   1971-01-01
        dtype: datetime64[ns]

    This accounts for leap years as well, following the `Gregorian calendar
    <https://en.wikipedia.org/wiki/Gregorian_calendar>`_.

    .. doctest::

        >>> cast(1, "datetime", unit="M", since="1972-02")
        0   1972-03-01
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="Y", since="1972")
        0   1973-01-01
        dtype: datetime64[ns]
    """
    if not isinstance(val, str):
        raise TypeError(f"`unit` must be a string, not {repr(val)}")
    if val not in time.valid_units:
        raise ValueError(
            f"`unit` must be one of {time.valid_units}, not {repr(val)}"
        )
    return val


@cast.argument(default=defaults["step_size"])
def step_size(val: int, context: dict) -> int:
    """The step size to use for each
    :func:`unit <pdcast.convert.arguments.unit>`.

    Parameters
    ----------
    val : int
        A positive integer >= 1.  This is effectively a multiplier for
        :func:`unit <pdcast.convert.arguments.unit>`.  Defaults to ``1``.

    Returns
    -------
    int
        A validated version of the integer passed to ``val``. 

    Raises
    ------
    TypeError
        If ``val`` is not an integer.
    ValueError
        If ``val`` is not >= 1.

    See Also
    --------
    unit <pdcast.convert.arguments.unit> :
        The unit to use for the conversion.

    Examples
    --------
    .. doctest::

        >>> cast(1, "datetime", unit="ns", step_size=5)
        0   1970-01-01 00:00:00.000000005
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="s", step_size=30)
        0   1970-01-01 00:00:30
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="M", step_size=3)
        0   1970-04-01
        dtype: datetime64[ns]
    """
    if not isinstance(val, int):
        raise TypeError(f"`step_size` must be an integer, not {val}")
    if val < 1:
        raise ValueError(f"`step_size` must be >= 1, not {val}")
    return val


@cast.argument(default=defaults["since"])
def since(val: str | datetime_like | time.Epoch, context: dict) -> time.Epoch:
    """The epoch to use for datetime/timedelta conversions.

    Parameters
    ----------
    val : str | datetime-like
        A datetime string, datetime object, or special epoch specifier as shown
        below.  Defaults to ``"utc"``.

    Returns
    -------
    Epoch
        An :class:`Epoch` object that represents a nanosecond offset from the
        UTC epoch (1970-01-01 00:00:00).

    Raises
    ------
    TypeError
        If ``val`` could not be interpreted as an :class:`Epoch`.

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
            system.  Can also be referred to through the ``"risc"`` alias.
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

        >>> cast(1, "datetime", unit="s", since="j2000")
        0   2000-01-01 12:00:01
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="s", since="gregorian")
        0    1582-10-14 00:00:01
        dtype: datetime[python]
        >>> cast(1, "datetime", unit="s", since="julian")
        0    -4713-11-24T12:00:01.000000
        dtype: object

    Using datetime strings:

    .. doctest::

        >>> cast(1, "datetime", unit="s", since="2022-03-27")
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="s", since="27 mar 2022")
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="s", since="03/27/22")
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]

    Using datetime objects:

    .. doctest::

        >>> cast(1, "datetime", unit="s", since=pd.Timestamp("2022-03-27"))
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="s", since=datetime.datetime(2022, 3, 27))
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]
        >>> cast(1, "datetime", unit="s", since=np.datetime64("2022-03-27"))
        0   2022-03-27 00:00:01
        dtype: datetime64[ns]
    """
    if isinstance(val, time.Epoch):
        return val

    if isinstance(val, str) and val not in time.epoch_aliases:
        val = cast(val, "datetime")
        if len(val) != 1:
            raise ValueError("`since` must be scalar")
        val = val[0]

    try:
        return time.Epoch(val)
    except Exception as err:
        raise TypeError(f"`since` must be datetime-like: {val}") from err


@cast.argument(default=defaults["tz"])
def tz(
    val: str | datetime.tzinfo | None,
    context: dict
) -> datetime.tzinfo:
    """Specifies a time zone to use for datetime conversions.

    Parameters
    ----------
    val : str | pytz.timezone | None
        An IANA time zone string, a `pytz <https://pypi.org/project/pytz/>`_
        timezone object, or :data:`None <python:None>` to indicate naive
        output.  This can also be the special string ``"local"``, which refers
        to the local time zone of the current system at the time of execution.
        Defaults to :data:`None <python:None>`.

    Returns
    -------
    pytz.timezone | None
        A `pytz <https://pypi.org/project/pytz/>`_ timezone object
        corresponding to the input.  :data:`None` indicates naive output.
        Defaults to :data:`None`.

    Raises
    ------
    pytz.exceptions.UnknownTimeZoneError
        If ``val`` could not be recognized as a time zone specifier.

    Examples
    --------
    Time zone localization is a somewhat complicated process, with
    different behavior depending on the input data type.

    Numerics (:doc:`boolean </content/types/boolean>`,
    :doc:`integer </content/types/integer>`,
    :doc:`float </content/types/float>`,
    :doc:`complex </content/types/complex>`, and
    :doc:`decimal </content/types/decimal>`) as well as
    :doc:`timedeltas </content/types/timedelta>` are always computed in UTC
    relative to the :func:`since <pdcast.convert.arguments.since>` argument.
    When a time zone is supplied via :func:`tz <pdcast.convert.arguments.tz>`,
    the resulting datetimes will be *converted* from UTC to the specified time
    zone.

    .. doctest::

        >>> cast(0, "datetime", tz="US/Pacific")
        0   1969-12-31 16:00:00-08:00
        dtype: datetime64[ns, US/Pacific]
        >>> cast(0, "datetime", since="2022-03-27", tz="Asia/Hong_Kong")
        0   2022-03-27 08:00:00+08:00
        dtype: datetime64[ns, Asia/Hong_Kong]
        >>> cast(0, "datetime", since="2022-03-27 00:00:00+0800", tz="Asia/Hong_Kong")
        0   2022-03-27 00:00:00+08:00
        dtype: datetime64[ns, Asia/Hong_Kong]

    :doc:`Strings </content/types/string>` and
    :doc:`datetimes </content/types/datetime>`, on the other hand, will be
    *localized* directly to the final :func:`tz <pdcast.convert.arguments.tz>`
    if they are naive, or *converted* to it if they are timezone-aware.

    .. doctest::

        >>> cast(pd.Timestamp(0), "datetime", tz="US/Pacific")
        0   1970-01-01 00:00:00-08:00
        dtype: datetime64[ns, US/Pacific]
        >>> cast(pd.Timestamp(0, tz="UTC"), "datetime", tz="US/Pacific")
        0   1969-12-31 16:00:00-08:00
        dtype: datetime64[ns, US/Pacific]
        >>> cast("2022-03-27", "datetime", tz="Asia/Hong_Kong")
        0   2022-03-27 00:00:00+08:00
        dtype: datetime64[ns, Asia/Hong_Kong]
        >>> cast("2022-03-27 00:00:00+0000", "datetime", tz="Asia/Hong_Kong")
        0   2022-03-27 08:00:00+08:00
        dtype: datetime64[ns, Asia/Hong_Kong]

    The same behavior as for numerics can be obtained by setting
    :func:`tz <pdcast.convert.arguments.tz>` to ``"utc"`` and adding an extra
    :meth:`tz_convert() <pandas.Series.dt.tz_convert>` step, as follows:

    .. doctest::

        >>> cast(pd.Timestamp(0), "datetime", tz="utc").dt.tz_convert("US/Pacific")
        0   1969-12-31 16:00:00-08:00
        dtype: datetime64[ns, US/Pacific]
        >>> cast("2022-03-27", "datetime", tz="utc").dt.tz_convert("Asia/Hong_Kong")
        0   2022-03-27 08:00:00+08:00
        dtype: datetime64[ns, Asia/Hong_Kong]

    .. note::

        In this sense, :func:`tz <pdcast.convert.arguments.tz>` is similar to
        the ``utc`` argument of :func:`pandas.to_datetime`, but allows for full
        control over the handling of naive inputs.
    """
    return time.tz(val, context)


@cast.argument(default=defaults["day_first"])
def day_first(val: bool, context: dict) -> bool:
    """Indicates whether to interpret the first value in an ambiguous
    3-integer date (e.g. 01/05/09) as the day (``True``) or month
    (``False``).

    Parameters
    ----------
    val : bool
        A boolean (or boolean-like) value indicating the rule to apply for
        ambiguous string dates.  Defaults to ``False``.

    Returns
    -------
    bool
        The boolean equivalent of the input.

    See Also
    --------
    year_first <pdcast.convert.arguments.year_first> :
        The year complement of this argument.

    Notes
    -----
    This argument is equivalent to
    `dateutil's <https://dateutil.readthedocs.io/en/stable>`_
    :class:`dayfirst <dateutil.parser.parserinfo>` argument.

    Examples
    --------
    By default, dateutil parses ambiguous strings in MDY (American) format.

    .. doctest::

        >>> cast("01/05/09", "datetime")
        0   2009-01-05
        dtype: datetime64[ns]

    Setting this argument to ``True`` changes this to DMY (international)
    format.

    .. doctest::

        >>> cast("01/05/09", "datetime", day_first=True)
        0   2009-05-01
        dtype: datetime64[ns]

    If :func:`year_first <pdcast.convert.arguments.year_first>` is set to
    ``True``, then this argument distinguishes between YMD and YDM.

    .. doctest::

        >>> cast("01/05/09", "datetime", day_first=False, year_first=True)
        0   2001-05-09
        dtype: datetime64[ns]
        >>> cast("01/05/09", "datetime", day_first=True, year_first=True)
        0   2001-09-05
        dtype: datetime64[ns]
    """
    return bool(val)


@cast.argument(default=defaults["year_first"])
def year_first(val: bool, context: dict) -> bool:
    """Indicates whether to interpret the first value in an ambiguous
    3-integer date (e.g. 01/05/09) as the year.

    Parameters
    ----------
    val : bool
        A boolean (or boolean-like) value indicating the rule to apply for
        ambiguous string dates.  Defaults to ``False``.

    Returns
    -------
    bool
        The boolean equivalent of the input.

    See Also
    --------
    day_first <pdcast.convert.arguments.day_first> :
        The day complement of this argument.

    Notes
    -----
    This argument is equivalent to
    `dateutil's <https://dateutil.readthedocs.io/en/stable>`_
    :class:`yearfirst <dateutil.parser.parserinfo>` argument.

    Examples
    --------
    By default, dateutil interprets the last number in an ambiguous date string
    to be the year.

    .. doctest::

        >>> cast("01/05/09", "datetime")
        0   2009-01-05
        dtype: datetime64[ns]

    Setting this to ``True`` takes the first number as the year.

    .. doctest::

        >>> cast("01/05/09", "datetime", year_first=True)
        0   2001-05-09
        dtype: datetime64[ns]

    See the :func:`day_first <pdcast.convert.arguments.day_first>` argument
    for examples on how these arguments interact.
    """
    return bool(val)


@cast.argument(default=defaults["as_hours"])
def as_hours(val: bool, context: dict) -> bool:
    """Indicates whether to interpret ambiguous MM:SS timedeltas as HH:MM.

    Parameters
    ----------
    val : bool
        A boolean (or boolean-like) value indicating the rule to apply for
        ambiguous string timedeltas.  Defaults to ``False``.

    Returns
    -------
    bool
        The boolean equivalent of the input.

    Examples
    --------
    :func:`cast() <pdcast.cast>` supports a variety of timedelta string formats,
    including those given as HH:MM:SS.  Oftentimes, these strings are provided
    without a third component, which causes them to become ambiguous.  By
    default, :func:`cast() <pdcast.cast>` interprets them as minutes and seconds.

    .. doctest::

        >>> cast("1:22", "timedelta")
        0   0 days 00:01:22
        dtype: timedelta64[ns]

    Setting this argument to ``True`` changes this assumption, taking them
    to be hours and minutes instead.

    .. doctest::

        >>> cast("1:22", "timedelta", as_hours=True)
        0   0 days 01:22:00
        dtype: timedelta64[ns]
    """
    return bool(val)


@cast.argument(default=defaults["true"])
def true(val: str | Iterable[str] | None, context: dict) -> set[str]:
    """A set of truthy strings to use for boolean conversions.

    Parameters
    ----------
    val : str | Iterable[str] | None
        A string, sequence of strings, or :data:`None <python:None>`, to be
        converted into a set.  :data:`None` indicates an empty set, and scalar
        strings are converted into sets of length 1.  This can also include the
        special character ``"*"`` as a catch-all wildcard.  Defaults to
        ``{"true", "t", "yes", "y", "on", "1"}``.
    context : dict
        A dictionary containing the values of other arguments, for comparison
        with :func:`false <pdcast.convert.arguments.false>`.

    Returns
    -------
    set[str]
        A set of strings to consider ``True`` during boolean conversions. The
        returned set must be disjoint with
        :func:`false <pdcast.convert.arguments.false>`, and any strings that
        are not contained in either set will raise an error.

    Raises
    ------
    TypeError
        If ``val`` does not contain strings.
    ValueError
        If one or more elements of the set intersect with
        :func:`false <pdcast.convert.arguments.false>`.

    See Also
    --------
    false <pdcast.convert.arguments.false> :
        the falsy equivalent of this argument.
    ignore_case <pdcast.convert.arguments.ignore_case> :
        whether to ignore differences of case during boolean comparisons.

    Examples
    --------
    Converting between strings and booleans is a bit unintuitive thanks to
    Python's rules around :ref:`truth value testing <python:truth>`.  Since
    strings are counted as sequences, only empty strings will ever explicitly
    evaluate to ``False``.

    .. doctest::

        >>> bool("")
        False
        >>> bool("False")
        True

    As a result, we have to implement a more complicated string parsing
    algorithm to recognize boolean values.  In ``pdcast``, this is accomplished
    through the :func:`true <pdcast.convert.arguments.true>` and
    :func:`false <pdcast.convert.arguments.false>` arguments, which define the
    strings to consider as ``True`` and ``False``, respectively.

    By default, :func:`cast() <pdcast.cast>` considers the following strings to
    be boolean-like (:func:`ignoring <pdcast.convert.arguments.ignore_case>`
    case):

    .. doctest::

        >>> cast(["true", "t", "yes", "y", "on", "1"], "bool")
        0    True
        1    True
        2    True
        3    True
        4    True
        5    True
        dtype: bool
        >>> cast(["false", "f", "no", "n", "off", "0"], "bool")
        0    False
        1    False
        2    False
        3    False
        4    False
        5    False
        dtype: bool

    Any string that falls outside these sets will raise a
    :class:`ValueError <python:ValueError>`.

    .. doctest::

        >>> cast("abc", "bool")
        Traceback (most recent call last):
            ...
        ValueError: encountered non-boolean value: 'abc'

    We can change this behavior by adding the offending string to one of
    either :func:`true <pdcast.convert.arguments.true>` or
    :func:`false <pdcast.convert.arguments.false>`.

    .. doctest::

        >>> cast("abc", "bool", true=pdcast.cast.true | {"abc"})
        0    True
        dtype: bool

    Or we can coerce it into a missing value by setting
    :func:`errors <pdcast.convert.arguments.errors>` to ``"coerce"``.

    .. doctest::

        >>> cast("abc", "bool", errors="coerce")
        0    <NA>
        dtype: boolean

    Additionally, both sets support the special value ``"*"``, which acts as a
    wildcard, matching any string that is not found in either set.

    .. doctest::

        >>> cast("abc", "bool", false="*")
        0    False
        dtype: bool

    This can be used together with an explicit empty string to replicate the
    behavior of Python's :class:`bool() <python:bool>` function.

    .. doctest::

        >>> cast(["False", "", "abc"], "bool", true="*", false="")
        0     True
        1    False
        2     True
        dtype: bool
    """
    # convert to string sets
    true_set = set() if val is None else as_string_set(val)
    false_set = as_string_set(context.get("false", set()))

    # ensure sets are disjoint
    try:
        assert_sets_are_disjoint(true_set, false_set)
    except ValueError as err:
        raise ValueError("`true` and `false` must be disjoint") from err

    # apply ignore_case
    if bool(context.get("ignore_case", True)):
        true_set = {x.lower() for x in true_set}
    return true_set


@cast.argument(default=defaults["false"])
def false(val, context: dict) -> set[str]:
    """A set of falsy strings to use for boolean conversions.

    Parameters
    ----------
    val : str | Iterable[str] | None
        A string, sequence of strings, or :data:`None <python:None>`, to be
        converted into a set.  :data:`None` indicates an empty set, and scalar
        strings are converted into sets of length 1.  This can also include the
        special character ``"*"`` as a catch-all wildcard.  Defaults to
        ``{"false", "f", "no", "n", "off", "0"}``.
    context : dict
        A dictionary containing the values of other arguments, for comparison
        with :func:`true <pdcast.convert.arguments.true>`.

    Returns
    -------
    set[str]
        A set of strings to consider ``False`` during boolean conversions. The
        returned set must be disjoint with
        :func:`true <pdcast.convert.arguments.true>`, and any strings that
        are not contained in either set will raise an error.

    Raises
    ------
    TypeError
        If ``val`` does not contain strings.
    ValueError
        If one or more elements of the set intersect with
        :func:`true <pdcast.convert.arguments.true>`.

    See Also
    --------
    true <pdcast.convert.arguments.true> :
        the truthy equivalent of this argument.
    ignore_case <pdcast.convert.arguments.ignore_case> :
        whether to ignore differences of case during boolean comparisons.

    Examples
    --------
    See the docs for the :func:`true <pdcast.convert.arguments.true>` argument
    for examples on how to customize boolean conversions using these arguments.
    """
    # convert to string sets
    true_set = as_string_set(context.get("true", set()))
    false_set = set() if val is None else as_string_set(val)

    # ensure sets are disjoint
    try:
        assert_sets_are_disjoint(true_set, false_set)
    except ValueError as err:
        raise ValueError("`true` and `false` must be disjoint") from err

    # apply ignore_case
    if bool(context.get("ignore_case", True)):
        false_set = {x.lower() for x in false_set}
    return false_set


@cast.argument(default=defaults["ignore_case"])
def ignore_case(val: bool, context: dict) -> bool:
    """Indicates whether to ignore differences in case during string
    conversions.

    Parameters
    ----------
    val : bool
        A boolean (or boolean-like) value indicating the rule to apply for
        string case comparisons.  Defaults to ``True``.

    Returns
    -------
    bool
        The boolean equivalent of the input.

    See Also
    --------
    :func:`true <pdcast.convert.arguments.true>` :
        uses this argument to compare for boolean ``True`` strings.
    :func:`false <pdcast.convert.arguments.false>` :
        uses this argument to compare for boolean ``False`` strings.

    Examples
    --------
    By default, this only applies to comparisons against the sets provided in
    the :func:`true <pdcast.convert.arguments.true>` and
    :func:`true <pdcast.convert.arguments.true>` arguments, but it can
    also be intercepted by other conversions in
    :ref:`extensions <tutorial.conversions>` to the ``pdcast`` type system.

    .. doctest::

        >>> cast("True", "bool", ignore_case=True)
        0    True
        dtype: bool
        >>> cast("True", "bool", ignore_case=False)
        Traceback (most recent call last):
            ...
        ValueError: encountered non-boolean value: 'True'
    """
    return bool(val)


@cast.argument(default=defaults["format"])
def format(val: str | None, context: dict) -> str:
    """A :ref:`format specifier <python:formatspec>` to use for string
    conversions.

    Parameters
    ----------
    val : str | None
        An optional :ref:`format specification <python:formatspec>` string,
        similar to those used in `f-string <https://peps.python.org/pep-0498/>`_
        or :ref:`strptime <python:strftime-strptime-behavior>` formatting.
        Defaults to :data:`None <python:None>`.

    Returns
    -------
    str | None
        A validated version of the string passed to ``val``.

    Raises
    ------
    TypeError
        If the passed value is not a string.

    See Also
    --------
    base <pdcast.convert.arguments.base> :
        mathematical bases for string <-> integer conversions.

    Examples
    --------
    This argument can be used to provide `f-string
    <https://peps.python.org/pep-0498/>`_ format codes to conversions, for
    instance by aligning/padding results, adding commas for large numbers,
    or changing the representation of fractional numerics.

    .. doctest::

        >>> cast("Hello, World!", "string", format="_^24")
        0    _____Hello, World!______
        dtype: string
        >>> cast(1000000, "string", format=",")
        0    1,000,000
        dtype: string
        >>> cast(1 / 3, "string", format=".2e")
        0    3.33e-01
        dtype: string

    It can also be used to provide explicit datetime formats for
    :meth:`strptime() <python:datetime.datetime.strptime>`.

    .. doctest::

        >>> cast("7:00 AM 01/05/09", "datetime", format="%I:%M %p %d/%m/%y")
        0   2009-05-01 07:00:00
        dtype: datetime64[ns]
    """
    if val is not None and not isinstance(val, str):
        raise TypeError(f"`format` must be a string, not {val}")
    return val


@cast.argument(default=defaults["base"])
def base(val: int, context: dict) -> int:
    """Base to use for integer <-> string conversions, as supplied to
    :class:`int() <python:int>`.

    Parameters
    ----------
    val : int
        An integer representing the base to use when translating between
        string and integer formats.  This must be either 0 or between 2 and
        36, equal to the allowable bases for the :class:`int() <python:int>`
        function.  Defaults to ``0``.

    Returns
    -------
    int
        The validated equivalent of ``val``.

    Raises
    ------
    TypeError
        If ``val`` is not an integer.
    ValueError
        If ``val`` is not 0 or between 2 and 36 (inclusive).

    See Also
    --------
    format <pdcast.convert.arguments.format> :
        :ref:`format specification <python:formatspec>` for string conversions.

    Notes
    -----
    For base 0, strings are interpreted in a way similar to an
    :ref:`integer literal <python:integers>` in code, in that the actual base
    is 2, 8, 10, or 16 as determined by the prefix.  Base 0 also disallows
    leading zeros: ``int('010', 0)`` is not legal, while ``int('010')`` and
    ``int('010', 8)`` are.

    Examples
    --------
    By default, this argument will automatically parse binary, octal, or
    hexadecimal strings with the appropriate prefix.

    .. doctest::

        >>> cast("0b101", "int")
        0    5
        dtype: int64
        >>> cast("0o77", "int")
        0    63
        dtype: int64
        >>> cast("0x3FF", "int")
        0    1023
        dtype: int64

    It can also be explicitly set to deal with strings that have no prefix,
    or are in a non-standard base.

    .. doctest::

        >>> cast("101", "int", base=2)
        0    5
        dtype: int64
        >>> cast("77", "int", base=8)
        0    63
        dtype: int64
        >>> cast("3FF", "int", base=16)
        0    1023
        dtype: int64
        >>> cast("abc", "int", base=36)
        0    13368
        dtype: int64

    This argument also controls the reverse conversion, from integer to string.

    .. doctest::

        >>> cast(5, "string", base=2)
        0    101
        dtype: string
        >>> cast(63, "string", base=8)
        0    77
        dtype: string
        >>> cast(1023, "string", base=16)
        0    3FF
        dtype: string
        >>> cast(13368, "string", base=36)
        0    ABC
        dtype: string
    """
    if not isinstance(val, int):
        raise TypeError(f"`base` must be an integer, not {repr(val)}")
    if val != 0 and not 2 <= val <= 36:
        raise ValueError(
            f"`base` must be 0 or >= 2 and <= 36, not {repr(val)}"
        )
    return val


@cast.argument(default=defaults["call"])
def call(val: Callable | None, context: dict) -> Callable:
    """Apply a callable over the input data, producing the desired output.

    This is only used for conversions from
    :class:`ObjectType <pdcast.ObjectType>`.  It allows users to specify a
    custom endpoint to perform this conversion, rather than relying exclusively
    on special methods (which is the default).
    
    TODO
    """
    if val is not None and not callable(val):
        raise TypeError(f"`call` must be callable, not {val}")
    return val


@cast.argument(default=defaults["downcast"])
def downcast(
    val: bool | type_specifier,
    context: dict
) -> types.CompositeType:
    """Losslessly reduce the precision of numeric data after converting.
    
    TODO
    """
    if val is None:
        return val
    if isinstance(val, bool):  # empty set is truthy, `None` is falsy
        return types.CompositeType() if val else None
    return resolve_type([val])


@cast.argument(default=defaults["errors"])
def errors(val: str, context: dict) -> str:
    """The rule to apply if/when errors are encountered during conversion.
    
    TODO
    """
    if val not in valid_errors:
        raise ValueError(
            f"`errors` must be one of {valid_errors}, not {repr(val)}"
        )
    return val
