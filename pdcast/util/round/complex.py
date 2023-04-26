from pdcast.util import wrapper

from .round import round, snap
from .float import round_float
from .tolerance import Tolerance


@round.overload("complex")
def round_complex(
    series: wrapper.SeriesWrapper,
    decimals: int = 0,
    rule: str = "half_even"
) -> wrapper.SeriesWrapper:
    """Round a complex series to the given number of decimal places using
    the specified rounding rule.
    """
    real = round_float(series.real, decimals=decimals, rule=rule)
    imag = round_float(series.imag, decimals=decimals, rule=rule)
    return real + imag * 1j


@snap.overload("complex")
def snap(
    series: wrapper.SeriesWrapper,
    tol: Tolerance
) -> wrapper.SeriesWrapper:
    """Snap each element of the series to the nearest integer if it is
    within the specified tolerance.
    """
    if not tol:  # trivial case, tol=0
        return series.copy()

    real = snap(series.real, tol=tol.real)
    imag = snap(series.imag, tol=tol.imag)
    return real + imag * 1j
