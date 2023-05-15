# pylint: disable=redefined-outer-name, unused-argument
import pandas as pd

from pdcast.convert.util import real, imag
from pdcast.decorators.attachable import attachable, VirtualAttribute
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func
from pdcast.util.round import round_decimal, round_div, round_float


######################
####    PUBLIC    ####
######################


@attachable
@extension_func
@dispatch("series")
def round(
    series: pd.Series,
    decimals: int = 0,
    rule: str = "half_even"
) -> pd.Series:
    """TODO: copy from abstract docs"""
    if rule != "half_even":
        raise ValueError(
            f"original pandas `Series.round()` implementation accepts only "
            f"rule='half_even', not {repr(rule)}"
        )

    original = getattr(series.round, "original", series.round)
    return original(decimals=decimals)


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


#######################
####    PRIVATE    ####
#######################


@round.overload("int")
def _round_integer(
    series: pd.Series,
    decimals: int,
    rule: str
) -> pd.Series:
    """Round an integer series to the given number of decimals.

    NOTE: this implementation does nothing unless the input to `decimals` is
    negative.
    """
    if decimals < 0:
        scale = 10**(-1 * decimals)
        return round_div(series, scale, rule=rule) * scale
    return series


@round.overload("decimal")
def _round_decimal(
    series: pd.Series,
    decimals: int,
    rule: str,
) -> pd.Series:
    """Overloaded round() implementation for decimal data."""
    return round_decimal(series, decimals=decimals, rule=rule)


@round.overload("float")
def _round_float(
    series: pd.Series,
    decimals: int,
    rule: str
) -> pd.Series:
    """Overloaded round() implementation for float data."""
    return round_float(series, decimals=decimals, rule=rule)


@round.overload("complex")
def _round_complex(
    series: pd.Series,
    decimals: int,
    rule: str
) -> pd.Series:
    """Round a complex series to the given number of decimal places using
    the specified rounding rule.
    """
    real_part = round_float(real(series), decimals=decimals, rule=rule)
    imag_part = round_float(imag(series), decimals=decimals, rule=rule)
    return real_part + imag_part * 1j
