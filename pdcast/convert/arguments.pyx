from functools import wraps
import threading
from types import MappingProxyType
from typing import Any, Callable

import pytz

import pdcast.convert.standalone as standalone
import pdcast.resolve as resolve
import pdcast.types as types

from pdcast.util.round import Tolerance, valid_rules
from pdcast.util.time import timezone, valid_units, Epoch, epoch_aliases
from pdcast.util.type_hints import numeric, datetime_like, type_specifier


# TODO: enable CastDefaults to be used as a context manager?
# -> requires a module-level ``active`` variable that standalone uses rather
# than the global defaults.  This is initialized to the global equivalent
# and is replaced in __enter__.  __exit__ replaces the original.
# -> benefits might not outweigh costs.  Would probably need to cache thread
# ids to make it fully thread-safe.


######################
####    PUBLIC    ####
######################


defaults = None


class CastDefaults(threading.local):
    """A thread-local configuration object containing default values for
    :func:`cast` operations.

    This object allows users to globally modify the default arguments for
    :func:`cast`\-related functionality.  It also performs some basic
    validation for each argument, ensuring that they are compatible.

    Parameters
    ----------
    **kwargs : dict
        Keyword args representing settings for any number of
        :meth:`registered <CastDefaults.register_argument>` arguments.  These
        can be used to selectively override their default values.

    Examples
    --------
    ``pdcast`` exposes a global :class:`CastDefaults` object under
    :attr:`pdcast.defaults`, which can be used to modify the behavior of
    the :func:`cast` function on a global basis.

    .. doctest::

        >>> pdcast.cast(1, "datetime")
        0   1970-01-01 00:00:00.000000001
        dtype: datetime64[ns]
        >>> pdcast.defaults.unit = "s"
        >>> pdcast.defaults.tz = "us/pacific"
        >>> pdcast.cast(1, "datetime")
        0   1969-12-31 16:00:01-08:00
        dtype: datetime64[ns, US/Pacific]
        >>> del pdcast.defaults.unit, pdcast.defaults.tz
        >>> pdcast.cast(1, "datetime")
        0   1970-01-01 00:00:00.000000001
        dtype: datetime64[ns]

    Additionally, new arguments can be added at run time by calling
    :meth:`CastDefaults.register_arg`.

    .. doctest::

        >>> @pdcast.defaults.register_arg("bar")
        ... def foo(val: str, defaults: pdcast.CastDefaults) -> str:
        ...     '''docstring for `foo`.''' 
        ...     if val not in ("bar", "baz"):
        ...         raise ValueError(f"`foo` must be one of ('bar', 'baz')")
        ...     return val

    From then on, all :func:`cast`\-related operations will accept an optional
    ``foo`` argument, as shown:

    .. doctest::

        >>> pdcast.cast(1, "datetime", foo="baz")
        0   1970-01-01 00:00:00.000000001
        dtype: datetime64[ns]

    The decorated function acts as a validator, ensuring that the input to
    ``foo`` is correct.

    .. doctest::

        >>> pdcast.cast(1, "datetime", foo="some other value")
        Traceback (most recent call last):
            ...
        ValueError: `foo` must be one of ('bar', 'baz')
        >>> pdcast.defaults.foo = "another value"
        Traceback (most recent call last):
            ...
        ValueError: `foo` must be one of ('bar', 'baz')
    """

    _defaults = {}
    _validators = {}

    def __init__(self, **kwargs):
        global defaults

        if defaults is not None:
            kwargs = {**kwargs, **defaults._vals}

        self._vals = kwargs  # prevent race conditions in getters
        self._vals = {
            k: self._validators[k](v, self) for k, v in kwargs.items()
        }

    ####################
    ####    BASE    ####
    ####################

    @property
    def default_values(self) -> MappingProxyType:
        """A mapping of all argument names to their associated values for this
        :class:`CastDefaults` object.

        Returns
        -------
        MappingProxyType
            A read-only dictionary suitable for use as the ``**kwargs`` input
            to :func:`cast` and its :ref:`related <cast.stand_alone>`
            functions.

        Examples
        --------
        .. doctest::

            >>> from pprint import pprint
            >>> pprint(pdcast.defaults.default_values)
            mappingproxy({'as_hours': False,
              'base': 0,
              'call': None,
              'day_first': False,
              'downcast': None,
              'errors': 'raise',
              'false': {'false', 'no', 'off', 'n', 'f', '0'},
              'format': None,
              'ignore_case': True,
              'naive_tz': None,
              'rounding': None,
              'since': 1970-01-01T00:00:00,
              'step_size': 1,
              'tol': Tolerance(9.99999999999999954748111825886258685613938723690807819366455078125E-7+9.99999999999999954748111825886258685613938723690807819366455078125E-7j),
              'true': {'y', 'true', 'on', 't', 'yes', '1'},
              'tz': None,
              'unit': 'ns',
              'year_first': False})
        """
        result = {k: getattr(self, k) for k in type(self)._defaults}
        return MappingProxyType(result)

    def register_arg(self, default_value: Any) -> Callable:
        """A decorator that transforms a naked validation function into a
        default argument to :func:`cast` and its derivatives.

        Parameters
        ----------
        default_value : Any
            The default value to use for this argument.  This is implicitly
            passed to the validator itself, so any custom parsing logic that
            is implemented there will also be applied to this value.

        Returns
        -------
        Callable
            A decorated version of the validation function that automatically
            fills out its ``defaults`` argument.

        Notes
        -----
        A validation function must have the following signature:

        .. code:: python

            def validator(val, defaults):
                ...

        Where ``val`` can be an arbitrary input to the argument and
        ``defaults`` is a :class:`CastDefaults` object containing the current
        parameter space.  If the validator interacts with other arguments
        (via mutual exclusivity, for instance), then they can be obtained from
        ``defaults``.

        .. note::

            Race conditions may be introduced when arguments access each other
            in their validators.  This can be mitigated by using ``getattr()``
            with a default value rather than relying on direct access, as well
            as manually applying the same coercions as in the referenced
            argument's validation function.

        Examples
        --------
        Occasionally, when a new data type is introduced to ``pdcast``, that
        type's conversions will require additional arguments beyond what is
        included by default.  :meth:`CastDefaults.register_arg` allows users to
        easily integrate these new arguments.

        .. code:: python

            @pdcast.defaults.register_arg("bar")
            def foo(val: str, defaults: pdcast.CastDefaults) -> str:
                '''docstring for `foo`.''' 
                if val not in ("bar", "baz"):
                    raise ValueError(f"`foo` must be one of ('bar', 'baz')")
                return val

        This allows the type's :ref:`delegated <atomic_type.conversions>`
        conversion methods to access ``foo`` simply by adding it to their call
        signature.

        .. code:: python

            def to_integer(
                self,
                series: pdcast.SeriesWrapper,
                dtype: pdcast.AtomicType,
                foo: str,
                **unused
            ) -> pdcast.SeriesWrapper:
                ...

        :class:`CastDefaults` ensures that ``foo`` is always passed to the
        conversion method, either with the default value specified in
        :meth:`register_arg <CastDefaults.register_arg>` or an overridden
        value in :class:`pdcast.defaults <CastDefaults>` or the signature of
        :func:`cast` itself.
        """
        def argument(validator: Callable) -> Callable:
            """Attach a validation function to CastDefaults objects as a
            managed property.
            """
            # use name of validation function as argument name
            name = validator.__name__
            if name in type(self)._defaults:
                raise KeyError(f"argument '{name}' already exists.")

            # compute and validate default value
            _default_value = validator(default_value, defaults=self)
            type(self)._defaults[name] = _default_value

            # generate getter, setter, and deleter attributes for @property
            def getter(self) -> Any:
                return self._vals.get(name, type(self)._defaults[name])
            def setter(self, val: Any) -> None:
                self._vals[name] = validator(val, defaults=self)
            def deleter(self) -> None:
                validator(self._defaults[name], defaults=self)
                del self._vals[name]

            # attach @property to global CastDefaults
            prop = property(
                getter,
                setter,
                deleter,
                doc=validator.__doc__
            )
            setattr(CastDefaults, name, prop)

            global defaults

            # wrap validator to accept default arguments
            @wraps(validator)
            def accept_default(val, defaults: CastDefaults = defaults):
                return validator(val, defaults=defaults)

            # make decorated validator available from CastDefaults and return
            type(self)._validators[name] = accept_default
            return accept_default

        return argument

    @property
    def validators(self) -> MappingProxyType:
        """TODO"""
        return MappingProxyType(self._validators)

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __iter__(self):
        return iter(self.default_values)

    def __len__(self) -> int:
        return len(self.default_values)

    def __getitem__(self, key) -> Any:
        return getattr(self, key)

    def __setitem__(self, key, val) -> None:
        setattr(self, key, val)

    def __repr__(self) -> str:
        return f"{type(self).__name__}({self.default_values})"


