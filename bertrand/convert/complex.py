"""This module contains dispatched cast() implementations for complex data."""
# pylint: disable=unused-argument
import pandas as pd

from pdcast import types
from pdcast.util.error import shorten_list
from pdcast.util.numeric import real, imag, within_tol
from pdcast.util.round import Tolerance

from .base import cast, generic_to_float


@cast.overload("complex", "bool")
def complex_to_boolean(
    series: pd.Series,
    dtype: types.ScalarType,
    rounding: str,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert complex data to a boolean data type."""
    # 2-step conversion: complex -> float, float -> bool
    series = cast(
        series,
        "float",
        tol=tol,
        downcast=None,
        errors=errors
    )
    return cast(
        series,
        dtype,
        rounding=rounding,
        tol=tol,
        errors=errors,
        **unused
    )


@cast.overload("complex", "int")
def complex_to_integer(
    series: pd.Series,
    dtype: types.ScalarType,
    rounding: str,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert complex data to an integer data type."""
    # 2-step conversion: complex -> float, float -> int
    series = cast(
        series,
        "float",
        tol=tol,
        downcast=None,
        errors=errors
    )
    return cast(
        series,
        dtype,
        rounding=rounding,
        tol=tol,
        downcast=downcast,
        errors=errors,
        **unused
    )


@cast.overload("complex", "float")
def complex_to_float(
    series: pd.Series,
    dtype: types.ScalarType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert complex data to a float data type."""
    # check for nonzero imag
    if errors != "coerce":  # ignore if coercing
        bad = ~within_tol(imag(series), 0, tol=tol.imag)
        if bad.any():
            raise ValueError(
                f"imaginary component exceeds tolerance "
                f"{float(tol.imag):g} at index "
                f"{shorten_list(series[bad].index.values)}"
            )

    return generic_to_float(
        real(series),
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors,
        **unused
    )


@cast.overload("complex", "decimal")
def complex_to_decimal(
    series: pd.Series,
    dtype: types.ScalarType,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert complex data to a decimal data type."""
    # 2-step conversion: complex -> float, float -> decimal
    series = cast(
        series,
        "float",
        tol=tol,
        downcast=None,
        errors=errors
    )
    return cast(
        series,
        dtype,
        tol=tol,
        errors=errors,
        **unused
    )
