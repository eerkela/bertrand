from pdcast import types
from pdcast.util import wrapper
from pdcast.util.round import snap_round, Tolerance

from .base import to_boolean, to_integer, to_decimal


#######################
####    BOOLEAN    ####
#######################


@to_boolean.overload("float")
def float_to_boolean(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert floating point data to a boolean data type."""
    series = snap_round(
        series,
        tol=tol.real,
        rule=rounding,
        errors=errors
    )
    series, dtype = series.boundscheck(dtype, errors=errors)
    return to_boolean.generic(
        series,
        dtype=dtype,
        errors=errors
    )


#######################
####    INTEGER    ####
#######################


@to_integer.overload("float")
def float_to_integer(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert floating point data to an integer data type."""
    series = snap_round(
        series,
        tol=tol.real,
        rule=rounding,
        errors=errors
    )
    series, dtype = series.boundscheck(dtype, errors=errors)
    return to_integer.generic(
        series,
        dtype=dtype,
        downcast=downcast,
        errors=errors
    )


#######################
####    DECIMAL    ####
#######################


try:  # float80 might not be defined

    @to_decimal.overload("float80")
    def longdouble_to_decimal(
        series: wrapper.SeriesWrapper,
        dtype: types.AtomicType,
        errors: str,
        **unused
    ) -> wrapper.SeriesWrapper:
        """A special case of FloatMixin.to_decimal() that bypasses `TypeError:
        conversion from numpy.float128 to Decimal is not supported`.
        """
        # convert longdouble to integer ratio and then to decimal
        def call(element):
            numerator, denominator = element.as_integer_ratio()
            return dtype.type_def(numerator) / denominator

        return series.apply_with_errors(
            call=call,
            errors=errors,
            element_type=dtype
        )

except ValueError:
    pass