defaults = CastDefaults()


#######################
####    PRIVATE    ####
#######################


cdef tuple valid_errors = ("raise", "coerce", "ignore")


cdef set as_string_set(object val):
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


cdef int assert_sets_are_disjoint(true: set, false: set) except -1:
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


@defaults.register_arg(1e-6)
def tol(val: numeric, defaults: CastDefaults) -> Tolerance:
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


@defaults.register_arg(None)
def rounding(val: str | None, defaults: CastDefaults) -> str:
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


@defaults.register_arg("ns")
def unit(val: str, defaults: CastDefaults) -> str:
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


@defaults.register_arg(1)
def step_size(val: int, defaults: CastDefaults) -> int:
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


@defaults.register_arg("utc")
def since(val: str | datetime_like, defaults: CastDefaults) -> Epoch:
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


@defaults.register_arg(None)
def tz(
    val: str | pytz.BaseTzInfo | None,
    defaults: CastDefaults
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


@defaults.register_arg(None)
def naive_tz(
    val: str | pytz.BaseTzInfo | None,
    defaults: CastDefaults
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


@defaults.register_arg(False)
def day_first(val: Any, defaults: CastDefaults) -> bool:
    """Indicates whether to interpret the first value in an ambiguous
    3-integer date (e.g. 01/05/09) as the day (``True``) or month
    (``False``).

    If year_first is set to ``True``, this distinguishes between YDM and
    YMD.
    """
    return bool(val)


@defaults.register_arg(False)
def year_first(val: Any, defaults: CastDefaults) -> bool:
    """Indicates whether to interpret the first value in an ambiguous
    3-integer date (e.g. 01/05/09) as the year.

    If ``True``, the first number is taken to be the year, otherwise the
    last number is taken to be the year.
    """
    return bool(val)


@defaults.register_arg(False)
def as_hours(val: Any, defaults: CastDefaults) -> bool:
    """Indicates whether to interpret ambiguous MM:SS times as HH:MM
    instead.
    """
    return bool(val)


@defaults.register_arg({"true", "t", "yes", "y", "on", "1"})
def true(val, defaults: CastDefaults) -> set[str]:
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


@defaults.register_arg({"false", "f", "no", "n", "off", "0"})
def false(val, defaults: CastDefaults) -> set[str]:
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


@defaults.register_arg(True)
def ignore_case(val, defaults: CastDefaults) -> bool:
    """Indicates whether to ignore differences in case during string
    conversions.
    """
    return bool(val)


@defaults.register_arg(None)
def format(val: str | None, defaults: CastDefaults) -> str:
    """f-string formatting for conversions to strings.
    
    A `format specifier <https://docs.python.org/3/library/string.html#formatspec>`_
    to use for conversions to string.
    """
    if val is not None and not isinstance(val, str):
        raise TypeError(f"`format` must be a string, not {val}")
    return val


@defaults.register_arg(0)
def base(val: int, defaults: CastDefaults) -> int:
    """Base to use for integer <-> string conversions.
    """
    if val != 0 and not 2 <= val <= 36:
        raise ValueError(f"`base` must be 0 or >= 2 and <= 36, not {val}")
    return val


@defaults.register_arg(None)
def call(val: Callable | None, defaults: CastDefaults) -> Callable:
    """Apply a callable over the input data, producing the desired output.

    This is only used for conversions from :class:`ObjectType`.  It allows
    users to specify a custom endpoint to perform this conversion, rather
    than relying exclusively on special methods (which is the default).
    """
    if val is not None and not callable(val):
        raise TypeError(f"`call` must be callable, not {val}")
    return val


@defaults.register_arg(False)
def downcast(
    val: bool | type_specifier,
    defaults: CastDefaults
) -> types.CompositeType:
    """Losslessly reduce the precision of numeric data after converting.
    """
    if isinstance(val, bool):  # empty set is truthy, `None` is falsy
        return types.CompositeType() if val else None
    return resolve.resolve_type([val])


@defaults.register_arg("raise")
def errors(val: str, defaults: CastDefaults) -> str:
    """The rule to apply if/when errors are encountered during conversion.
    """
    if val not in valid_errors:
        raise ValueError(
            f"`errors` must be one of {valid_errors}, not {repr(val)}"
        )
    return val
