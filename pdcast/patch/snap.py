# pylint: disable=redefined-outer-name, unused-argument
import pandas as pd

from pdcast.decorators.attachable import attachable
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func
from pdcast.util.numeric import real, imag, within_tol
from pdcast.util.round import Tolerance
from pdcast.util.type_hints import numeric

from .round import round


######################
####    PUBLIC    ####
######################


@attachable
@extension_func
@dispatch("series")
def snap(series: pd.Series, tol: numeric = 1e-6) -> pd.Series:
    """Snap each element of the series to the nearest integer if it is
    within the specified tolerance.
    """
    if not tol:
        return series.copy()

    rounded = round(series, rule="half_even")
    return series.where(~within_tol(series, rounded, tol=tol.real), rounded)


#########################
####    ARGUMENTS    ####
#########################


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


@snap.overload("int")
def _snap_integer(series: pd.Series, tol: Tolerance) -> pd.Series:
    """Snap each element of the series to the nearest integer if it is
    within the specified tolerance.

    For integers, this is an identity function.
    """
    return series


@snap.overload("complex")
def _snap_complex(series: pd.Series, tol: Tolerance) -> pd.Series:
    """Snap each element of the series to the nearest integer if it is
    within the specified tolerance.
    """
    if not tol:  # trivial case, tol=0
        return series.copy()

    real_part = snap(real(series), tol=tol.real)
    imag_part = snap(imag(series), tol=tol.imag)
    return real_part + imag_part * 1j
