from contextlib import contextmanager
import decimal
from typing import Any, Callable, Iterable, Iterator

cimport numpy as np
import numpy as np
import pandas as pd

cimport pdtypes.types.atomic as atomic
import pdtypes.types.atomic as atomic
cimport pdtypes.types.detect as detect
import pdtypes.types.detect as detect
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve

from pdtypes.type_hints import array_like, numeric
from pdtypes.util.round import Tolerance


# TODO: downcast flag should accept resolvable type specifiers as well as
# booleans.  If a non-boolean value is given, it will not downcast below the
# specified type.


# TODO: sparse types currently broken

# TODO: top-level to_x() functions are respondible for input validation

# TODO: have to account for empty series in each conversion.


# TODO: every conversion returns a SeriesWrapper instance, which allows them
# to be interoperable.
# SeriesWrapper.to_float(self, dtype, ...) -> SeriesWrapper:
#     return self.element_type.to_float(series=self, dtype=dtype, ...)

# then you can just stack 2-step conversions, like so:
# IntegerType.to_complex():
#     result = self.to_float(equiv_float, ...)
#     return result.to_complex(dtype, ...)


# TODO: add a reject_non_integer argument to snap_round that will throw a
# precision loss error if the results of the operation are not exact integer
# values.  This saves on some boilerplate when doing float conversions.

# round, snap, and snap_round should return SeriesWrappers to make them
# compatible with dispatch().


######################
####    PUBLIC    ####
######################


