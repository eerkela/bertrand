import datetime
from functools import partial
from types import MappingProxyType
from typing import Any, Union, Sequence

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd
import regex as re  # using alternate python regex engine

from .base cimport AtomicType, CompositeType
from .base import dispatch, generic, lru_cache

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve

from pdtypes.util.round cimport Tolerance
from pdtypes.util.round import round_div
from pdtypes.util.time cimport Epoch
from pdtypes.util.time import (
    convert_unit, pytimedelta_to_ns, timedelta_string_to_ns, valid_units
)

# TODO: timedelta -> float does not retain longdouble precision.  This is due
# to the / operator in convert_unit() defaulting to float64.


######################
####    MIXINS    ####
######################


class TimedeltaMixin:

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    @dispatch
    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        rounding: str,
        unit: str,
        step_size: int,
        epoch: Epoch,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert timedelta data to a boolean data type."""
        # 2-step conversion: timedelta -> decimal, decimal -> bool
        series = self.to_decimal(
            series,
            resolve.resolve_type("decimal"),
            tol=tol,
            rounding=rounding,
            unit=unit,
            step_size=step_size,
            epoch=epoch,
            errors=errors
        )
        return series.to_boolean(
            dtype=dtype,
            tol=tol,
            rounding=rounding,
            unit=unit,
            step_size=step_size,
            epoch=epoch,
            errors=errors,
            **unused
        )

    @dispatch
    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        epoch: Epoch,
        tol: Tolerance,
        rounding: str,
        downcast: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert timedelta data to a floating point data type."""
        # convert to nanoseconds, then from nanoseconds to final unit
        series = self.to_integer(
            series,
            resolve.resolve_type(int),
            unit="ns",
            step_size=1,
            epoch=epoch,
            rounding=None,
            downcast=False,
            errors=errors
        )
        if unit != "ns" or step_size != 1:
            series.series = convert_unit(
                series.series,
                "ns",
                unit,
                rounding=rounding,
                since=epoch
            )
            if step_size != 1:
                series.series /= step_size

        return series.to_float(
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            epoch=epoch,
            tol=tol,
            rounding=rounding,
            downcast=downcast,
            errors=errors,
            **unused
        )

    @dispatch
    def to_complex(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        epoch: Epoch,
        tol: Tolerance,
        rounding: str,
        downcast: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert timedelta data to a complex data type."""
        # 2-step conversion: timedelta -> float, float -> complex
        series = self.to_float(
            series,
            dtype.equiv_float,
            unit=unit,
            step_size=step_size,
            epoch=epoch,
            rounding=rounding,
            downcast=False,
            errors=errors
        )
        return series.to_complex(
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            epoch=epoch,
            tol=tol,
            rounding=rounding,
            downcast=downcast,
            errors=errors,
            **unused
        )

    @dispatch
    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        epoch: Epoch,
        tol: Tolerance,
        rounding: str,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert timedelta data to a decimal data type."""
        # convert to nanoseconds, then from nanoseconds to final unit
        series = self.to_integer(
            series,
            resolve.resolve_type(int),
            unit="ns",
            step_size=1,
            epoch=epoch,
            rounding=None,
            downcast=False,
            errors=errors
        )
        series = series.to_decimal(
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            epoch=epoch,
            tol=tol,
            rounding=rounding,
            errors=errors,
            **unused
        )
        if unit != "ns" or step_size != 1:
            series.series = convert_unit(
                series.series,
                "ns",
                unit,
                rounding=rounding,
                since=epoch
            )
            if step_size != 1:
                series.series /= step_size

        return series


    # TODO: to_datetime


    @dispatch
    def to_timedelta(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert timedelta data to another timedelta data type."""
        if dtype.unwrap() == series.element_type.unwrap():
            return series

        # 2-step conversion: timedelta -> int, int -> timedelta
        series = self.to_integer(
            series,
            resolve.resolve_type(int),
            unit="ns",
            step_size=1,
            epoch=Epoch("utc"),
            rounding=None,
            downcast=False,
            errors=errors
        )
        return series.to_timedelta(  # TODO: have to define int -> timedelta
            dtype=dtype,
            errors=errors,
            **unused
        )


#######################
####    GENERIC    ####
#######################


@generic
class TimedeltaType(TimedeltaMixin, AtomicType):

    conversion_func = cast.to_timedelta  # all subtypes/backends inherit this
    name = "timedelta"
    aliases = {"timedelta"}
    max = 0
    min = 1  # these values always trip overflow/upcast check

    def __init__(self):
        super().__init__(
            type_def=None,
            dtype=np.dtype("O"),
            na_value=pd.NaT,
            itemsize=None
        )


#####################
####    NUMPY    ####
#####################


@lru_cache(64)
@TimedeltaType.register_backend("numpy")
class NumpyTimedelta64Type(TimedeltaMixin, AtomicType):

    aliases = {
        np.timedelta64,
        # np.dtype("m8") handled in resolve_typespec_dtype special case
        "m8",
        "timedelta64",
        "numpy.timedelta64",
        "np.timedelta64",
    }
    # NOTE: these epochs are chosen to minimize the available range, so that
    # conversions work regardless of leap days.
    max = convert_unit(2**63 - 1, "Y", "ns", since=Epoch("cocoa"))
    min = convert_unit(-2**63 + 1, "Y", "ns", since=Epoch("j2000"))

    def __init__(self, unit: str = None, step_size: int = 1):
        if unit is None:
            dtype = np.dtype("m8")
        else:
            valid_units = {"ns", "us", "ms", "s", "m", "h", "D", "W", "M", "Y"}
            if unit not in valid_units:
                raise ValueError(f"unit not understood: {repr(unit)}")
            dtype = np.dtype(f"m8[{step_size}{unit}]")

        super().__init__(
            type_def=np.timedelta64,
            dtype=dtype,
            na_value=pd.NaT,
            itemsize=8,
            unit=unit,
            step_size=step_size
        )

    ############################
    ####    TYPE METHODS    ####
    ############################

    @classmethod
    def slugify(cls, unit: str = None, step_size: int = 1) -> str:
        slug = cls.name
        if unit is not None:
            slug += f"[{cls.backend}, {step_size}{unit}]"
        else:
            slug += f"[{cls.backend}]"
        return slug

    def contains(self, other: Any) -> bool:
        other = resolve.resolve_type(other)

        # treat unit=None as wildcard
        if self.unit is None:
            if isinstance(other, CompositeType):
                return all(isinstance(o, type(self)) for o in other)
            return isinstance(other, type(self))

        return super().contains(other)

    @classmethod
    def detect(cls, example: np.datetime64, **defaults) -> AtomicType:
        unit, step_size = np.datetime_data(example)
        return cls.instance(unit=unit, step_size=step_size, **defaults)

    @classmethod
    def resolve(cls, context: str = None) -> AtomicType:
        if context is not None:
            match = m8_pattern.match(context)
            if not match:
                raise ValueError(f"invalid unit: {repr(context)}")
            unit = match.group("unit")
            step_size = int(match.group("step_size") or 1)
            return cls.instance(unit=unit, step_size=step_size)
        return cls.instance()

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    @dispatch
    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        epoch: Epoch,
        rounding: str,
        downcast: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert numpy timedelta64s into an integer data type."""
        # NOTE: using numpy m8 array is ~2x faster than looping through series
        m8_str = f"m8[{self.step_size}{self.unit}]"
        arr = series.series.to_numpy(m8_str).view(np.int64).astype("O")
        arr *= self.step_size
        arr = convert_unit(
            arr,
            self.unit,
            unit,
            rounding=rounding or "down",
            since=epoch
        )
        series = cast.SeriesWrapper(
            pd.Series(arr, index=series.series.index),
            hasnans=series.hasnans,
            element_type=resolve.resolve_type(int)
        )

        series, dtype = series.boundscheck(dtype, tol=0, errors=errors)
        return super().to_integer(
            series,
            dtype,
            downcast=downcast,
            errors=errors
        )


######################
####    PANDAS    ####
######################


@TimedeltaType.register_backend("pandas")
class PandasTimedeltaType(TimedeltaMixin, AtomicType):

    aliases = {pd.Timedelta, "Timedelta", "pandas.Timedelta", "pd.Timedelta"}
    max = pd.Timedelta.max.value
    min = pd.Timedelta.min.value

    def __init__(self):
        super().__init__(
            type_def=pd.Timedelta,
            dtype=np.dtype("m8[ns]"),
            na_value=pd.NaT,
            itemsize=8
        )

    @dispatch
    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        epoch: Epoch,
        rounding: str,
        downcast: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert pandas Timedeltas to an integer data type."""
        series = series.rectify().astype(np.int64)
        if unit != "ns" or step_size != 1:
            convert_ns_to_unit(
                series,
                unit=unit,
                step_size=step_size,
                rounding=rounding,
                epoch=epoch
            )

        series, dtype = series.boundscheck(dtype, tol=0, errors=errors)
        return super().to_integer(
            series,
            dtype,
            downcast=downcast,
            errors=errors
        )


######################
####    PYTHON    ####
######################


@TimedeltaType.register_backend("python")
class PythonTimedeltaType(TimedeltaMixin, AtomicType):

    aliases = {datetime.timedelta, "pytimedelta", "datetime.timedelta"}
    max = pytimedelta_to_ns(datetime.timedelta.max)
    min = pytimedelta_to_ns(datetime.timedelta.min)

    def __init__(self):
        super().__init__(
            type_def=datetime.timedelta,
            dtype=np.dtype("O"),
            na_value=pd.NaT,
            itemsize=None
        )

    @dispatch
    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        epoch: Epoch,
        rounding: str,
        downcast: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert python timedeltas to an integer data type."""
        series = series.apply_with_errors(pytimedelta_to_ns)
        series.element_type = int

        if unit != "ns" or step_size != 1:
            convert_ns_to_unit(
                series,
                unit=unit,
                step_size=step_size,
                rounding=rounding,
                epoch=epoch
            )

        series, dtype = series.boundscheck(dtype, tol=0, errors=errors)
        return super().to_integer(
            series,
            dtype,
            downcast=downcast,
            errors=errors
        )


#######################
####    PRIVATE    ####
#######################


cdef object m8_pattern = re.compile(
    r"(?P<step_size>[0-9]+)?(?P<unit>ns|us|ms|s|m|h|D|W|M|Y)"
)


def convert_ns_to_unit(
    series: cast.SeriesWrapper,
    unit: str,
    step_size: int,
    rounding: str,
    epoch: Epoch
) -> None:
    """Helper for converting between integer time units."""
    series.series = convert_unit(
        series.series,
        "ns",
        unit,
        since=epoch,
        rounding=rounding or "down",
    )
    if step_size != 1:
        series.series = round_div(
            series.series,
            step_size,
            rule=rounding or "down"
        )
