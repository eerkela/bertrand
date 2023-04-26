from pdcast import types
from pdcast.util import wrapper
from pdcast.util.round import Tolerance

from .base import to_boolean, to_integer, to_float, to_decimal


#######################
####    BOOLEAN    ####
#######################


@to_boolean.overload("complex")
def complex_to_boolean(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert complex data to a boolean data type."""
    # 2-step conversion: complex -> float, float -> bool
    series = to_float(
        series,
        dtype="float",
        tol=tol,
        downcast=None,
        errors=errors
    )
    return to_boolean(
        series,
        dtype=dtype,
        rounding=rounding,
        tol=tol,
        errors=errors,
        **unused
    )


#######################
####    INTEGER    ####
#######################


@to_integer.overload("complex")
def complex_to_integer(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert complex data to an integer data type."""
    # 2-step conversion: complex -> float, float -> int
    series = to_float(
        series,
        dtype="float",
        tol=tol,
        downcast=None,
        errors=errors
    )
    return to_integer(
        series,
        dtype=dtype,
        rounding=rounding,
        tol=tol,
        downcast=downcast,
        errors=errors,
        **unused
    )


#####################
####    FLOAT    ####
#####################


@to_float.overload("complex")
def complex_to_float(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert complex data to a float data type."""
    real = series.real

    # check for nonzero imag
    if errors != "coerce":  # ignore if coercing
        bad = ~series.imag.within_tol(0, tol=tol.imag)
        if bad.any():
            raise ValueError(
                f"imaginary component exceeds tolerance "
                f"{float(tol.imag):g} at index "
                f"{shorten_list(bad[bad].index.values)}"
            )

    return to_float(
        real,
        dtype=dtype,
        tol=tol,
        downcast=downcast,
        errors=errors,
        **unused
    )


#######################
####    DECIMAL    ####
#######################


@to_decimal.overload("complex")
def complex_to_decimal(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert complex data to a decimal data type."""
    # 2-step conversion: complex -> float, float -> decimal
    series = to_float(
        series,
        dtype="float",
        tol=tol,
        downcast=None,
        errors=errors
    )
    return to_decimal(
        series,
        dtype=dtype,
        tol=tol,
        errors=errors,
        **unused
    )
