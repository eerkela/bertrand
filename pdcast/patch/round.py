from pdcast.decorators.attachable import attachable
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func
from pdcast.decorators.wrapper import SeriesWrapper
from pdcast.util.type_hints import numeric

from pdcast.util.round import round_decimal, round_div, round_float, Tolerance


######################
####    PUBLIC    ####
######################


@attachable
@extension_func
@dispatch
def round(
    series: SeriesWrapper,
    decimals: int = 0,
    rule: str = "half_even"
) -> SeriesWrapper:
    """TODO: copy from abstract docs"""
    if rule != "half_even":
        raise ValueError(
            f"original pandas `Series.round()` implementation accepts only "
            f"rule='half_even', not {repr(rule)}"
        )

    # bypass attachment
    endpoint = series.series.round
    if isinstance(endpoint, attachable.VirtualAttribute):
        endpoint = endpoint.original

    return SeriesWrapper(
        endpoint(decimals=decimals),
        element_type=series.element_type,
        hasnans=series.hasnans
    )


@attachable
@extension_func
@dispatch
def snap(
    series: SeriesWrapper,
    tol: numeric = 1e-6
) -> SeriesWrapper:
    """Snap each element of the series to the nearest integer if it is
    within the specified tolerance.
    """
    # trivial case, tol=0
    if not tol:
        return series.copy()

    # use rounded result if within tol, else use original
    rounded = round(series, rule="half_even")
    return SeriesWrapper(
        series.series.where((
            (series.series - rounded).abs() > tol.real),
            rounded.series
        ),
        hasnans=series.hasnans,
        element_type=series.element_type
    )


@extension_func
@dispatch
def snap_round(
    series: SeriesWrapper,
    tol: numeric,
    rule: str | None,
    errors: str
) -> SeriesWrapper:
    """Snap a series to the nearest integer within `tol`, and then round
    any remaining results according to the given rule.  Rejects any outputs
    that are not integer-like by the end of this process.
    """
    # TODO: update this

    # NOTE: this looks complicated, but it ensures that rounding is done only
    # where necessary.  If `tol` is given and `rule` is not "half_even" or
    # None, then we apply it.

    # apply tolerance
    if tol or rule is None:
        rounded = round(series, rule="half_even")  # compute once
        outside = ~series.within_tol(rounded, tol=tol)
        if tol:
            element_type = series.element_type
            series = series.where(outside.series, rounded.series)
            series.element_type = element_type

        # check for non-integer (ignore if rounding)
        if rule is None and outside.any():
            if errors == "coerce":
                series = round(series, "down")
            else:
                raise ValueError(
                    f"precision loss exceeds tolerance {float(tol):g} at "
                    f"index {shorten_list(outside[outside].index.values)}"
                )

    # round according to specified rule
    if rule:
        series = round(series, rule=rule)

    return series


#########################
####    ARGUMENTS    ####
#########################


valid_rules = (
    "floor", "ceiling", "down", "up", "half_floor", "half_ceiling",
    "half_down", "half_up", "half_even"
)


@round.register_arg
def decimals(val: int, state: dict) -> int:
    """Ensure that `decimals` are integer-like."""
    return int(val)


@round.register_arg
def rule(val: str | None, state: dict) -> str:
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
    if not (val is None or isinstance(val, str)):
        raise TypeError(
            f"`rounding` must be a string or None, not {repr(val)}"
        )
    if val is not None and val not in valid_rules:
        raise ValueError(
            f"`rounding` must be one of {valid_rules}, not {repr(val)}"
        )
    return val


@snap.register_arg
def tol(val: numeric, state: dict) -> Tolerance:
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
    clipped to fit rather than raise an
    :class:`OverflowError <python:OverflowError>`.

    .. doctest::

        >>> pdcast.cast(129, "int8", tol=2)
        0    127
        dtype: int8
        >>> pdcast.cast(129, "int8", tol=0)
        Traceback (most recent call last):
            ...
        OverflowError: values exceed int8 range at index [0]

    Additionally, this argument controls the maximum amount of precision
    loss that can occur when
    :func:`downcasting <pdcast.convert.arguments.downcast>` numeric values.

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
        :func:`rounding <pdcast.convert.arguments.rounding>` to
        ``"half_even"``, with additional clipping around the minimum and
        maximum values.
    """
    if isinstance(val, Tolerance):
        return val
    return Tolerance(val)


#######################
####    PRIVATE    ####
#######################


# NOTE: we have to re-implement these because cython functions are not
# introspectable.


@round.overload("int")
def _round_integer(
    series: SeriesWrapper,
    decimals: int,
    rule: str
) -> SeriesWrapper:
    """Round an integer series to the given number of decimals.

    NOTE: this implementation does nothing unless the input to `decimals` is
    negative.
    """
    if decimals < 0:
        scale = 10**(-1 * decimals)
        return SeriesWrapper(
            round_div(series.series, scale, rule=rule) * scale,
            hasnans=series.hasnans,
            element_type=series.element_type
        )
    return series.copy()


@round.overload("decimal")
def _round_decimal(
    series: SeriesWrapper,
    decimals: int,
    rule: str,
) -> SeriesWrapper:
    """Overloaded round() implementation for decimal data."""
    return SeriesWrapper(
        round_decimal(series, decimals=decimals, rule=rule),
        hasnans=series.hasnans,
        element_type=series.element_type
    )


@round.overload("float")
def _round_float(
    series: SeriesWrapper,
    decimals: int,
    rule: str
) -> SeriesWrapper:
    """Overloaded round() implementation for float data."""
    return SeriesWrapper(
        round_float(series, decimals=decimals, rule=rule),
        hasnans=series.hasnans,
        element_type=series.element_type
    )


@round.overload("complex")
def _round_complex(
    series: SeriesWrapper,
    decimals: int,
    rule: str
) -> SeriesWrapper:
    """Round a complex series to the given number of decimal places using
    the specified rounding rule.
    """
    real = round_float(series.real, decimals=decimals, rule=rule)
    imag = round_float(series.imag, decimals=decimals, rule=rule)
    return real + imag * 1j


@snap.overload("int")
def _snap_integer(
    series: SeriesWrapper,
    tol: Tolerance
) -> SeriesWrapper:
    """Snap each element of the series to the nearest integer if it is
    within the specified tolerance.

    For integers, this is an identity function.
    """
    return series.copy()


@snap.overload("complex")
def _snap_complex(
    series: SeriesWrapper,
    tol: Tolerance
) -> SeriesWrapper:
    """Snap each element of the series to the nearest integer if it is
    within the specified tolerance.
    """
    if not tol:  # trivial case, tol=0
        return series.copy()

    real = snap(series.real, tol=tol.real)
    imag = snap(series.imag, tol=tol.imag)
    return real + imag * 1j
