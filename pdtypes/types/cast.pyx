from contextlib import contextmanager
import decimal
from functools import partial, wraps
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

from pdtypes.error import shorten_list
from pdtypes.type_hints import array_like, numeric
from pdtypes.util.round import Tolerance


# TODO: dispatch() -> dispatch_method().  This is a decorator that wraps the
# result of a function call.



# TODO: downcast flag should accept resolvable type specifiers as well as
# booleans.  If a non-boolean value is given, it will not downcast below the
# specified type.


# TODO: sparse types currently broken

# TODO: top-level to_x() functions are respondible for input validation

# TODO: have to account for empty series in each conversion.


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


def check_for_overflow(
    series: SeriesWrapper,
    dtype: atomic.AtomicType,
    errors: str
) -> atomic.AtomicType:
    """Ensure that a series does not overflow past the allowable range of the
    given AtomicType.  If overflow is detected, attempt to upcast the
    AtomicType to fit or coerce the series if directed.
    """
    # TODO: may want to incorporate a tolerance here to allow for clipping

    if int(series.min()) < dtype.min or int(series.max()) > dtype.max:
        # attempt to upcast
        if hasattr(dtype, "upcast"):
            try:
                return dtype.upcast(series)
            except OverflowError:
                pass

        # process error
        index = (series < dtype.min) | (series > dtype.max)
        if errors == "coerce":
            # series.series = series[~index].clip(dtype.min, dtype.max)
            series.series = series[~index]
            series.hasnans = True
        else:
            raise OverflowError(
                f"values exceed {dtype} range at index "
                f"{shorten_list(series[index].index.values)}"
            )

    return dtype


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


def snap_round(
    series: SeriesWrapper,
    tol: numeric,
    rule: str,
    errors: str
) -> SeriesWrapper:
    """Snap a SeriesWrapper to the nearest integer within `tol`, and then round
    any remaining results according to the given rule.  Rejects any outputs
    that are not integer-like by the end of this process.
    """
    # NOTE: semantics are a bit messy here, but they minimize rounding
    # operations as much as possible.

    # apply tolerance, then check for non-integers if necessary
    if tol or rule is None:
        rounded = series.round("half_even")  # compute once
        outside = ~within_tolerance(series, rounded, tol=tol)
        if tol:
            series.series = series.where(outside.series, rounded.series).series

        # check for non-integer (ignore if rounding)
        if rule is None and outside.any():
            if errors == "coerce":
                series.series = series.round("down").series
            else:
                raise ValueError(
                    f"precision loss exceeds tolerance {tol:.2e} at index "
                    f"{shorten_list(outside[outside].index.values)}"
                )

    # apply final rounding rule
    if rule:
        series.series = series.round(rule).series

    return series


def within_tolerance(series_1, series_2, tol: numeric) -> array_like:
    """Check if every element of a series is within tolerance of another
    series.
    """
    if not tol:  # fastpath if tolerance=0
        return series_1 == series_2
    return ~((series_1 - series_2).abs() > tol)


