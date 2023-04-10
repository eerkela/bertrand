import datetime
import decimal
from functools import partial

import dateutil
import numpy as np
cimport numpy as np
import pandas as pd
import pytz
import regex as re  # using alternate python regex engine

cimport pdcast.convert as convert
import pdcast.convert as convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from pdcast.util.round cimport Tolerance
from pdcast.util.round import round_div
from pdcast.util.time cimport Epoch
from pdcast.util.time import (
    as_ns, convert_unit, filter_dateutil_parser_error, 
    is_iso_8601_format_string, iso_8601_to_ns, localize_pydatetime,
    ns_to_pydatetime, numpy_datetime64_to_ns, pydatetime_to_ns,
    string_to_pydatetime, timezone, valid_units
)
from pdcast.util.type_hints import type_specifier

from .base cimport AtomicType, CompositeType
from .base import dispatch, generic, register


# TODO: naive_tz has inconsistent behavior.
# >>> pdcast.cast("2022-03-07", "datetime[pandas, us/pacific]", tz=None, naive_tz=None)
# 0   2022-03-07 00:00:00-08:00
# dtype: datetime64[ns, US/Pacific]
# >>> pdcast.cast(pd.Timestamp("2022-03-07"), "datetime[pandas, us/pacific]", tz=None, naive_tz=None)
# 0   2022-03-07 08:00:00
# dtype: datetime64[ns]


# TODO: PandasTimestampType.from_string cannot convert quarterly dates


######################
####    MIXINS    ####
######################


