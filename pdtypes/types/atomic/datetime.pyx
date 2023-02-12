import datetime
import decimal
from functools import partial
from types import MappingProxyType
from typing import Any, Union, Sequence
import warnings

import numpy as np
cimport numpy as np
import pandas as pd
import pytz
import regex as re  # using alternate python regex engine

from .base cimport AtomicType, BaseType, CompositeType
from .base import dispatch, generic, lru_cache

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve

from pdtypes.util.round cimport Tolerance
from pdtypes.util.round import round_div
from pdtypes.util.time cimport Epoch
from pdtypes.util.time import (
    as_ns, convert_unit, ns_to_pydatetime, numpy_datetime64_to_ns,
    pydatetime_to_ns, pytimedelta_to_ns, valid_units
)


# TODO: allow for naive tz specification for pandas/python datetime to_integer
# methods.
# -> add `tz` argument for all conversions.


# TODO: parse() should account for self.tz/unit/step_size
# TODO: allow for tz="local"
# -> handled in Timezone factory, which needs to be introduced wherever `tz` is
# referenced (instance, __init__)


######################
####    MIXINS    ####
######################


class DatetimeMixin:

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
        downcast: bool | BaseType,
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
        downcast: bool | BaseType,
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


#######################
####    GENERIC    ####
#######################


@generic
class DatetimeType(DatetimeMixin, AtomicType):

    conversion_func = cast.to_datetime  # all subtypes/backends inherit this
    name = "datetime"
    aliases = {"datetime"}
    max = 0
    min = 1  # NOTE: these values always trip overflow/upcast check

    def __init__(self):
        super().__init__(
            type_def=None,
            dtype=np.dtype(np.object_),
            na_value=pd.NaT,
            itemsize=None
        )

    ############################
    ####    TYPE METHODS    ####
    ############################

    @property
    def larger(self) -> list:
        """Get a list of types that this type can be upcasted to."""
        # start with bounded subtypes that have range wider than self
        candidates = set(self.subtypes) - {self}
        result = [
            x for x in candidates if (
                x.min <= x.max and (x.min < self.min or x.max > self.max)
            )
        ]
        result.sort(key=lambda x: x.max - x.min)

        # add subtypes that are themselves upcast-only
        others = [x for x in candidates if x.min > x.max]
        result.extend(sorted(others, key=lambda x: x.min - x.max))
        return result

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    # NOTE: because this type has no associated scalars, it will never be given
    # as the result of a detect_type() operation.  It can only be specified
    # manually, as the target of a resolve_type() call.


#####################
####    NUMPY    ####
#####################