cdef class SeriesWrapper:
    """Wrapper for type-aware pd.Series objects.

    Implements a dynamic wrapper according to the Gang of Four's Decorator
    Pattern (not to be confused with python decorators).
    """

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        element_type: atomic.BaseType = None
    ):
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
        self.cache = {}  # reset cache

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
                errors=errors,
                element_type=dtype
            )
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

    def copy(self, *args, **kwargs) -> SeriesWrapper:
        """Duplicate a SeriesWrapper."""
        return SeriesWrapper(
            self.series.copy(*args, **kwargs),
            hasnans=self._hasnans,
            element_type=self._element_type
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

    def __getattr__(self, name: str) -> Any:
        dispatch_map = atomic.AtomicType.registry.dispatch_map
        if name in dispatch_map:  # dispatch method
            return self.dispatch(name)

        # fall back to pandas
        fallback = getattr(self.series, name)
        if callable(fallback):  # dynamically wrap series outputs

            @wraps(fallback)
            def wrapper(*args, **kwargs):
                result = fallback(*args, **kwargs)
                if isinstance(result, pd.Series):
                    return SeriesWrapper(result)
                return result

            return wrapper

        if isinstance(fallback, pd.Series):  # re-wrap
            return SeriesWrapper(fallback)
        return fallback

    def apply_with_errors(
        self,
        call: Callable,
        errors: str = "raise",
        element_type: atomic.BaseType = None
    ) -> SeriesWrapper:
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
            hasnans=has_errors or self.hasnans,
            element_type=element_type
        )

    def dispatch(self, endpoint: str) -> Callable:
        """Decorate the named method, dispatching it across every type present
        in a SeriesWrapper instance.
        """
        dispatch_map = atomic.AtomicType.registry.dispatch_map
        submap = dispatch_map[endpoint]
        element_type = self.element_type

        # series is homogenous
        if isinstance(element_type, atomic.AtomicType):
            # check for corresponding AtomicType method
            call = submap.get(type(element_type), None)
            if call is not None:
                return partial(call, element_type, self)

            # fall back to pandas implementation
            call = getattr(self.series, endpoint)

            @wraps(call)
            def wrapper(*args, **kwargs):
                result = call(*args, **kwargs)
                if isinstance(result, pd.Series):
                    return SeriesWrapper(result)
                return result

            return wrapper

        # series is composite
        groups = self.series.groupby(element_type.index, sort=False)
        output_types = set()
        indices = []

        def wrapper(*args, **kwargs):
            def transform(grp):
                atomic_type = grp.name
                grp = SeriesWrapper(
                    grp,
                    hasnans=self.hasnans,
                    element_type=atomic_type
                )
                call = submap.get(type(atomic_type), None)
                if call is not None:
                    result = call(atomic_type, grp, *args, **kwargs)
                else:
                    call = getattr(grp.series, endpoint)
                    result = SeriesWrapper(call(*args, **kwargs))
                if not self.hasnans:
                    self.hasnans=result.hasnans
                output_types.add(result.element_type)
                indices.append(
                    pd.Series(
                        np.full(result.shape, result.element_type),
                        index=result.series.index
                    )
                )
                return result.series

            result = groups.transform(transform)
            result_type = resolve.resolve_type(output_types)
            if len(result_type) == 1:
                result_type = result_type.pop()
            else:
                index = pd.Series(
                    np.empty(result.shape, dtype="O"),
                    index=result.index
                )
                for i in indices:
                    index.update(i)
                result_type.index = index.to_numpy()
            return SeriesWrapper(
                result,
                hasnans=self.hasnans,
                element_type=result_type
            )

        return wrapper

    def isinf(self) -> SeriesWrapper:
        """TODO"""
        return self.isin([np.inf, -np.inf])

    def rectify(self) -> SeriesWrapper:
        """Convert an improperly-formatted object series to a standardized
        numpy/pandas data type.
        """
        # TODO: deprecate this
        if (
            pd.api.types.is_object_dtype(self.series) and
            self.element_type.dtype != np.dtype("O")
        ):
            return self.astype(self.element_type)
        return self

    def snap(self, tol: numeric = 1e-6) -> SeriesWrapper:
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
        SeriesWrapper
            The result of conditionally rounding the series around integers,
            with tolerance `tol`.
        """
        if not tol:  # trivial case, tol=0
            return self.copy()

        rounded = self.round("half_even")
        return SeriesWrapper(
            self.series.where((
                (self - rounded).abs() > tol).series,
                rounded.series
            ),
            hasnans=self.hasnans,
            element_type=self.element_type
        )

    #################################
    ####   DISPATCHED METHODS    ####
    #################################

    # NOTE: SeriesWrapper dynamically inherits every @dispatch method that is
    # defined by its element_type.  In the case of a composite series, these
    # dispatched methods are applied independently to each type that is present
    # in the series.  If a dispatched method is not defined for a given
    # element_type, then SeriesWrapper automatically falls back to the pandas
    # implementation, if one exists.  See __getattr__() for more details on how
    # this is done.


    ##########################
    ####    ARITHMETIC    ####
    ##########################

    # NOTE: math operators can change the element_type of a series

    def __abs__(self) -> SeriesWrapper:
        return SeriesWrapper(abs(self.series), hasnans=self._hasnans)

    def __add__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series + other, hasnans=self._hasnans)

    def __and__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series & other, hasnans=self._hasnans)

    def __divmod__(self, other) -> SeriesWrapper:
        return SeriesWrapper(divmod(self.series, other), hasnans=self._hasnans)

    def __eq__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series == other, hasnans=self._hasnans)

    def __floordiv__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series // other, hasnans=self._hasnans)

    def __ge__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series >= other, hasnans=self._hasnans)

    def __gt__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series > other, hasnans=self._hasnans)

    def __iadd__(self, other) -> None:
        self.series += other
        self._element_type = None

    def __iand__(self, other) -> None:
        self.series &= other
        self._element_type = None

    def __idiv__(self, other) -> None:
        self.series /= other
        self._element_type = None

    def __ifloordiv__(self, other) -> None:
        self.series //= other
        self._element_type = None

    def __ilshift__(self, other) -> None:
        self.series <<= other
        self._element_type = None

    def __imod__(self, other) -> None:
        self.series %= other
        self._element_type = None

    def __imul__(self, other) -> None:
        self.series *= other
        self._element_type = None

    def __invert__(self) -> SeriesWrapper:
        return SeriesWrapper(~self.series, hasnans=self._hasnans)

    def __ior__(self, other) -> None:
        self.series |= other
        self._element_type = None

    def __ipow__(self, other) -> None:
        self.series **= other
        self._element_type = None

    def __irshift__(self, other) -> None:
        self.series >>= other
        self._element_type = None

    def __isub__(self, other) -> None:
        self.series -= other
        self._element_type = None

    def __ixor__(self, other) -> None:
        self.series ^= other
        self._element_type = None

    def __le__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series <= other, hasnans=self._hasnans)

    def __lshift__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series << other, hasnans=self._hasnans)

    def __lt__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series < other, hasnans=self._hasnans)

    def __mod__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series % other, hasnans=self._hasnans)

    def __mul__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series * other, hasnans=self._hasnans)

    def __ne__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series != other, hasnans=self._hasnans)

    def __neg__(self) -> SeriesWrapper:
        return SeriesWrapper(-self.series, hasnans=self._hasnans)

    def __or__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series | other, hasnans=self._hasnans)

    def __pos__(self) -> SeriesWrapper:
        return SeriesWrapper(+self.series, hasnans=self._hasnans)

    def __pow__(self, other, mod) -> SeriesWrapper:
        return SeriesWrapper(
            self.series.__pow__(other, mod),
            hasnans=self._hasnans
        )

    def __radd__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other + self.series, hasnans=self._hasnans)

    def __rand__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other & self.series, hasnans=self._hasnans)

    def __rdivmod__(self, other) -> SeriesWrapper:
        return SeriesWrapper(divmod(other, self.series), hasnans=self._hasnans)

    def __rfloordiv__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other // self.series, hasnans=self._hasnans)

    def __rlshift__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other << self.series, hasnans=self._hasnans)

    def __rmod__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other % self.series, hasnans=self._hasnans)

    def __rmul__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other * self.series, hasnans=self._hasnans)

    def __ror__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other | self.series, hasnans=self._hasnans)

    def __rpow__(self, other, mod) -> SeriesWrapper:
        return SeriesWrapper(
            self.series.__rpow__(other, mod),
            hasnans=self._hasnans
        )

    def __rrshift__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other >> self.series, hasnans=self._hasnans)

    def __rshift__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other >> self.series, hasnans=self._hasnans)

    def __rsub__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other - self.series, hasnans=self._hasnans)

    def __rtruediv__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other / self.series, hasnans=self._hasnans)

    def __rxor__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other ^ self.series, hasnans=self._hasnans)

    def __sub__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series - other, hasnans=self._hasnans)

    def __truediv__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series / other, hasnans=self._hasnans)

    def __xor__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series ^ other, hasnans=self._hasnans)

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __contains__(self, val) -> bool:
        return val in self.series

    def __delitem__(self, key) -> None:
        del self.series[key]
        self._element_type = None

    def __dir__(self) -> list[str]:
        # direct SeriesWrapper attributes
        result = dir(type(self))
        result += list(self.__dict__.keys())

        # pd.Series attributes
        result += [x for x in dir(self.series) if x not in result]

        # dispatched attributes
        result += [x for x in atomic.AtomicType.registry.dispatch_map]

        return result

    def __float__(self) -> float:
        return float(self.series)

    def __getitem__(self, key) -> Any:
        result = self.series[key]

        # slicing: re-wrap result
        if isinstance(result, pd.Series):
            # slicing can change element_type of result, but only if composite
            if isinstance(self._element_type, atomic.CompositeType):
                element_type = None
            else:
                element_type = self._element_type

            # wrap
            result = SeriesWrapper(
                result,
                hasnans=self._hasnans,
                element_type=element_type
            )

        return result

    def __hex__(self) -> hex:
        return hex(self.series)

    def __int__(self) -> int:
        return int(self.series)

    def __iter__(self) -> Iterator:
        return self.series.__iter__()

    def __len__(self) -> int:
        return len(self.series)

    def __next__(self):
        return self.series.__next__()

    def __oct__(self) -> oct:
        return oct(self.series)

    def __repr__(self) -> str:
        return repr(self.series)

    def __setitem__(self, key, val) -> None:
        self.series[key] = val
        self._element_type = None
        if self._hasnans == False:
            self._hasnans = None

    def __str__(self) -> str:
        return str(self.series)
