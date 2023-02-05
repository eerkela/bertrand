import datetime
import decimal
from types import MappingProxyType
from typing import Any, Union, Sequence

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd
import regex as re  # using alternate python regex engine

from .base cimport AtomicType, CompositeType
from .base import dispatch, generic, lru_cache

from pdtypes.util.round cimport Tolerance

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


# TODO: import timedelta helpers from pdtypes.util.time
# TODO: update type hints from Any to datetime_like, timezone_like, etc.

# TODO: parse() should account for self.unit/step_size/timezone

# TODO: add min/max?
# -> these could potentially replace the constants in util/time/


######################
####    MIXINS    ####
######################


class TimedeltaMixin:

    @dispatch
    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        epoch: np.datetime64,
        rounding: str,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert timedelta data to a boolean data type."""
        # 2-step conversion: timedelta -> int, int -> bool
        result = self.to_integer(
            series,
            resolve.resolve_type(int),
            unit=unit,
            step_size=step_size,
            epoch=epoch,
            rounding=rounding,
            downcast=False,
            errors=errors
        )
        return result.to_boolean(
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            epoch=epoch,
            rounding=rounding,
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
        epoch: np.datetime64,
        tol: Tolerance,
        rounding: str,
        downcast: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert timedelta data to a floating point data type."""
        # convert to nanoseconds, then from nanoseconds to final unit
        result = self.to_integer(
            series,
            resolve.resolve_type(int),
            unit="ns",
            step_size=1,
            epoch=epoch,
            rounding=None,
            downcast=False,
            errors=errors
        )

        # TODO: pass through time.convert_unit(
        #     result,
        #     "ns",
        #     unit,
        #     step_size=step_size,
        #     rounding=rounding,
        #     epoch=epoch.
        #     return_type="float"
        # )

        return result.to_float(
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
        epoch: np.datetime64,
        tol: Tolerance,
        rounding: str,
        downcast: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert timedelta data to a complex data type."""
        # 2-step conversion: timedelta -> float, float -> complex
        result = self.to_float(
            series,
            dtype.equiv_float,
            unit=unit,
            step_size=step_size,
            epoch=epoch,
            rounding=rounding,
            downcast=False,
            errors=errors
        )
        return result.to_complex(
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
        epoch: np.datetime64,
        tol: Tolerance,
        rounding: str,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert timedelta data to a decimal data type."""
        # convert to nanoseconds, then from nanoseconds to final unit
        result = self.to_integer(
            series,
            resolve.resolve_type(int),
            unit="ns",
            step_size=1,
            epoch=epoch,
            rounding=None,
            downcast=False,
            errors=errors
        )

        # TODO: pass through time.convert_unit(
        #     result,
        #     "ns",
        #     unit,
        #     step_size=step_size,
        #     rounding=rounding,
        #     epoch=epoch.
        #     return_type="decimal"
        # )

        return result.to_decimal(
            dtype=dtype,
            unit=unit,
            step_size=step_size,
            epoch=epoch,
            tol=tol,
            rounding=rounding,
            errors=errors,
            **unused
        )

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
        result = self.to_integer(
            series,
            resolve.resolve_type(int),
            unit="ns",
            step_size=1,
            epoch=np.datetime64(0, "s"),
            rounding=None,
            downcast=False,
            errors=errors
        )
        return result.to_timedelta(
            dtype=dtype,
            errors=errors,
            **unused
        )

    # TODO: to_string with format?


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


######################
####    PANDAS    ####
######################


@TimedeltaType.register_backend("pandas")
class PandasTimedeltaType(TimedeltaMixin, AtomicType):

    aliases = {pd.Timedelta, "Timedelta", "pandas.Timedelta", "pd.Timedelta"}
    max = 2**63 - 1
    min = -2**63 + 1  # -2**63 reserved for NaT

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
        epoch: np.datetime64,
        rounding: str,
        downcast: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert pandas Timedeltas to an integer data type."""
        if pd.api.types.is_timedelta64_ns_dtype(series.series):
            series = series.astype(np.int64)
        else:
            series = series.apply_with_errors(lambda x: x.value)
            series.element_type = int

        if unit != "ns" or step_size != 1:
            raise NotImplementedError()
            # TODO: pass through time.convert_unit(
            #     series,
            #     "ns",
            #     unit,
            #     step_size=step_size,
            #     since=epoch,
            #     rounding=rounding,
            #     return_type="int"
            # )

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
        epoch: np.datetime64,
        rounding: str,
        downcast: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert python timedeltas to an integer data type."""
        series = series.apply_with_errors(pytimedelta_to_ns)
        series.element_type = int

        if unit != "ns" or step_size != 1:
            raise NotImplementedError()
            # TODO: pass through time.convert_unit(
            #     series,
            #     "ns",
            #     unit,
            #     step_size=step_size,
            #     since=epoch,
            #     rounding=rounding,
            #     return_type="int"
            # )

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


cdef np.ndarray pytimedelta_ns_coefs = np.array(
    [24 * 60 * 60 * 10**9, 10**9, 10**3],
    dtype="O"
)


cdef object pytimedelta_to_ns(object delta):
    """Convert a python timedelta into an integer number of nanoseconds."""
    return np.dot(
        [delta.days, delta.seconds, delta.microseconds],
        pytimedelta_ns_coefs
    )