@lru_cache(64)
@DatetimeType.register_backend("numpy")
class NumpyDatetime64Type(DatetimeMixin, AtomicType):

    aliases = {
        np.datetime64,
        # np.dtype("M8") handled in resolve_typespec_dtype special case
        "M8",
        "datetime64",
        "numpy.datetime64",
        "np.datetime64",
    }

    def __init__(self, unit: str = None, step_size: int = 1):
        if unit is None:
            dtype = np.dtype("M8")
            # NOTE: these min/max values always trigger upcast check.
            self.min = 1  # increase this to take precedence when upcasting
            self.max = 0
        else:
            dtype = np.dtype(f"M8[{step_size}{unit}]")
            # NOTE: min/max datetime64 depends on unit
            if unit == "Y":  # appears to be biased toward UTC
                min_M8 = np.datetime64(-2**63 + 1, "Y")
                max_M8 = np.datetime64(2**63 - 1 - 1970, "Y")
            elif unit == "W":  # appears almost identical to unit="D"
                min_M8 = np.datetime64((-2**63 + 1 + 10956) // 7 + 1, "W")
                max_M8 = np.datetime64((2**63 - 1 + 10956) // 7 , "W")
            elif unit == "D":
                min_M8 = np.datetime64(-2**63 + 1 + 10956, "D")  # 10956 ??
                max_M8 = np.datetime64(2**63 - 1, "D")  # unbiased ??
            else:
                min_M8 = np.datetime64(-2**63 + 1, unit)
                max_M8 = np.datetime64(2**63 - 1, unit)
            self.min = numpy_datetime64_to_ns(min_M8)
            self.max = numpy_datetime64_to_ns(max_M8)

        super().__init__(
            type_def=np.datetime64,
            dtype=dtype,
            na_value=pd.NaT,
            itemsize=8,
            unit=unit,
            step_size=step_size
        )

    ###########################
    ####   TYPE METHODS    ####
    ###########################

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

    @property
    def larger(self) -> list:
        """Get a list of types that this type can be upcasted to."""
        if self.unit is None:
            return [self.instance(unit=u) for u in valid_units]
        return []

    @classmethod
    def resolve(cls, context: str = None) -> AtomicType:
        if context is not None:
            match = M8_pattern.match(context)
            if not match:
                raise ValueError(f"invalid unit: {repr(context)}")
            unit = match.group("unit")
            step_size = int(match.group("step_size") or 1)
            return cls.instance(unit=unit, step_size=step_size)
        return cls.instance()

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def from_ns(
        self,
        series: cast.SeriesWrapper,
        rounding: str,
        tz: pytz.BaseTzInfo,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert nanosecond offsets from the given epoch into numpy
        timedelta64s with this type's unit and step size.
        """
        if tz:
            raise TypeError(
                f"np.datetime64 objects do not carry timezone information"
            )

        # convert from nanoseconds to final unit
        series.series = convert_unit(
            series.series,
            "ns",
            self.unit,
            rounding=rounding or "down"
        )
        if self.step_size != 1:
            series.series = round_div(
                series.series,
                self.step_size,
                rule=rounding or "down"
            )
        M8_str = f"M8[{self.step_size}{self.unit}]"
        return cast.SeriesWrapper(
            pd.Series(
                list(series.series.to_numpy(M8_str)),
                index=series.series.index,
                dtype="O"
            ),
            hasnans=series.hasnans,
            element_type=self
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
        downcast: bool | BaseType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert numpy datetime64s into an integer data type."""
        # NOTE: using numpy M8 array is ~2x faster than looping through series
        M8_str = f"M8[{self.step_size}{self.unit}]"
        arr = series.series.to_numpy(M8_str).view(np.int64).astype("O")
        arr *= self.step_size
        if epoch:  # apply epoch offset if not utc
            arr = convert_unit(
                arr,
                self.unit,
                "ns"
            )
            arr -= epoch.offset  # retains full ns precision from epoch
            arr = convert_unit(
                arr,
                "ns",
                unit,
                rounding=rounding or "down"
            )
        else:  # skip straight to final unit
            arr = convert_unit(
                arr,
                self.unit,
                unit,
                rounding=rounding or "down"
            )
        series = cast.SeriesWrapper(
            pd.Series(arr, index=series.series.index),
            hasnans=series.hasnans,
            element_type=resolve.resolve_type(int)
        )

        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_integer(
            series,
            dtype,
            downcast=downcast,
            errors=errors
        )


######################
####    PANDAS    ####
######################


@lru_cache(64)
@DatetimeType.register_backend("pandas")
class PandasTimestampType(DatetimeMixin, AtomicType):

    aliases = {
        pd.Timestamp,
        # pd.DatetimeTZDtype() handled in resolve_typespec_dtype special case
        "Timestamp",
        "pandas.Timestamp",
        "pd.Timestamp",
    }
    # NOTE: timezone localization can cause pd.Timestamp objects to overflow.
    # In order to account for this, we artificially reduce the available range
    # to ensure that all timezones, no matter how extreme, are representable.
    min = pd.Timestamp.min.value + 14 * as_ns["h"]  # UTC-14 is furthest ahead
    max = pd.Timestamp.max.value - 12 * as_ns["h"]  # UTC+12 is furthest behind

    def __init__(self, tz: datetime.tzinfo = None):
        if tz is None:
            dtype = np.dtype("M8[ns]")
        else:
            dtype = pd.DatetimeTZDtype(tz=tz)

        super().__init__(
            type_def=pd.Timestamp,
            dtype=dtype,
            na_value=pd.NaT,
            itemsize=8,
            tz=tz
        )

    ############################
    ####    TYPE METHODS    ####
    ############################

    @classmethod
    def slugify(cls, tz: datetime.tzinfo = None):
        slug = cls.name
        if tz is not None:
            slug += f"[{cls.backend}, {tz}]"
        else:
            slug += f"[{cls.backend}]"
        return slug

    def contains(self, other: Any) -> bool:
        other = resolve.resolve_type(other)

        # treat tz=None as wildcard
        if self.tz is None:
            if isinstance(other, CompositeType):
                return all(isinstance(o, type(self)) for o in other)
            return isinstance(other, type(self))

        return super().contains(other)

    @classmethod
    def detect(cls, example: pd.Timestamp, **defaults) -> AtomicType:
        return cls.instance(tz=example.tzinfo, **defaults)

    @classmethod
    def resolve(cls, context: str = None) -> AtomicType:
        if context is not None:
            return cls.instance(tz=pytz.timezone(context))
        return cls.instance()

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def from_ns(
        self,
        series: cast.SeriesWrapper,
        tz: pytz.BaseTzInfo,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert nanosecond offsets from the UTC epoch into pandas
        Timestamps.
        """
        # reconcile `tz` argument with timezone attached to dtype, if given
        dtype = self
        if tz:
            dtype = dtype.replace(tz=tz)

        # convert using pd.to_datetime, accounting for timezone
        if dtype.tz is None:
            result = pd.to_datetime(series.series, unit="ns")
        else:
            result = pd.to_datetime(series.series, unit="ns", utc=True)
            if dtype.tz != pytz.utc:
                result = result.dt.tz_convert(dtype.tz)

        return cast.SeriesWrapper(
            result,
            hasnans=series.hasnans,
            element_type=series.element_type
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
        downcast: bool | BaseType,
        errors: str,
        **kwargs
    ) -> cast.SeriesWrapper:
        """Convert pandas Timestamps into an integer data type."""
        # TODO: .tz_localize() after series.rectify() if self.tz is None?
        series = series.rectify().astype(np.int64)
        if epoch:
            series.series = series.series.astype("O")  # overflow-safe
            series.series -= epoch.offset

        if unit != "ns" or step_size != 1:
            convert_ns_to_unit(
                series,
                unit=unit,
                step_size=step_size,
                rounding=rounding
            )

        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_integer(
            series,
            dtype,
            downcast=downcast,
            errors=errors
        )


######################
####    PYTHON    ####
######################


@lru_cache(64)
@DatetimeType.register_backend("python")
class PythonDatetimeType(DatetimeMixin, AtomicType):

    aliases = {datetime.datetime, "pydatetime", "datetime.datetime"}
    max = pydatetime_to_ns(datetime.datetime.max)
    min = pydatetime_to_ns(datetime.datetime.min)

    def __init__(self, tz: datetime.tzinfo = None):
        super().__init__(
            type_def=datetime.datetime,
            dtype=np.dtype("O"),
            na_value=pd.NaT,
            itemsize=None,
            tz=tz
        )

    ############################
    ####    TYPE METHODS    ####
    ############################

    @classmethod
    def slugify(cls, tz: datetime.tzinfo = None):
        slug = cls.name
        if tz is not None:
            slug += f"[{cls.backend}, {tz}]"
        else:
            slug += f"[{cls.backend}]"
        return slug

    def contains(self, other: Any) -> bool:
        other = resolve.resolve_type(other)

        # treat tz=None as wildcard
        if self.tz is None:
            if isinstance(other, CompositeType):
                return all(isinstance(o, type(self)) for o in other)
            return isinstance(other, type(self))

        return super().contains(other)

    @classmethod
    def detect(cls, example: datetime.datetime, **defaults) -> AtomicType:
        return cls.instance(tz=example.tzinfo, **defaults)

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def from_ns(
        self,
        series: cast.SeriesWrapper,
        tz: pytz.BaseTzInfo,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert nanosecond offsets from the UTC epoch into python
        datetimes.
        """
        # reconcile `tz` argument with timezone attached to dtype, if given
        dtype = self
        if tz:
            dtype = dtype.replace(tz=tz)

        # convert elementwise
        call = partial(ns_to_pydatetime, tz=dtype.tz)
        series = series.apply_with_errors(call)
        series.element_type = dtype
        return series

    @dispatch
    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        epoch: Epoch,
        rounding: str,
        downcast: bool | BaseType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert python datetimes into an integer data type."""
        # TODO: use a partial to include naive tz behavior
        series = series.apply_with_errors(pydatetime_to_ns)
        series.element_type = int
        if epoch:
            series.series -= epoch.offset

        if unit != "ns" or step_size != 1:
            convert_ns_to_unit(
                series,
                unit=unit,
                step_size=step_size,
                rounding=rounding,
            )

        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_integer(
            series,
            dtype,
            downcast=downcast,
            errors=errors
        )


#######################
####    PRIVATE    ####
#######################


cdef object M8_pattern = re.compile(
    r"(?P<step_size>[0-9]+)?(?P<unit>ns|us|ms|s|m|h|D|W|M|Y)"
)

def convert_ns_to_unit(
    series: cast.SeriesWrapper,
    unit: str,
    step_size: int,
    rounding: str
) -> None:
    """Helper for converting between integer time units."""
    series.series = convert_unit(
        series.series,
        "ns",
        unit,
        rounding=rounding or "down",
    )
    if step_size != 1:
        series.series = round_div(
            series.series,
            step_size,
            rule=rounding or "down"
        )