def cast(
    series: Iterable,
    dtype: resolve.resolvable = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to the given data type."""
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    return dtype.conversion_func(series, dtype, **kwargs)


def to_boolean(
    series: Iterable,
    dtype: resolve.resolvable = bool,
    rounding: str = None,
    tol: int | float | complex | decimal.Decimal = 1e-6,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to boolean representation."""
    # validate dtype
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.unwrap().is_subtype(atomic.BooleanType):
        raise ValueError(f"`dtype` must be a boolean type, not {dtype}")

    # TODO: collate and validate args

    return do_conversion(
        series,
        "to_boolean",  # not passed to conversion method
        dtype=dtype,
        rounding=rounding,
        tol=tol,
        errors=errors,
        **kwargs
    )


def to_integer(
    series: Iterable,
    dtype: resolve.resolvable = int,
    rounding: str = None,
    tol: numeric | Tolerance = 1e-6,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to integer representation."""
    # validate dtype
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.unwrap().is_subtype(atomic.IntegerType):
        raise ValueError(f"`dtype` must be an integer type, not {dtype}")

    # validate tolerance
    if not isinstance(tol, Tolerance):
        tol = Tolerance(tol)

    # TODO: collate and validate args

    return do_conversion(
        series,
        "to_integer",  # not passed to conversion method
        dtype=dtype,
        rounding=rounding,
        tol=tol,
        errors=errors,
        **kwargs
    )


def to_float(
    series: Iterable,
    dtype: resolve.resolvable = float,
    tol: int | float | complex | decimal.Decimal = 1e-6,
    downcast: bool = False,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to float representation."""
    # validate dtype
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.FloatType):
        raise ValueError(f"`dtype` must be a float type, not {dtype}")

    # TODO: collate and validate args

    return do_conversion(
        series,
        "to_float",  # not passed to conversion method
        dtype=dtype,
        tol=tol,
        downcast=downcast,
        errors=errors,
        **kwargs
    )


def to_complex(
    series: Iterable,
    dtype: resolve.resolvable = complex,
    tol: int | float | complex | decimal.Decimal = 1e-6,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to complex representation."""
    # validate dtype
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.ComplexType):
        raise ValueError(f"`dtype` must be a complex type, not {dtype}")

    # TODO: collate and validate args

    return do_conversion(
        series,
        "to_complex",  # not passed to conversion method
        dtype=dtype,
        tol=tol,
        errors=errors,
        **kwargs
    )


def to_decimal(
    series: Iterable,
    dtype: resolve.resolvable = decimal.Decimal,
    rounding: str = None,
    tol: int | float | complex | decimal.Decimal = 1e-6,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to decimal representation."""
    # validate dtype
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.DecimalType):
        raise ValueError(f"`dtype` must be a decimal type, not {dtype}")

    # TODO: collate and validate args

    return do_conversion(
        series,
        "to_decimal",  # not passed to conversion method
        dtype=dtype,
        rounding=rounding,
        tol=tol,
        errors=errors,
        **kwargs
    )


def to_datetime(
    series: Iterable,
    dtype: resolve.resolvable = "datetime",
    rounding: str = None,
    tol: int | float | complex | decimal.Decimal = 1e-6,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to datetime representation."""
    # validate dtype
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.DatetimeType):
        raise ValueError(f"`dtype` must be a datetime type, not {dtype}")

    # TODO: collate and validate args

    return do_conversion(
        series,
        "to_datetime",  # not passed to conversion method
        dtype=dtype,
        rounding=rounding,
        tol=tol,
        errors=errors,
        **kwargs
    )


def to_timedelta(
    series: Iterable,
    dtype: resolve.resolvable = "timedelta",
    rounding: str = None,
    tol: int | float | complex | decimal.Decimal = 1e-6,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to timedelta representation."""
    # validate dtype
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.TimedeltaType):
        raise ValueError(f"`dtype` must be a timedelta type, not {dtype}")

    # TODO: collate and validate args

    return do_conversion(
        series,
        "to_timedelta",  # not passed to conversion method
        dtype=dtype,
        rounding=rounding,
        tol=tol,
        errors=errors,
        **kwargs
    )


def to_string(
    series: Iterable,
    dtype: resolve.resolvable = str,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # validate dtype
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.StringType):
        raise ValueError(f"`dtype` must be a string type, not {dtype}")

    # TODO: collate and validate args

    return do_conversion(
        series,
        "to_string",  # not passed to conversion method
        dtype=dtype,
        errors=errors,
        **kwargs
    )


def to_object(
    series: Iterable,
    dtype: resolve.resolvable = object,
    call: Callable = None,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # validate dtype
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.ObjectType):
        raise ValueError(f"`dtype` must be an object type, not {dtype}")

    # TODO: collate and validate args

    return do_conversion(
        series,
        "to_object",  # not passed to conversion method
        dtype=dtype,
        call=call,
        errors=errors,
        **kwargs
    )


######################
####    PRIVATE   ####
######################


cdef tuple _apply_with_errors(
    np.ndarray[object] arr,
    object call,
    str errors
):
    """Apply a function over an object array using the given error-handling
    rule.
    """
    cdef unsigned int arr_length = arr.shape[0]
    cdef unsigned int i
    cdef np.ndarray[object] result = np.full(arr_length, None, dtype="O")
    cdef bint has_errors = False
    cdef np.ndarray[np.uint8_t, cast=True] index

    # index is only necessary if errors="coerce"
    if errors == "coerce":
        index = np.full(arr_length, False)
    else:
        index = None

    # apply `call` at every index of array and record errors
    for i in range(arr_length):
        try:
            result[i] = call(arr[i])
        except (KeyboardInterrupt, MemoryError, SystemError, SystemExit):
            raise  # never coerce on these error types
        except Exception as err:
            if errors == "coerce":
                has_errors = True
                index[i] = True
                continue
            raise err

    return result, has_errors, index


def as_series(data) -> pd.Series:
    """Convert the given data into a corresponding pd.Series object."""
    if isinstance(data, pd.Series):
        return data.copy()

    if isinstance(data, np.ndarray):
        return pd.Series(np.atleast_1d(data))

    return pd.Series(data, dtype="O")


def do_conversion(
    data,
    endpoint: str,
    *args,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    try:
        with SeriesWrapper(as_series(data)) as series:
            result = getattr(series, endpoint)(*args, errors=errors, **kwargs)
            series.series = result.series
            series.hasnans = result.hasnans
            series.element_type = result.element_type
        return series.series
    except (KeyboardInterrupt, MemoryError, SystemError, SystemExit):
        raise  # never ignore these errors
    except Exception as err:
        if errors == "ignore":
            return data
        raise err

def within_tolerance(series_1, series_2, tol: numeric) -> array_like:
    """Check if every element of a series is within tolerance of another
    series.
    """
    if not tol:  # fastpath if tolerance=0
        return series_1 == series_2
    return ~((series_1 - series_2).abs() > tol)


cdef class SeriesWrapper:
    """Base wrapper for pd.Series objects.

    Implements a dynamic wrapper according to the Gang of Four's Decorator
    Pattern (not to be confused with python decorators).
    """

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        element_type: atomic.BaseType = None
    ):
        if not isinstance(series, pd.Series):
            raise TypeError(
                f"`series` must be a pd.Series object, not {type(series)}"
            )
        self.series = series
        self.size = len(self.series)
        self.hasnans = hasnans
        self.element_type = element_type

    ##########################
    ####    PROPERTIES    ####
    ##########################

    @property
    def element_type(self) -> atomic.BaseType:
        if self._element_type is None:
            self._element_type = detect.detect_type(self.dropna())
        return self._element_type

    @element_type.setter
    def element_type(self, val: atomic.BaseType) -> None:
        if (
            isinstance(val, atomic.CompositeType) and
            getattr(val.index, "shape", None) != self.shape
        ):
            raise ValueError(
                f"`element_type.index` must have the same shape as the series "
                f"it describes"
            )
        self._element_type = val

    @property
    def hasinfs(self) -> bool:
        """Check whether a wrapped series contains infinities."""
        # TODO: delete this?
        if self._hasinfs is None:
            self._hasinfs = self.isinf().any()
        return self._hasinfs

    @hasinfs.setter
    def hasinfs(self, val: bool) -> None:
        # TODO: delete this?
        self._hasinfs = val

    @property
    def hasnans(self) -> bool:
        """Check whether a wrapped series contains missing values."""
        if self._hasnans is None:
            self._hasnans = self.isna().any()
        return self._hasnans

    @hasnans.setter
    def hasnans(self, val: bool) -> None:
        self._hasnans = val

    @property
    def imag(self) -> SeriesWrapper:
        """Get the imaginary component of a wrapped series."""
        if self.cache.get("imag", None) is None:
            # NOTE: np.imag() fails when applied over object arrays that may
            # contain complex values.  In this case, we reduce it to a loop.
            if pd.api.types.is_object_dtype(self.series):
                result = np.frompyfunc(np.imag, 1, 1)(self.series)
            else:
                result = pd.Series(np.imag(self.series), index=self.index)

            target = getattr(
                self.element_type,
                "equiv_float",
                self.element_type
            )
            self.cache["imag"] = SeriesWrapper(result, element_type=target)
        return self.cache["imag"]

    @property
    def real(self) -> SeriesWrapper:
        """Get the real component of a wrapped series."""
        if self.cache.get("None", None) is None:
            # NOTE: np.real() fails when applied over object arrays that may
            # contain complex values.  In this case, we reduce it to a loop.
            if pd.api.types.is_object_dtype(self.series):
                result = np.frompyfunc(np.real, 1, 1)(self.series)
            else:
                result = pd.Series(np.real(self.series), index=self.index)

            target = getattr(
                self.element_type,
                "equiv_float",
                self.element_type
            )
            self.cache["real"] = SeriesWrapper(result, element_type=target)
        return self.cache["real"]

    @property
    def series(self) -> pd.Series:
        return self._series

    @series.setter
    def series(self, val: pd.Series) -> None:
        if not isinstance(val, pd.Series):
            raise TypeError(
                f"`series` must be a pandas Series object, not {type(val)}"
            )
        self._series = val
        self.cache = {}

    ###############################
    ####    WRAPPED METHODS    ####
    ###############################

    def argmax(self, *args, **kwargs):
        """Alias for IntegerSeries.max()."""
        return self.max(*args, **kwargs)

    def argmin(self, *args, **kwargs):
        """Alias for IntegerSeries.min()."""
        return self.min(*args, **kwargs)

    def astype(
        self,
        dtype: atomic.AtomicType,
        errors: str = "raise"
    ) -> SeriesWrapper:
        """`astype()` equivalent for SeriesWrapper instances that works for
        object-based type specifiers.
        """
        # apply dtype.type_def elementwise if not astype-compliant
        if dtype.unwrap().dtype == np.dtype("O"):
            result = self.apply_with_errors(
                call=dtype.type_def,
                errors=errors
            )
            result.element_type = dtype
            return result

        # default to pd.Series.astype()
        if pd.api.types.is_object_dtype(self) and dtype.backend == "pandas":
            # NOTE: pandas doesn't like converting arbitrary objects to
            # nullable extension types.  Luckily, numpy has no such problem,
            # and SeriesWrapper automatically filters out NAs.
            target = dtype.dtype
            result = self.series.astype(target.numpy_dtype).astype(target)
        else:
            result = self.series.astype(dtype.dtype, copy=False)

        return SeriesWrapper(
            result,
            hasnans=self.hasnans,
            element_type=dtype
        )

    def idxmax(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.idxmax()."""
        if self.cache.get("idxmax", None) is None:
            self.cache["idxmax"] = self.series.idxmax(*args, **kwargs)
            self.cache["max"] = self.series[self.cache["idxmax"]]
        return self.cache["idxmax"]

    def idxmin(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.idxmin()."""
        if self.cache.get("idxmin", None) is None:
            self.cache["idxmin"] = self.series.idxmin(*args, **kwargs)
            self.cache["min"] = self.series[self.cache["idxmin"]]
        return self.cache["idxmin"]

    def max(self, *args, **kwargs):
        """A cached version of pd.Series.max()."""
        if self.cache.get("max", None) is None:
            self.cache["max"] = self.series.max(*args, **kwargs)
        return self.cache["max"]

    def min(self, *args, **kwargs):
        """A cached version of pd.Series.min()."""
        if self.cache.get("min", None) is None:
            self.cache["min"] = self.series.min(*args, **kwargs)
        return self.cache["min"]

    ###########################
    ####    NEW METHODS    ####
    ###########################

    def __enter__(self) -> SeriesWrapper:
        # normalize index
        if not isinstance(self.series.index, pd.RangeIndex):
            self.original_index = self.series.index
            self.series.index = pd.RangeIndex(0, self.size)

        # drop missing values
        is_na = self.isna()
        self.hasnans = is_na.any()
        if self._hasnans:
            self.series = self.series[~is_na]

        # detect element type if not set manually
        if self._element_type is None:
            self.element_type = detect.detect_type(self.series)

        # enter context block
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        # replace missing values, aligning on index
        if self.hasnans:
            result = pd.Series(
                np.full(self.size, self.element_type.na_value, dtype="O"),
                dtype=self.dtype
            )
            result.update(self.series)
            self.series = result

        # replace original index
        if self.original_index is not None:
            self.series.index = self.original_index

    def apply_with_errors(self, call: Callable, errors: str) -> SeriesWrapper:
        """Apply `call` over the series, applying the specified error handling
        rule at each index.
        """
        result, has_errors, index = _apply_with_errors(
            self.to_numpy(dtype="O"),
            call=call,
            errors=errors
        )
        result = pd.Series(result, index=self.index, dtype="O")
        if has_errors:
            result = result[~index]
        return SeriesWrapper(
            result,
            hasnans=has_errors or self.hasnans
        )

    def dispatch(self, endpoint: str, *args, **kwargs) -> SeriesWrapper:
        """Apply an AtomicType method over the series.

        If the series is non-homogenous (contains elements of more than one
        type), then each type is processed separately.  The dispatched method
        must accept a SeriesWrapper as its first argument, and any additional
        arguments to this function are passed directly to it.
        """
        # group by AtomicType and transform
        if isinstance(self.element_type, atomic.CompositeType):
            groups = self.groupby(self.element_type.index, sort=False)
            result = groups.transform(lambda grp: getattr(grp.name, endpoint)(
                SeriesWrapper(
                    grp,
                    hasnans=self.hasnans,
                    element_type=grp.name
                ),
                *args,
                **kwargs
            ).series)

        # no grouping necessary, delegate directly to AtomicType
        else:
            result = getattr(self.element_type, endpoint)(
                self,
                *args,
                **kwargs
            ).series

        # construct new SeriesWrapper with results
        hasnans = self.hasnans
        if len(result) < self.size:
            hasnans = True  # account for coerced nans
        return SeriesWrapper(
            result,
            hasnans=hasnans
        )

    def isinf(self) -> pd.Series:
        """TODO"""
        return self.isin([np.inf, -np.inf])

    def rectify(self) -> pd.Series:
        """Convert an improperly-formatted object series to a standardized
        numpy/pandas data type.
        """
        # TODO: deprecate this
        if (
            pd.api.types.is_object_dtype(self) and
            self.element_type.dtype != np.dtype("O")
        ):
            return self.astype(self.element_type)
        return self.series

    def round(self, rule: str = "half_even", decimals: int = 0) -> pd.Series:
        """Round the series using to the given number of decimal places using
        the specified rounding rule.

        Round numerics according to the specified rule.

        Parameters
        ----------
        rule : str, default 'half_even'
            A string specifying the rounding strategy to use.  Must be one of
            ('floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
            'half_down', 'half_up', 'half_even'), where `up`/`down` round
            away/toward zero, and `ceiling`/`floor` round toward +/- infinity,
            respectively.
        decimals : int, default 0
            The number of decimals to round to.  Positive numbers count to the
            right of the decimal point, and negative values count to the left.
            0 represents rounding in the ones place of `val`.  This follows the
            convention set out in `numpy.around`.

        Returns
        -------
        pd.Series
            The result of rounding `val` according to the given rule.

        Raises
        ------
        ValueError
            If `rule` is not one of the accepted rounding rules ('floor',
            'ceiling', 'down', 'up', 'half_floor', 'half_ceiling', 'half_down',
            'half_up', 'half_even').
        """
        return self.dispatch("round", rule=rule, decimals=decimals)

    def snap(self, tol: numeric = 1e-6) -> pd.Series:
        """Snap each element of the series to the nearest integer if it is
        within the specified tolerance.

        Parameters
        ----------
        tol : int | float | decimal.Decimal
            The tolerance to use for the conditional check, which represents
            the width of the 2-sided region around each integer within which
            rounding is performed.  This can be arbitrarily large, but values
            over 0.5 are functionally equivalent to rounding half_even.

        Returns
        -------
        pd.Series
            The result of conditionally rounding the series around integers,
            with tolerance `tol`.
        """
        if not tol:  # trivial case, tol=0
            return self.series

        rounded = self.round("half_even", decimals=0)
        return self.where(np.abs(self - rounded) > tol, rounded)

    def snap_round(
        self,
        tol: numeric = 1e-6,
        rule: str = "half_even"
    ) -> SeriesWrapper:
        """Snap the series to the nearest integer within `tol`, and then round
        the remaining results according to the given rule.
        """
        # snap round, checking for precision loss
        if tol or rounding is None:
            rounded = series.round("half_even")
            index = ~cast.within_tolerance(series, rounded, tol=tol.real)
            if tol:
                series.series = series.where(index, rounded)
            if rounding is None and index.any():
                if errors == "coerce":
                    series.series = series.round("down")
                else:
                    raise ValueError(
                        f"precision loss detected at index "
                        f"{shorten_list(series[index].index.values)}"
                    )
        if rounding:
            series.series = series.round(rounding)

        # don't snap if rounding to nearest
        cdef set nearest = {
            "half_floor", "half_ceiling", "half_down", "half_up", "half_even"
        }

        result = self.series
        if tol and rule not in nearest:
            result = self.snap(tol=tol)
        if rule:
            result = self.round(rule=rule)
        return result

    def to_boolean(
        self,
        dtype: atomic.AtomicType,
        **kwargs: str
    ) -> SeriesWrapper:
        """Convert arbitrary series data to a boolean data type.

        This function hooks into an AtomicType's `to_boolean()` method, which
        holds implementation logic.
        """
        result = self.dispatch("to_boolean", dtype=dtype, **kwargs)
        result.element_type = dtype
        return result

    def to_integer(
        self,
        dtype: atomic.AtomicType,
        **kwargs: str
    ) -> SeriesWrapper:
        """Convert arbitrary series data to a boolean data type.

        This function hooks into an AtomicType's `to_integer()` method, which
        holds implementation logic.
        """
        result = self.dispatch("to_integer", dtype=dtype, **kwargs)
        result.element_type = dtype
        return result

    def to_float(
        self,
        dtype: atomic.AtomicType,
        **kwargs: str
    ) -> SeriesWrapper:
        """Convert arbitrary series data to a boolean data type.

        This function hooks into an AtomicType's `to_float()` method, which
        holds implementation logic.
        """
        result = self.dispatch("to_float", dtype=dtype, **kwargs)
        result.element_type = dtype
        return result

    def to_complex(
        self,
        dtype: atomic.AtomicType,
        **kwargs: str
    ) -> SeriesWrapper:
        """Convert arbitrary series data to a boolean data type.

        This function hooks into an AtomicType's `to_complex()` method, which
        holds implementation logic.
        """
        result = self.dispatch("to_complex", dtype=dtype, **kwargs)
        result.element_type = dtype
        return result

    def to_decimal(
        self,
        dtype: atomic.AtomicType,
        **kwargs: str
    ) -> SeriesWrapper:
        """Convert arbitrary series data to a boolean data type.

        This function hooks into an AtomicType's `to_decimal()` method, which
        holds implementation logic.
        """
        result = self.dispatch("to_decimal", dtype=dtype, **kwargs)
        result.element_type = dtype
        return result

    def to_datetime(
        self,
        dtype: atomic.AtomicType,
        **kwargs: str
    ) -> SeriesWrapper:
        """Convert arbitrary series data to a boolean data type.

        This function hooks into an AtomicType's `to_datetime()` method, which
        holds implementation logic.
        """
        result = self.dispatch("to_datetime", dtype=dtype, **kwargs)
        result.element_type = dtype
        return result

    def to_timedelta(
        self,
        dtype: atomic.AtomicType,
        **kwargs: str
    ) -> SeriesWrapper:
        """Convert arbitrary series data to a boolean data type.

        This function hooks into an AtomicType's `to_timedelta()` method, which
        holds implementation logic.
        """
        result = self.dispatch("to_timedelta", dtype=dtype, **kwargs)
        result.element_type = dtype
        return result

    def to_string(
        self,
        dtype: atomic.AtomicType,
        **kwargs: str
    ) -> SeriesWrapper:
        """Convert arbitrary series data to a boolean data type.

        This function hooks into an AtomicType's `to_string()` method, which
        holds implementation logic.
        """
        result = self.dispatch("to_string", dtype=dtype, **kwargs)
        result.element_type = dtype
        return result

    def to_object(
        self,
        dtype: atomic.AtomicType,
        **kwargs: str
    ) -> SeriesWrapper:
        """Convert arbitrary series data to a boolean data type.

        This function hooks into an AtomicType's `to_object()` method, which
        holds implementation logic.
        """
        result = self.dispatch("to_object", dtype=dtype, **kwargs)
        result.element_type = dtype
        return result

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __abs__(self) -> pd.Series:
        return abs(self.series)

    def __add__(self, other) -> pd.Series:
        return self.series + other

    def __and__(self, other) -> pd.Series:
        return self.series & other

    def __contains__(self, val) -> bool:
        return self.series.__contains__(val)

    def __delattr__(self, name):
        delattr(self.series, name)

    def __delete__(self, instance):
        self.series.__delete__(instance)

    def __delitem__(self, key) -> None:
        del self.series[key]

    def __dir__(self) -> list[str]:
        result = dir(type(self))
        result += list(self.__dict__.keys())
        result += [x for x in dir(self.series) if x not in result]
        return result

    def __div__(self, other) -> pd.Series:
        return self.series / other

    def __divmod__(self, other) -> pd.Series:
        return divmod(self.series, other)

    def __eq__(self, other) -> pd.Series:
        return self.series == other

    def __float__(self) -> float:
        return float(self.series)

    def __floordiv__(self, other) -> pd.Series:
        return self.series // other

    def __ge__(self, other) -> pd.Series:
        return self.series >= other

    def __get__(self, instance, class_):
        return self.series.__get__(instance, class_)

    def __getattr__(self, name) -> Any:
        return getattr(self.series, name)

    def __getitem__(self, key) -> Any:
        return self.series[key]

    def __gt__(self, other) -> pd.Series:
        return self.series > other

    def __hex__(self) -> hex:
        return hex(self.series)

    def __iadd__(self, other) -> None:
        self.series += other

    def __iand__(self, other) -> None:
        self.series &= other

    def __idiv__(self, other) -> None:
        self.series /= other

    def __ifloordiv__(self, other) -> None:
        self.series //= other

    def __ilshift__(self, other) -> None:
        self.series <<= other

    def __imod__(self, other) -> None:
        self.series %= other

    def __imul__(self, other) -> None:
        self.series *= other

    def __int__(self) -> int:
        return int(self.series)

    def __invert__(self) -> pd.Series:
        return ~ self.series

    def __ior__(self, other) -> None:
        self.series |= other

    def __ipow__(self, other) -> None:
        self.series **= other

    def __irshift__(self, other) -> None:
        self.series >>= other

    def __isub__(self, other) -> None:
        self.series -= other

    def __iter__(self) -> Iterator:
        return self.series.__iter__()

    def __ixor__(self, other) -> None:
        self.series ^= other

    def __le__(self, other) -> pd.Series:
        return self.series <= other

    def __len__(self) -> int:
        return len(self.series)

    def __lshift__(self, other) -> pd.Series:
        return self.series << other

    def __lt__(self, other) -> pd.Series:
        return self.series < other

    def __mod__(self, other) -> pd.Series:
        return self.series % other

    def __mul__(self, other) -> pd.Series:
        return self.series * other

    def __ne__(self, other) -> pd.Series:
        return self.series != other

    def __neg__(self) -> pd.Series:
        return - self.series

    def __next__(self):
        return self.series.__next__()

    def __oct__(self) -> oct:
        return oct(self.series)

    def __or__(self, other) -> pd.Series:
        return self.series | other

    def __pos__(self) -> pd.Series:
        return + self.series

    def __pow__(self, other, mod) -> pd.Series:
        return self.series.__pow__(other, mod)

    def __radd__(self, other) -> pd.Series:
        return other + self.series

    def __rand__(self, other) -> pd.Series:
        return other & self.series

    def __rdiv__(self, other) -> pd.Series:
        return other / self.series

    def __rdivmod__(self, other) -> pd.Series:
        return divmod(other, self.series)

    def __repr__(self) -> str:
        return repr(self.series)

    def __rfloordiv__(self, other) -> pd.Series:
        return other // self.series

    def __rlshift__(self, other) -> pd.Series:
        return other << self.series

    def __rmod__(self, other) -> pd.Series:
        return other % self.series

    def __rmul__(self, other) -> pd.Series:
        return other * self.series

    def __ror__(self, other) -> pd.Series:
        return other | self.series

    def __rpow__(self, other, mod) -> pd.Series:
        return self.series.__rpow__(other, mod)

    def __rrshift__(self, other) -> pd.Series:
        return other >> self.series

    def __rshift__(self, other) -> pd.Series:
        return self.series >> other

    def __rsub__(self, other) -> pd.Series:
        return other - self.series

    def __rxor__(self, other) -> pd.Series:
        return other ^ self.series

    def __set__(self, instance, value):
        self.series.__set__(instance, value)

    def __setitem__(self, key, val) -> None:
        self.series[key] = val

    def __str__(self) -> str:
        return str(self.series)

    def __sub__(self, other) -> pd.Series:
        return self.series - other

    def __xor__(self, other) -> pd.Series:
        return self.series ^ other
