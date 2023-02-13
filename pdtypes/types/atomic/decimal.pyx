import decimal
from functools import partial
from typing import Union, Sequence

import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.error import shorten_list
from pdtypes.type_hints import numeric
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve

from pdtypes.util.round cimport Tolerance
from pdtypes.util.round import round_decimal
from pdtypes.util.time cimport Epoch
from pdtypes.util.time import (
    as_ns, convert_unit, round_months_to_ns, round_years_to_ns
)

from .base cimport AtomicType, BaseType
from .base import dispatch, generic


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
        series: cast.SeriesWrapper,
        rule: str = "half_even",
        decimals: int = 0
    ) -> cast.SeriesWrapper:
        """Round a decimal series to the given number of decimal places using
        the specified rounding rule.
        """
        return cast.SeriesWrapper(
            round_decimal(series.series, rule=rule, decimals=decimals),
            hasnans=series.hasnans,
            element_type=series.element_type
        )

    @dispatch
    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert decimal data to a boolean data type."""
        series = series.snap_round(tol.real, rounding, errors)
        series, dtype = series.boundscheck(dtype, errors)
        return super().to_boolean(series, dtype, errors=errors)

    @dispatch
    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        downcast: bool | BaseType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert decimal data to an integer data type."""
        series = series.snap_round(tol.real, rounding, errors)
        series, dtype = series.boundscheck(dtype, errors)
        return super().to_integer(
            series,
            dtype,
            downcast=downcast,
            errors=errors
        )

    @dispatch
    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: bool | BaseType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
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
        if int(series.min()) < dtype.min or int(series.max()) > dtype.max:
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
                result.to_decimal(self, errors="raise"),
                tol=tol.real
            )
            if bad.any():
                raise ValueError(
                    f"precision loss exceeds tolerance {float(tol.real):g} at "
                    f"index {shorten_list(bad[bad].index.values)}"
                )

        if downcast:
            smallest = downcast if not isinstance(downcast, bool) else None
            return dtype.downcast(result, tol=tol.real, smallest=smallest)
        return result

    @dispatch
    def to_complex(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: numeric,
        downcast: bool | BaseType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert decimal data to a complex data type."""
        series = self.to_float(
            series,
            dtype.equiv_float,
            tol=tol,
            downcast=False,
            errors=errors
        )
        return series.to_complex(
            dtype=dtype,
            tol=tol,
            downcast=downcast,
            errors=errors
            **unused
        )

    @dispatch
    def to_datetime(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        epoch: Epoch,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert integer data to a datetime data type."""
        # round fractional inputs to the nearest nanosecond
        if unit == "Y":
            ns = round_years_to_ns(series.series * step_size, since=epoch)
        elif unit == "M":
            ns = round_months_to_ns(series.series * step_size, since=epoch)
        else:
            cast_to_int = np.frompyfunc(int, 1, 1)
            ns = cast_to_int(series.series * step_size * as_ns[unit])

        # account for non-utc epoch
        if epoch:
            ns += epoch.offset

        series = cast.SeriesWrapper(
            ns,
            hasnans=series.hasnans,
            element_type=resolve.resolve_type(int)
        )

        # check for overflow and upcast if applicable
        series, dtype = series.boundscheck(dtype, errors=errors)

        # convert to final representation
        return dtype.from_ns(series, **unused)

    @dispatch
    def to_timedelta(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        epoch: Epoch,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert integer data to a timedelta data type."""
        # round fractional inputs to the nearest nanosecond
        if unit == "Y":  # account for leap days
            ns = round_years_to_ns(series.series * step_size, since=epoch)
        elif unit == "M":  # account for irregular lengths
            ns = round_months_to_ns(series.series * step_size, since=epoch)
        else:
            cast_to_int = np.frompyfunc(int, 1, 1)
            ns = cast_to_int(series.series * step_size * as_ns[unit])

        series = cast.SeriesWrapper(
            ns,
            hasnans=series.hasnans,
            element_type=resolve.resolve_type(int)
        )

        # check for overflow and upcast if necessary
        series, dtype = series.boundscheck(dtype, errors=errors)

        # convert to final representation
        return dtype.from_ns(
            series,
            epoch=epoch,
            **unused,
        )


#######################
####    GENERIC    ####
#######################


@generic
class DecimalType(DecimalMixin, AtomicType):

    conversion_func = cast.to_decimal  # all subtypes/backends inherit this
    name = "decimal"
    aliases = {"decimal"}

    def __init__(self):
        super().__init__(
            type_def=decimal.Decimal,
            dtype=np.dtype(np.object_),
            na_value=pd.NA,
            itemsize=None
        )


##############################
####    PYTHON DECIMAL    ####
##############################


@DecimalType.register_backend("python")
class PythonDecimalType(DecimalMixin, AtomicType):

    aliases = {decimal.Decimal}

    def __init__(self):
        super().__init__(
            type_def=decimal.Decimal,
            dtype=np.dtype(np.object_),
            na_value=pd.NA,
            itemsize=None
        )
