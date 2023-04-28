from pdcast.decorators import attachable, dispatch, extension_func
from pdcast.util import wrapper
from pdcast.util.type_hints import numeric

from .decimal import round_decimal
from .integer import round_div
from .float import round_float
from .tolerance import Tolerance


######################
####    PUBLIC    ####
######################


@attachable
@extension_func
@dispatch
def round(
    series: wrapper.SeriesWrapper,
    decimals: int = 0,
    rule: str = "half_even"
) -> wrapper.SeriesWrapper:
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

    return wrapper.SeriesWrapper(
        endpoint(decimals=decimals),
        element_type=series.element_type,
        hasnans=series.hasnans
    )


@attachable
@extension_func
@dispatch
def snap(
    series: wrapper.SeriesWrapper,
    tol: numeric = 1e-6
) -> wrapper.SeriesWrapper:
    """Snap each element of the series to the nearest integer if it is
    within the specified tolerance.
    """
    # trivial case, tol=0
    if not tol:
        return series.copy()

    # use rounded result if within tol, else use original
    rounded = round(series, rule="half_even")
    return wrapper.SeriesWrapper(
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
    series: wrapper.SeriesWrapper,
    tol: numeric,
    rule: str | None,
    errors: str
) -> wrapper.SeriesWrapper:
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


#######################
####    PRIVATE    ####
#######################


@round.overload("complex")
def _round_complex(
    series: wrapper.SeriesWrapper,
    decimals: int,
    rule: str
) -> wrapper.SeriesWrapper:
    """Round a complex series to the given number of decimal places using
    the specified rounding rule.
    """
    real = round_float(series.real, decimals=decimals, rule=rule)
    imag = round_float(series.imag, decimals=decimals, rule=rule)
    return real + imag * 1j


@snap.overload("complex")
def _snap_complex(
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


@round.overload("decimal")
def _round_decimal(
    series: wrapper.SeriesWrapper,
    decimals: int,
    rule: str,
) -> wrapper.SeriesWrapper:
    """Overloaded round() implementation for decimal data."""
    return wrapper.SeriesWrapper(
        round_decimal(series, decimals=decimals, rule=rule),
        hasnans=series.hasnans,
        element_type=series.element_type
    )


@round.overload("float")
def _round_float(
    series: wrapper.SeriesWrapper,
    decimals: int,
    rule: str
) -> wrapper.SeriesWrapper:
    """Overloaded round() implementation for float data."""
    return wrapper.SeriesWrapper(
        round_float(series, decimals=decimals, rule=rule),
        hasnans=series.hasnans,
        element_type=series.element_type
    )


@round.overload("int")
def _round_integer(
    series: wrapper.SeriesWrapper,
    decimals: int,
    rule: str
) -> wrapper.SeriesWrapper:
    """Round an integer series to the given number of decimals.

    NOTE: this implementation does nothing unless the input to `decimals` is
    negative.
    """
    if decimals < 0:
        scale = 10**(-1 * decimals)
        return wrapper.SeriesWrapper(
            round_div(series.series, scale, rule=rule) * scale,
            hasnans=series.hasnans,
            element_type=series.element_type
        )
    return series.copy()


@snap.overload("int")
def _snap_integer(
    series: wrapper.SeriesWrapper,
    tol: Tolerance
) -> wrapper.SeriesWrapper:
    """Snap each element of the series to the nearest integer if it is
    within the specified tolerance.

    For integers, this is an identity function.
    """
    return series.copy()
