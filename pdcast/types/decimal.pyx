import decimal
import sys

import numpy as np
cimport numpy as np
import pandas as pd
import pytz

cimport pdcast.convert as convert
import pdcast.convert as convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from pdcast.util.error import shorten_list
from pdcast.util.round cimport Tolerance
from pdcast.util.round import round_decimal
from pdcast.util.time cimport Epoch
from pdcast.util.time import as_ns, round_months_to_ns, round_years_to_ns
from pdcast.util.type_hints import numeric

from .base cimport AtomicType, CompositeType
from .base import dispatch, generic, register


# https://github.com/pandas-dev/pandas/blob/e246c3b05924ac1fe083565a765ce847fcad3d91/pandas/tests/extension/decimal/array.py


######################
####    MIXINS    ####
######################


class DecimalMixin:

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    @dispatch
    def round(
        self,
        series: convert.SeriesWrapper,
        decimals: int = 0,
        rule: str = "half_even"
    ) -> convert.SeriesWrapper:
        """Round a decimal series to the given number of decimal places using
        the specified rounding rule.
        """
        rule = convert.validate_rounding(rule)
        return convert.SeriesWrapper(
            round_decimal(series.series, rule=rule, decimals=decimals),
            hasnans=series.hasnans,
            element_type=series.element_type
        )

    @dispatch
    def snap(
        self,
        series: convert.SeriesWrapper,
        tol: numeric = 1e-6
    ) -> convert.SeriesWrapper:
        """Snap each element of the series to the nearest integer if it is
        within the specified tolerance.
        """
        tol = Tolerance(tol)
        if not tol:  # trivial case, tol=0
            return series.copy()

        rounded = self.round(series, rule="half_even")
        return convert.SeriesWrapper(
            series.series.where((
                (series.series - rounded).abs() > tol.real),
                rounded.series
            ),
            hasnans=series.hasnans,
            element_type=series.element_type
        )

    def snap_round(
        self,
        series: convert.SeriesWrapper,
        tol: numeric,
        rule: str,
        errors: str
    ) -> convert.SeriesWrapper:
        """Snap a series to the nearest integer within `tol`, and then round
        any remaining results according to the given rule.  Rejects any outputs
        that are not integer-like by the end of this process.
        """
        # apply tolerance, then check for non-integers if not rounding
        if tol or rule is None:
            rounded = self.round(series, rule="half_even")  # compute once
            outside = ~series.within_tol(rounded, tol=tol)
            if tol:
                element_type = series.element_type
                series = series.where(outside.series, rounded.series)
                series.element_type = element_type

            # check for non-integer (ignore if rounding)
            if rule is None and outside.any():
                if errors == "coerce":
                    series = self.round(series, "down")
                else:
                    raise ValueError(
                        f"precision loss exceeds tolerance {float(tol):g} at "
                        f"index {shorten_list(outside[outside].index.values)}"
                    )

        # round according to specified rule
        if rule:
            series = self.round(series, rule=rule)

        return series

    def to_boolean(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert decimal data to a boolean data type."""
        series = self.snap_round(
            series,
            tol=tol.real,
            rule=rounding,
            errors=errors
        )
        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_boolean(series, dtype=dtype, errors=errors)

    def to_integer(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert decimal data to an integer data type."""
        series = self.snap_round(
            series,
            tol=tol.real,
            rule=rounding,
            errors=errors
        )
        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_integer(
            series,
            dtype,
            downcast=downcast,
            errors=errors
        )

    def to_float(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert decimal data to a floating point data type."""
        # do naive conversion
        if dtype.itemsize > 8:
            # NOTE: series.astype() implicitly calls Decimal.__float__(), which
            # is limited to 64-bits.  Converting to an intermediate string
            # representation avoids this.
            result = series.astype(str).astype(dtype)
        else:
            result = series.astype(dtype)

        # check for overflow
        if int(series.min) < dtype.min or int(series.max) > dtype.max:
            infs = result.isinf() ^ series.isinf()
            if infs.any():
                if errors == "coerce":
                    result = result[~infs]
                    result.hasnans = True
                    series = series[~infs]  # mirror on original
                else:
                    raise OverflowError(
                        f"values exceed {dtype} range at index "
                        f"{shorten_list(infs[infs].index.values)}"
                    )

        # backtrack to check for precision loss
        if errors != "coerce":  # coercion ignores precision loss
            bad = ~series.within_tol(
                dtype.to_decimal(result, dtype=self, errors="raise"),
                tol=tol.real
            )
            if bad.any():
                raise ValueError(
                    f"precision loss exceeds tolerance {float(tol.real):g} at "
                    f"index {shorten_list(bad[bad].index.values)}"
                )

        if downcast is not None:
            return dtype.downcast(result, smallest=downcast, tol=tol)
        return result

    def to_complex(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tol: numeric,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert decimal data to a complex data type."""
        # 2-step conversion: decimal -> float, float -> complex
        transfer_type = dtype.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_complex(
            series,
            dtype=dtype,
            tol=tol,
            downcast=downcast,
            errors=errors,
            **unused
        )

    def to_decimal(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert boolean data to a decimal data type."""
        return series.astype(dtype, errors=errors)

    def to_decimal(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert decimal data to another decimal type."""
        # trivial case
        if dtype == self:
            return series.rectify()
        return super().to_decimal(series, dtype=dtype, **unused)

    def to_datetime(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        since: Epoch,
        tz: pytz.BaseTzInfo,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert integer data to a datetime data type."""
        # round fractional inputs to the nearest nanosecond
        if unit == "Y":
            ns = round_years_to_ns(series.series * step_size, since=since)
        elif unit == "M":
            ns = round_months_to_ns(series.series * step_size, since=since)
        else:
            cast_to_int = np.frompyfunc(int, 1, 1)
            ns = cast_to_int(series.series * step_size * as_ns[unit])

        # account for non-utc epoch
        if since:
            ns += since.offset

        series = convert.SeriesWrapper(
            ns,
            hasnans=series.hasnans,
            element_type=resolve.resolve_type("int[python]")
        )

        # check for overflow and upcast if applicable
        series, dtype = series.boundscheck(dtype, errors=errors)

        # convert to final representation
        return dtype.from_ns(
            series,
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            since=since,
            tz=tz,
            errors=errors,
            **unused
        )

    def to_timedelta(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        since: Epoch,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert integer data to a timedelta data type."""
        # round fractional inputs to the nearest nanosecond
        if unit == "Y":  # account for leap days
            ns = round_years_to_ns(series.series * step_size, since=since)
        elif unit == "M":  # account for irregular lengths
            ns = round_months_to_ns(series.series * step_size, since=since)
        else:
            cast_to_int = np.frompyfunc(int, 1, 1)
            ns = cast_to_int(series.series * step_size * as_ns[unit])

        series = convert.SeriesWrapper(
            ns,
            hasnans=series.hasnans,
            element_type=resolve.resolve_type("int[python]")
        )

        # check for overflow and upcast if necessary
        series, dtype = series.boundscheck(dtype, errors=errors)

        # convert to final representation
        return dtype.from_ns(
            series,
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            since=since,
            errors=errors,
            **unused,
        )


#######################
####    GENERIC    ####
#######################


@register
@generic
class DecimalType(DecimalMixin, AtomicType):

    # internal root fields - all subtypes/backends inherit these
    conversion_func = convert.to_decimal
    is_numeric = True

    name = "decimal"
    aliases = {"decimal"}
    type_def = decimal.Decimal
    itemsize = sys.getsizeof(decimal.Decimal(0))
    na_value = decimal.Decimal("nan")


##############################
####    PYTHON DECIMAL    ####
##############################


@register
@DecimalType.register_backend("python")
class PythonDecimalType(DecimalMixin, AtomicType):

    aliases = {decimal.Decimal}
    type_def = decimal.Decimal
    itemsize = sys.getsizeof(decimal.Decimal(0))
    na_value = decimal.Decimal("nan")
