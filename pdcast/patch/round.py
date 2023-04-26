from pdcast.convert.arguments import rounding
from pdcast.decorators.attachable import attachable, VirtualAttribute
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func

from pdcast.util.round import round_div, round_float, round_decimal
from pdcast.util.wrapper import SeriesWrapper


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
    if isinstance(endpoint, VirtualAttribute):
        endpoint = endpoint.original

    return SeriesWrapper(
        endpoint(decimals=decimals),
        element_type=series.element_type,
        hasnans=series.hasnans
    )


@round.overload("int")
def integer_round(
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


@round.overload("float")
def float_round(
    series: SeriesWrapper,
    decimals: int,
    rule: str
) -> SeriesWrapper:
    """Round a floating point series to the given number of decimal places
    using the specified rounding rule.
    """
    return SeriesWrapper(
        round_float(series.rectify().series, rule=rule, decimals=decimals),
        hasnans=series.hasnans,
        element_type=series.element_type
    )


@round.overload("complex")
def complex_round(
    series: SeriesWrapper,
    decimals: int,
    rule: str
) -> SeriesWrapper:
    """Round a series of complex numbers to the given number of decimals.

    NOTE: this rounds real and imaginary components separately.
    """
    real = float_round(series.real, decimals=decimals, rule=rule)
    imag = float_round(series.imag, decimals=decimals, rule=rule)
    return real + imag * 1j


@round.overload("decimal")
def decimal_round(
    series: SeriesWrapper,
    decimals: int,
    rule: str
) -> SeriesWrapper:
    """Round a series of arbitrary precision decimal numbers to the given
    number of decimal places.
    """
    return SeriesWrapper(
        round_decimal(series.series, rule=rule, decimals=decimals),
        hasnans=series.hasnans,
        element_type=series.element_type
    )


#########################
####    ARGUMENTS    ####
#########################


@round.register_arg
def decimals(val: int, state: dict) -> int:
    """Ensure that `decimals` are integer-like."""
    return int(val)


# use same validator as conversions
round.register_arg(rounding, name="rule")