class DatetimeMixin:

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def to_boolean(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        rounding: str,
        unit: str,
        step_size: int,
        since: Epoch,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert timedelta data to a boolean data type."""
        # 2-step conversion: timedelta -> decimal, decimal -> bool
        transfer_type = resolve.resolve_type("decimal")
        series = self.to_decimal(
            series,
            dtype=transfer_type,
            tol=tol,
            rounding=rounding,
            unit=unit,
            step_size=step_size,
            since=since,
            errors=errors
        )
        return transfer_type.to_boolean(
            series,
            dtype=dtype,
            tol=tol,
            rounding=rounding,
            unit=unit,
            step_size=step_size,
            since=since,
            errors=errors,
            **unused
        )

    def to_float(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        since: Epoch,
        tol: Tolerance,
        rounding: str,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert timedelta data to a floating point data type."""
        # convert to nanoseconds, then from nanoseconds to final unit
        transfer_type = resolve.resolve_type("int")
        series = self.to_integer(
            series,
            dtype=transfer_type,
            unit="ns",
            step_size=1,
            since=since,
            rounding=None,
            downcast=None,
            errors=errors
        )
        if unit != "ns" or step_size != 1:
            series.series = convert_unit(
                series.series.astype(object),
                "ns",
                unit,
                rounding=rounding,
                since=since
            )
            if step_size != 1:
                series.series /= step_size
            transfer_type = resolve.resolve_type("float[python]")

        return transfer_type.to_float(
            series,
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            since=since,
            tol=tol,
            rounding=rounding,
            downcast=downcast,
            errors=errors,
            **unused
        )

    def to_complex(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        since: Epoch,
        tol: Tolerance,
        rounding: str,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert timedelta data to a complex data type."""
        # 2-step conversion: timedelta -> float, float -> complex
        transfer_type = dtype.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            unit=unit,
            step_size=step_size,
            since=since,
            tol=tol,
            rounding=rounding,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_complex(
            series,
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            since=since,
            tol=tol,
            rounding=rounding,
            downcast=downcast,
            errors=errors,
            **unused
        )

    def to_decimal(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        since: Epoch,
        tol: Tolerance,
        rounding: str,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert timedelta data to a decimal data type."""
        # 2-step conversion: datetime -> ns, ns -> decimal
        transfer_type = resolve.resolve_type("int")
        series = self.to_integer(
            series,
            dtype=transfer_type,
            unit="ns",
            step_size=1,
            since=since,
            rounding=None,
            downcast=None,
            errors=errors
        )
        series = transfer_type.to_decimal(
            series,
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            since=since,
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
                since=since
            )
            if step_size != 1:
                series.series /= step_size

        return series

    def to_datetime(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        unit: str,
        step_size: int,
        since: Epoch,
        tz: pytz.BaseTzInfo,
        naive_tz: pytz.BaseTzInfo,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert datetime data to another datetime representation."""
        # trivial case
        if dtype == self:
            return series.rectify()

        if tz and hasattr(dtype, "tz"):
            dtype = dtype.replace(tz=tz)
        if naive_tz is None:
            if hasattr(dtype, "tz"):
                naive_tz = dtype.tz
            else:
                naive_tz = tz

        # 2-step conversion: datetime -> ns, ns -> datetime
        transfer_type = resolve.resolve_type("int")
        series = self.to_integer(
            series,
            dtype=transfer_type,
            rounding=rounding,
            unit="ns",
            step_size=1,
            since=Epoch("utc"),
            naive_tz=naive_tz,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_datetime(
            series,
            dtype=dtype,
            rounding=rounding,
            unit="ns",
            step_size=1,
            since=Epoch("utc"),
            tz=tz,
            naive_tz=naive_tz,
            errors=errors,
            **unused
        )

    def to_timedelta(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        rounding: str,
        since: Epoch,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert datetime data to a timedelta representation."""
        # 2-step conversion: datetime -> ns, ns -> timedelta
        transfer_type = resolve.resolve_type("int")
        series = self.to_integer(
            series,
            dtype=transfer_type,
            unit="ns",
            step_size=1,
            rounding=rounding,
            since=since,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_timedelta(
            series,
            dtype=dtype,
            unit="ns",
            step_size=1,
            rounding=rounding,
            since=since,
            errors=errors,
            **unused
        )

#######################
####    GENERIC    ####
#######################


@register
@generic
class DatetimeType(DatetimeMixin, AtomicType):

    # internal root fields - all subtypes/backends inherit these
    _conversion_func = convert.to_datetime

    name = "datetime"
    aliases = {"datetime"}
    dtype = None
    na_value = pd.NaT
    max = 0
    min = 1  # NOTE: these values always trip overflow/upcast check

    ############################
    ####    TYPE METHODS    ####
    ############################

    @property
    def larger(self) -> list:
        """Get a list of types that this type can be upcasted to."""
        # get candidates
        candidates = {
            x for y in self.backends.values() for x in y.subtypes if x != self
        }

        # filter off any that are upcast-only or larger than self
        result = [
            x for x in candidates if (
                x.min <= x.max and (x.min < self.min or x.max > self.max)
            )
        ]

        # sort by range
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

    def from_string(
        self,
        series: convert.SeriesWrapper,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert string data into an arbitrary datetime data type."""
        last_err = None
        for candidate in self.larger:
            try:
                return candidate.from_string(series, errors="raise", **unused)
            except OverflowError as err:
                last_err = err

        # every representation overflows - pick the last one and coerce
        if errors == "coerce":
            return candidate.from_string(
                series,
                errors=errors,
                **unused
            )
        raise last_err


#####################
####    NUMPY    ####
#####################


@register
@DatetimeType.register_backend("numpy")
class NumpyDatetime64Type(DatetimeMixin, AtomicType, cache_size=64):

    # NOTE: dtype is set to object due to pandas and its penchant for
    # automatically converting datetimes to pd.Timestamp.  Otherwise, we'd use
    # a custom ExtensionDtype/AbstractDtype or the raw numpy dtypes here.

    aliases = {
        np.datetime64,
        np.dtype("M8"),
        "M8",
        "datetime64",
        "numpy.datetime64",
        "np.datetime64",
    }
    type_def = np.datetime64
    dtype = np.dtype(object)  # workaround for above
    itemsize = 8
    na_value = pd.NaT

    def __init__(self, unit: str = None, step_size: int = 1):
        if unit is None:
            # NOTE: these min/max values always trigger upcast check.
            self.min = 1  # increase this to take precedence when upcasting
            self.max = 0
        else:
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

        super().__init__(unit=unit, step_size=step_size)

    ###########################
    ####   TYPE METHODS    ####
    ###########################

    @classmethod
    def slugify(cls, unit: str = None, step_size: int = 1) -> str:
        if unit is None:
            return f"{cls.name}[{cls._backend}]"
        if step_size == 1:
            return f"{cls.name}[{cls._backend}, {unit}]"
        return f"{cls.name}[{cls._backend}, {step_size}{unit}]"

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(
                self.contains(o, include_subtypes=include_subtypes)
                for o in other
            )

        # treat unit=None as wildcard
        if self.unit is None:
            return isinstance(other, type(self))
        return super().contains(other, include_subtypes=include_subtypes)

    @classmethod
    def detect(cls, example: np.datetime64, **defaults) -> AtomicType:
        unit, step_size = np.datetime_data(example)
        return cls.instance(unit=unit, step_size=step_size, **defaults)

    @classmethod
    def from_dtype(
        cls,
        dtype: np.dtype | pd.api.extensions.ExtensionDtype
    ) -> AtomicType:
        unit, step_size = np.datetime_data(dtype)
        return cls.instance(
            unit=None if unit == "generic" else unit,
            step_size=step_size
        )

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
        series: convert.SeriesWrapper,
        rounding: str,
        tz: pytz.BaseTzInfo,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert nanosecond offsets from the given epoch into numpy
        timedelta64s with this type's unit and step size.
        """
        if tz and tz != pytz.utc:
            raise TypeError(
                "np.datetime64 objects do not carry timezone information "
                f"(must be UTC)"
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

        # TODO: we get these as decimals rather than integers, but only
        # for units 'M' and 'Y'
        # pdcast.to_datetime([-13.8], "datetime[numpy]", unit="Y")

        M8_str = f"M8[{self.step_size}{self.unit}]"
        return convert.SeriesWrapper(
            pd.Series(
                list(series.series.to_numpy(M8_str)),
                index=series.series.index,
                dtype="O"
            ),
            hasnans=series.hasnans,
            element_type=self
        )

    def from_string(
        self,
        series: convert.SeriesWrapper,
        format: str,
        tz: pytz.BaseTzInfo,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert ISO 8601 strings to a numpy datetime64 data type."""
        # 2-step conversion: string -> ns, ns -> datetime64
        if format and not is_iso_8601_format_string(format):
            raise TypeError(
                f"np.datetime64 strings must be in ISO 8601 format"
            )
        if tz and tz != pytz.utc:
            raise TypeError(
                "np.datetime64 objects do not carry timezone information"
            )

        transfer_type = resolve.resolve_type("int[python]")
        series = series.apply_with_errors(
            iso_8601_to_ns,
            errors=errors,
            element_type=transfer_type
        )
        return transfer_type.to_datetime(
            series,
            format=format,
            tz=tz,
            errors=errors,
            **unused
        )

    def to_integer(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        unit: str,
        step_size: int,
        since: Epoch,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert numpy datetime64s into an integer data type."""
        # NOTE: using numpy M8 array is ~2x faster than looping through series
        M8_str = f"M8[{self.step_size}{self.unit}]"
        arr = series.series.to_numpy(M8_str).view(np.int64).astype(object)
        arr *= self.step_size
        if since:  # apply epoch offset if not utc
            arr = convert_unit(
                arr,
                self.unit,
                "ns"
            )
            arr -= since.offset  # retains full ns precision from epoch
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
        series = convert.SeriesWrapper(
            pd.Series(arr, index=series.series.index),
            hasnans=series.hasnans,
            element_type=resolve.resolve_type("int[python]")
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


@register
@DatetimeType.register_backend("pandas")
class PandasTimestampType(DatetimeMixin, AtomicType, cache_size=64):

    aliases = {
        pd.Timestamp,
        pd.DatetimeTZDtype,
        "Timestamp",
        "pandas.Timestamp",
        "pd.Timestamp",
    }
    # NOTE: timezone localization can cause pd.Timestamp objects to overflow.
    # In order to account for this, we artificially reduce the available range
    # to ensure that all timezones, no matter how extreme, are representable.
    itemsize = 8
    na_value = pd.NaT
    type_def = pd.Timestamp
    min = pd.Timestamp.min.value + 14 * as_ns["h"]  # UTC-14 is furthest ahead
    max = pd.Timestamp.max.value - 12 * as_ns["h"]  # UTC+12 is furthest behind

    def __init__(self, tz: datetime.tzinfo | str = None):
        tz = timezone(tz)
        super().__init__(tz=tz)

    ########################
    ####    REQUIRED    ####
    ########################

    @property
    def dtype(self) -> np.dtype | pd.api.extensions.ExtensionDtype:
        if self.tz is None:
            return np.dtype("M8[ns]")
        return pd.DatetimeTZDtype(tz=self.tz)

    ############################
    ####    TYPE METHODS    ####
    ############################

    @classmethod
    def slugify(cls, tz: datetime.tzinfo | str = None):
        if tz is None:
            return f"{cls.name}[{cls._backend}]"
        return f"{cls.name}[{cls._backend}, {str(tz))}]"

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(
                self.contains(o, include_subtypes=include_subtypes)
                for o in other
            )

        # treat tz=None as wildcard
        if self.tz is None:
            return isinstance(other, type(self))
        return super().contains(other, include_subtypes=include_subtypes)

    @classmethod
    def detect(cls, example: pd.Timestamp, **defaults) -> AtomicType:
        return cls.instance(tz=example.tzinfo, **defaults)

    @classmethod
    def from_dtype(
        cls,
        dtype: np.dtype | pd.api.extensions.ExtensionDtype
    ) -> AtomicType:
        return cls.instance(tz=getattr(dtype, "tz", None))

    @classmethod
    def resolve(cls, context: str = None) -> AtomicType:
        if context is not None:
            return cls.instance(tz=timezone(context))
        return cls.instance()

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def from_ns(
        self,
        series: convert.SeriesWrapper,
        tz: pytz.BaseTzInfo,
        **unused
    ) -> convert.SeriesWrapper:
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

        return convert.SeriesWrapper(
            result,
            hasnans=series.hasnans,
            element_type=dtype
        )

    def from_string(
        self,
        series: convert.SeriesWrapper,
        tz: pytz.BaseTzInfo,
        format: str,
        naive_tz: pytz.BaseTzInfo,
        day_first: bool,
        year_first: bool,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert datetime strings into pandas Timestamps."""
        # reconcile `tz` argument with timezone attached to dtype, if given
        dtype = self
        if tz:
            dtype = dtype.replace(tz=tz)

        # configure kwargs for pd.to_datetime
        utc = naive_tz == pytz.utc or naive_tz is None and dtype.tz == pytz.utc
        kwargs = {
            "dayfirst": day_first,
            "yearfirst": year_first,
            "utc": utc,
            "errors": "raise" if errors == "ignore" else errors
        }
        if format:
            kwargs |= {"format": format, "exact": False}

        # NOTE: pd.to_datetime() can throw lots of different exceptions, not
        # all of which are immediately clear.  For the sake of simplicity, we
        # catch and re-raise these only as ValueErrors or OverflowErrors.
        # Raising from None truncates stack traces, which can get quite long.
        try:
            result = pd.to_datetime(series.series, **kwargs)

        # exception 1: outside pd.Timestamp range, but within datetime.datetime
        except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime as err:
            raise OverflowError(str(err)) from None  # truncate stack

        # exception 2: bad string or outside datetime.datetime range
        except dateutil.parser.ParserError as err:  # ambiguous
            raise filter_dateutil_parser_error(err) from None  # truncate stack

        # account for missing values introduced during error coercion
        hasnans = series.hasnans
        if errors == "coerce":
            isna = result.isna()
            hasnans = isna.any()
            result = result[~isna]

        # localize to final timezone
        try:
            # NOTE: if utc=False and there are mixed timezones and/or mixed
            # aware/naive strings in the input series, the output of
            # pd.to_datetime() could be malformed.
            if utc:  # simple - convert to final tz
                if dtype.tz != pytz.utc:
                    result = result.dt.tz_convert(dtype.tz)
            else:
                # homogenous - either naive or consistent timezone
                if pd.api.types.is_datetime64_ns_dtype(result):
                    if not result.dt.tz:  # naive
                        if not naive_tz:  # localize directly
                            result = result.dt.tz_localize(dtype.tz)
                        else:  # localize, then convert
                            result = result.dt.tz_localize(naive_tz)
                            result = result.dt.tz_convert(dtype.tz)
                    else:  # aware
                        result = result.dt.tz_convert(dtype.tz)

                # non-homogenous - either mixed timezone or mixed aware/naive
                else:
                    # NOTE: pd.to_datetime() sacrifices ns precision here
                    localize = partial(
                        localize_pydatetime,
                        tz=dtype.tz,
                        naive_tz=naive_tz
                    )
                    # NOTE: np.frompyfunc() implicitly casts to pd.Timestamp
                    result = np.frompyfunc(localize, 1, 1)(result)

        # exception 3: overflow induced by timezone localization
        except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime as err:
            raise OverflowError(str(err)) from None

        return convert.SeriesWrapper(
            result,
            hasnans=hasnans,
            element_type=dtype
        )

    def to_integer(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        unit: str,
        step_size: int,
        since: Epoch,
        naive_tz: pytz.BaseTzInfo,
        downcast: CompositeType,
        errors: str,
        **kwargs
    ) -> convert.SeriesWrapper:
        """Convert pandas Timestamps into an integer data type."""
        # convert to ns
        series = series.rectify()
        if naive_tz:
            series.series = series.series.dt.tz_localize(naive_tz)
        series = series.astype(np.int64)

        # apply epoch
        if since:
            series.series = series.series.astype("O")  # overflow-safe
            series.series -= since.offset

        # convert ns to final unit
        if unit != "ns" or step_size != 1:
            convert_ns_to_unit(
                series,
                unit=unit,
                step_size=step_size,
                rounding=rounding
            )

        # boundscheck and convert to final integer representation
        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_integer(
            series,
            dtype,
            downcast=downcast,
            errors=errors
        )

    def to_datetime(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tz: pytz.BaseTzInfo,
        naive_tz: pytz.BaseTzInfo,
        **unused
    ) -> convert.SeriesWrapper:
        """Specialized for same-type conversions."""
        # fastpath for same-class datetime conversions
        if type(dtype) == type(self):
            series = series.rectify()
            if tz:
                dtype = dtype.replace(tz=tz)
            if dtype.tz != self.tz:
                if not self.tz:
                    if not naive_tz:
                        result = series.series.dt.tz_localize(dtype.tz)
                    else:
                        result = series.series.dt.tz_localize(naive_tz)
                    series = convert.SeriesWrapper(
                        result,
                        hasnans=series.hasnans,
                        element_type=self.replace(tz=naive_tz)
                    )
                series = convert.SeriesWrapper(
                    series.series.dt.tz_convert(dtype.tz),
                    hasnans=series.hasnans,
                    element_type=dtype
                )
            return series

        return super().to_datetime(
            series,
            dtype=dtype,
            tz=tz,
            naive_tz=naive_tz,
            **unused
        )

    @dispatch(namespace="dt")
    def tz_convert(
        self,
        series: convert.SeriesWrapper,
        tz: str | datetime.tzinfo,
        *args,
        **kwargs
    ) -> convert.SeriesWrapper:
        """Convert python datetime objects to the specified timezone."""
        series = series.rectify()
        tz = timezone(tz)

        # pass to original .dt.tz_convert() implementation
        return convert.SeriesWrapper(
            series.dt.tz_convert.original(tz, *args, **kwargs),
            hasnans=series.hasnans,
            element_type=self.replace(tz=tz)
        )

    @dispatch(namespace="dt")
    def tz_localize(
        self,
        series: convert.SeriesWrapper,
        tz: str | datetime.tzinfo,
        *args,
        **kwargs
    ) -> convert.SeriesWrapper:
        """Localize python datetime objects to the specified timezone."""
        series = series.rectify()
        tz = timezone(tz)

        # pass to original .dt.tz_localize() implementation
        return convert.SeriesWrapper(
            series.series.dt.tz_localize.original(tz, *args, **kwargs),
            hasnans=series.hasnans,
            element_type=self.replace(tz=tz)
        )


######################
####    PYTHON    ####
######################


@register
@DatetimeType.register_backend("python")
class PythonDatetimeType(DatetimeMixin, AtomicType, cache_size=64):

    aliases = {datetime.datetime, "pydatetime", "datetime.datetime"}
    na_value = pd.NaT
    type_def = datetime.datetime
    max = pydatetime_to_ns(datetime.datetime.max)
    min = pydatetime_to_ns(datetime.datetime.min)

    def __init__(self, tz: datetime.tzinfo = None):
        tz = timezone(tz)
        super().__init__(tz=tz)

    ############################
    ####    TYPE METHODS    ####
    ############################

    @classmethod
    def slugify(cls, tz: datetime.tzinfo = None):
        if tz is None:
            return f"{cls.name}[{cls._backend}]"
        return f"{cls.name}[{cls._backend}, {str(tz))}]"

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(
                self.contains(o, include_subtypes=include_subtypes)
                for o in other
            )

        # treat tz=None as wildcard
        if self.tz is None:
            return isinstance(other, type(self))
        return super().contains(other, include_subtypes=include_subtypes)

    @classmethod
    def detect(cls, example: datetime.datetime, **defaults) -> AtomicType:
        return cls.instance(tz=example.tzinfo, **defaults)

    @classmethod
    def resolve(cls, context: str = None) -> AtomicType:
        if context is not None:
            return cls.instance(tz=timezone(context))
        return cls.instance()

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def from_ns(
        self,
        series: convert.SeriesWrapper,
        tz: pytz.BaseTzInfo,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert nanosecond offsets from the UTC epoch into python
        datetimes.
        """
        # reconcile `tz` argument with timezone attached to dtype, if given
        dtype = self
        if tz:
            dtype = dtype.replace(tz=tz)

        # convert elementwise
        call = partial(ns_to_pydatetime, tz=dtype.tz)
        return series.apply_with_errors(call, element_type=dtype)

    def from_string(
        self,
        series: convert.SeriesWrapper,
        tz: pytz.BaseTzInfo,
        naive_tz: pytz.BaseTzInfo,
        day_first: bool,
        year_first: bool,
        format: str,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert strings into datetime objects."""
        # reconcile `tz` argument with timezone attached to dtype, if given
        dtype = self
        if tz:
            dtype = dtype.replace(tz=tz)

        # set up dateutil parserinfo
        parser_info = dateutil.parser.parserinfo(
            dayfirst=day_first,
            yearfirst=year_first
        )

        # apply elementwise
        return series.apply_with_errors(
            partial(
                string_to_pydatetime,
                format=format,
                parser_info=parser_info,
                tz=dtype.tz,
                naive_tz=naive_tz,
                errors=errors
            ),
            errors=errors,
            element_type=dtype
        )

    def to_integer(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        since: Epoch,
        rounding: str,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert python datetimes into an integer data type."""
        series = series.apply_with_errors(
            pydatetime_to_ns,
            element_type=resolve.resolve_type("int[python]")
        )
        if since:
            series.series -= since.offset

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

    def to_datetime(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tz: pytz.BaseTzInfo,
        **unused
    ) -> convert.SeriesWrapper:
        """Specialized for same-type conversions."""
        # fastpath for same-class datetime conversions
        if type(dtype) == type(self):
            if tz:
                dtype = dtype.replace(tz=tz)
            if dtype.tz != self.tz:
                if not self.tz:
                    series = self.tz_localize(series, "UTC")
                return self.tz_convert(series, dtype.tz)

        return super().to_datetime(series, dtype=dtype, tz=tz, **unused)

    @dispatch(namespace="dt")
    def tz_convert(
        self,
        series: convert.SeriesWrapper,
        tz: str | pytz.BaseTzInfo
    ) -> convert.SeriesWrapper:
        """Convert python datetime objects to the specified timezone."""
        if not self.tz:
            # NOTE: matches error thrown by pandas
            raise TypeError(
                f"Cannot convert tz-naive pydatetimes, use tz_localize to "
                f"localize"
            )

        tz = timezone(tz)

        # iterate elementwise
        localize = partial(localize_pydatetime, tz=tz, utc=True)
        return series.apply_with_errors(
            localize,
            errors="raise",
            element_type=self.replace(tz=tz)
        )

    @dispatch(namespace="dt")
    def tz_localize(
        self,
        series: convert.SeriesWrapper,
        tz: str | pytz.BaseTzInfo
    ) -> convert.SeriesWrapper:
        """Localize python datetime objects to the specified timezone."""
        if tz is not None and self.tz is not None:
            # NOTE: matches error thrown by pandas
            raise TypeError("Already tz-aware, use tz_convert to convert.")

        tz = timezone(tz)

        # iterate elementwise
        localize = partial(localize_pydatetime, tz=tz, naive_tz=None)
        return series.apply_with_errors(
            localize,
            errors="raise",
            element_type=self.replace(tz=tz)
        )


#######################
####    PRIVATE    ####
#######################


cdef object M8_pattern = re.compile(
    r"(?P<step_size>[0-9]+)?(?P<unit>ns|us|ms|s|m|h|D|W|M|Y)"
)


def convert_ns_to_unit(
    series: convert.SeriesWrapper,
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
