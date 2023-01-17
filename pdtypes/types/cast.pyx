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


# TODO: sparse types currently broken

# TODO: have SeriesWrapper accept other SeriesWrapper objects

# TODO: have SeriesWrapper automatically subset NAs, rather than using
# exclude_na.  .exclude_na just gets replaced with .replace_na()

# TODO: top-level to_x() functions are respondible for input validation


######################
####    PUBLIC    ####
######################


def cast(
    series: Iterable,
    dtype: resolve.resolvable = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to the given data type."""
    # TODO: SeriesWrapper must accept other SeriesWrappers
    # if dtype is None:
    #     dtype = SeriesWrapper(series).element_type
    # else:
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")

    # get specialized conversion function
    unwrapped = dtype.unwrap()
    if unwrapped.is_subtype(atomic.BooleanType):
        endpoint = to_boolean
    elif unwrapped.is_subtype(atomic.IntegerType):
        endpoint = to_integer
    elif unwrapped.is_subtype(atomic.FloatType):
        endpoint = to_float
    elif unwrapped.is_subtype(atomic.ComplexType):
        endpoint = to_complex
    elif unwrapped.is_subtype(atomic.DecimalType):
        endpoint = to_decimal
    elif unwrapped.is_subtype(atomic.DatetimeType):
        endpoint = to_datetime
    elif unwrapped.is_subtype(atomic.TimedeltaType):
        endpoint = to_timedelta
    elif unwrapped.is_subtype(atomic.StringType):
        endpoint = to_string
    else:
        endpoint = to_object

    # apply conversion function
    return endpoint(series, dtype, **kwargs)


def to_boolean(
    series: Iterable,
    dtype: resolve.resolvable = bool,
    rounding: str = None,
    tol: int | float | complex | decimal.Decimal = 1e-6,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to boolean representation."""
    if not isinstance(series, SeriesWrapper):
        series = SeriesWrapper(series)
    dtype = resolve.resolve_type(dtype)

    # validate input
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.unwrap().is_subtype(atomic.BooleanType):
        raise ValueError(f"`dtype` must be a boolean type, not {dtype}")

    # delegate to SeriesWrapper.do_conversion()
    return series.do_conversion(
        "to_boolean",
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
    tol: int | float | complex | decimal.Decimal = 1e-6,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to integer representation."""
    if not isinstance(series, SeriesWrapper):
        series = SeriesWrapper(series)
    dtype = resolve.resolve_type(dtype)

    # validate input
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.unwrap().is_subtype(atomic.IntegerType):
        raise ValueError(f"`dtype` must be an integer type, not {dtype}")

    # delegate to SeriesWrapper.do_conversion()
    return series.do_conversion(
        "to_integer",
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
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to float representation."""
    if not isinstance(series, SeriesWrapper):
        series = SeriesWrapper(series)
    dtype = resolve.resolve_type(dtype)

    # validate input
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.FloatType):
        raise ValueError(f"`dtype` must be a float type, not {dtype}")

    # delegate to SeriesWrapper.do_conversion()
    return series.do_conversion(
        "to_float",
        dtype=dtype,
        tol=tol,
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
    if not isinstance(series, SeriesWrapper):
        series = SeriesWrapper(series)
    dtype = resolve.resolve_type(dtype)

    # validate input
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.ComplexType):
        raise ValueError(f"`dtype` must be a complex type, not {dtype}")

    # delegate to SeriesWrapper.do_conversion()
    return series.do_conversion(
        "to_complex",
        dtype=dtype,
        tol=tol,
        errors=errors,
        **kwargs
    )


def to_decimal(
    series: Iterable,
    dtype: resolve.resolvable = int,
    rounding: str = None,
    tol: int | float | complex | decimal.Decimal = 1e-6,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to decimal representation."""
    if not isinstance(series, SeriesWrapper):
        series = SeriesWrapper(series)
    dtype = resolve.resolve_type(dtype)

    # validate input
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.DecimalType):
        raise ValueError(f"`dtype` must be a decimal type, not {dtype}")

    # delegate to SeriesWrapper.do_conversion()
    return series.do_conversion(
        "to_decimal",
        dtype=dtype,
        rounding=rounding,
        tol=tol,
        errors=errors,
        **kwargs
    )


def to_datetime(
    series: Iterable,
    dtype: resolve.resolvable = int,
    rounding: str = None,
    tol: int | float | complex | decimal.Decimal = 1e-6,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to datetime representation."""
    if not isinstance(series, SeriesWrapper):
        series = SeriesWrapper(series)
    dtype = resolve.resolve_type(dtype)

    # validate input
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.DatetimeType):
        raise ValueError(f"`dtype` must be a datetime type, not {dtype}")

    # delegate to SeriesWrapper.do_conversion()
    return series.do_conversion(
        "to_datetime",
        dtype=dtype,
        rounding=rounding,
        tol=tol,
        errors=errors,
        **kwargs
    )


def to_timedelta(
    series: Iterable,
    dtype: resolve.resolvable = int,
    rounding: str = None,
    tol: int | float | complex | decimal.Decimal = 1e-6,
    errors: str = "raise",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to timedelta representation."""
    if not isinstance(series, SeriesWrapper):
        series = SeriesWrapper(series)
    dtype = resolve.resolve_type(dtype)

    # validate input
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.TimedeltaType):
        raise ValueError(f"`dtype` must be a timedelta type, not {dtype}")

    # delegate to SeriesWrapper.do_conversion()
    return series.do_conversion(
        "to_timedelta",
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
    if not isinstance(series, SeriesWrapper):
        series = SeriesWrapper(series)
    dtype = resolve.resolve_type(dtype)

    # validate input
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.StringType):
        raise ValueError(f"`dtype` must be a string type, not {dtype}")

    # delegate to SeriesWrapper.do_conversion()
    return series.do_conversion(
        "to_string",
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
    if not isinstance(series, SeriesWrapper):
        series = SeriesWrapper(series)
    dtype = resolve.resolve_type(dtype)

    # validate input
    if isinstance(dtype, atomic.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")
    if not dtype.is_subtype(atomic.ObjectType):
        raise ValueError(f"`dtype` must be an object type, not {dtype}")

    # delegate to SeriesWrapper.do_conversion()
    return series.do_conversion(
        "to_object",
        dtype=dtype,
        call=call,
        errors=errors,
        **kwargs
    )


######################
####    HELPERS   ####
######################


cdef np.ndarray[object] _apply_with_errors(
    np.ndarray[object] arr,
    object call,
    object na_value,
    str errors
):
    """Apply a function over an object array using the given error-handling
    rule
    """
    cdef unsigned int arr_length = arr.shape[0]
    cdef unsigned int i
    cdef np.ndarray[object] result = np.full(arr_length, na_value, dtype="O")

    for i in range(arr_length):
        try:
            result[i] = call(arr[i])
        except (KeyboardInterrupt, MemoryError, SystemError, SystemExit):
            raise  # never coerce on these error types
        except Exception as err:
            if errors == "coerce":
                continue  # np.full() implicitly fills with na_value
            raise err

    return result


def apply_with_errors(
    series: pd.Series,
    call: Callable,
    na_value: Any,
    errors: str
) -> pd.Series:
    return pd.Series(
        _apply_with_errors(
            series.to_numpy(dtype="O"),
            call=call,
            na_value=na_value,
            errors=errors
        ),
        index=series.index,
        dtype="O"
    )


def apply_and_wrap(
    series: pd.Series,
    call: Callable,
    na_value: Any,
    errors: str,
    element_type: atomic.AtomicType = None
) -> SeriesWrapper:
    return SeriesWrapper(
        apply_with_errors(
            series,
            call=call,
            na_value=na_value,
            errors=errors
        ),
        hasnans=None if errors == "coerce" else series.hasnans,
        is_na=None if errors == "coerce" else False,
        # element_type=element_type
    )


def within_tolerance(series_1, series_2, tol) -> bool:
    """Check if every element of a series is within tolerance of another
    series.
    """
    if not tol:  # fastpath if tolerance=0
        return (series_1 == series_2).all()
    return not ((series_1 - series_2).abs() > tol).any()


cdef class SeriesWrapper:
    """Base wrapper for pd.Series objects.

    Implements a dynamic wrapper according to the Gang of Four's Decorator
    Pattern (not to be confused with python decorators).
    """

    def __init__(
        self,
        series: pd.Series,
        is_na: pd.Series = None,
        hasnans: bool = None,
        is_inf: pd.Series = None,
        hasinfs: bool = None,
        min: Any = None,
        idxmin: int = None,
        max: Any = None,
        idxmax: int = None,
    ):
        # generate series
        if isinstance(series, pd.Series):
            self.series = series
        elif isinstance(series, np.ndarray):
            # pandas doesn't like converting scalar numpy arrays
            if not series.shape:
                self.series = pd.Series(series[()], dtype=series.dtype)
            else:
                self.series = pd.Series(series)
        else:
            self.series = pd.Series(series, dtype="O")

        # override hasnans
        if hasnans is not None:
            self._hasnans = hasnans

        # override isna()
        if is_na is not None:
            self._is_na = pd.Series(
                np.broadcast_to(is_na, self.series.shape),
                index=self.series.index
            )
        elif self._hasnans == False:  # pre-cache a False isna() Series
            self._is_na = pd.Series(
                np.broadcast_to(False, self.series.shape),
                index=self.series.index
            )

        # override hasinfs
        if hasinfs is not None:
            self._hasinfs = hasinfs

        # override isinf()
        if is_inf is not None:
            self._is_na = pd.Series(
                np.broadcast_to(is_inf, self.series.shape),
                index=self.series.index
            )
        elif self._hasinfs == False:  # pre-cache a False isinf() Series
            self._is_inf = pd.Series(
                np.broadcast_to(False, self.series.shape),
                index=self.series.index
            )

        # override min
        if min is not None:
            self._min = min
        if idxmin is not None:
            self._idxmin = idxmin

        # override max
        if max is not None:
            self._max = max
        if idxmax is not None:
            self._idxmax = idxmax

        # detect element type
        self.element_type = detect.detect_type(self.dropna())

    #################################
    ####    CACHED PROPERTIES    ####
    #################################

    def argmax(self, *args, **kwargs) -> int:
        """Alias for IntegerSeries.max()."""
        return self.max(*args, **kwargs)

    def argmin(self, *args, **kwargs) -> int:
        """Alias for IntegerSeries.min()."""
        return self.min(*args, **kwargs)

    def dropna(self) -> pd.Series:
        """Return the wrapped series with missing values removed."""
        return self.series[self.notna()]

    @property
    def hasinfs(self) -> bool:
        """TODO"""
        if self._hasinfs is None:
            self.isinf().any()
        return self._hasinfs

    @property
    def hasnans(self) -> bool:
        """True if `hasnans=True` was supplied to the SeriesWrapper
        constructor.
        """
        if self._hasnans is None:
            self._hasnans = self.isna().any()
        return self._hasnans

    def idxmax(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.idxmax()."""
        if self._idxmax is None:
            self._idxmax = self.series.idxmax(*args, **kwargs)
        return self._idxmax

    def idxmin(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.idxmin()."""
        if self._idxmin is None:
            self._idxmin = self.series.idxmin(*args, **kwargs)
        return self._idxmin

    @property
    def imag(self) -> SeriesWrapper:
        if self._imag is None:
            self._imag = SeriesWrapper(
                pd.Series(
                    np.imag(self.series),
                    index=self.series.index
                )
            )
        return self._imag

    def isinf(self) -> pd.Series:
        """TODO"""
        if self._is_inf is None:
            self._is_inf = pd.Series(
                np.isinf(self.series),
                index=self.series.index
            )
        return self._is_inf

    def isna(self) -> pd.Series:
        """A cached version of the ordinary Series.isna() method."""
        # cached
        if self._is_na is None:
            self._is_na = self.series.isna()
        return self._is_na

    def isnull(self) -> pd.Series:
        """An alias for SeriesWrapper.isna()."""
        return self.isna()

    def max(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.max()."""
        if self._max is None:
            self._max = self.series.max(*args, **kwargs)
        return self._max

    def min(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.min()."""
        if self._min is None:
            self._min = self.series.min(*args, **kwargs)
        return self._min

    def notna(self) -> pd.Series:
        """A cached version of the ordinary Series.notna() method."""
        return self.isna().__invert__()

    def notnull(self) -> pd.Series:
        """An alias for SeriesWrapper.notna()"""
        return self.notna()

    @property
    def real(self) -> SeriesWrapper:
        if self._real is None:
            self._real = SeriesWrapper(
                pd.Series(
                    np.real(self.series),
                    index=self.series.index
                )
            )
        return self._real

    ##############################
    ####    HELPER METHODS    ####
    ##############################

    def do_conversion(
        self,
        conversion: str,
        dtype: atomic.AtomicType,
        errors: str,
        **kwargs
    ) -> pd.Series:
        """Perform a conversion on a SeriesWrapper object, automatically
        splitting into groups if it is non-homogenous
        """
        try:
            with self.exclude_na(dtype.na_value):
                if isinstance(self.element_type, atomic.CompositeType):
                    groups = self.groupby(self.element_type.index, sort=False)
                    self.series = groups.transform(
                        lambda grp: getattr(grp.name, conversion)(
                            SeriesWrapper(
                                grp,
                                hasnans=self.hasnans,
                                is_na=False
                            ),
                            dtype=dtype,
                            errors=errors,
                            **kwargs
                        )
                    )
                else:
                    self.series = getattr(self.element_type, conversion)(
                        self,
                        dtype=dtype,
                        errors=errors,
                        **kwargs
                    )

            return self.series

        except Exception as err:
            if errors == "ignore":
                return self.series
            raise err

    @contextmanager
    def exclude_na(
        self,
        fill_value: Any,
        array_type: np.dtype = None
    ) -> Iterator[SeriesWrapper]:
        """Provides a context manager that strips missing values from the
        decorated series and replaces them at the end of the context block.
        Operations within the block can safely disregard missing values.
        """
        # fastpath: skip subsetting if series does not have missing values
        if not self.hasnans:
            yield self

        # strip missing values and reconstruct after context block
        else:
            # determine intermediate array type
            if array_type is None:
                array_type = np.dtype("O")

            # strip nans
            self.series = self.dropna()

            # yield context
            try:
                yield self

            # replace nans
            finally:
                result = np.full(
                    self.isna().shape,
                    fill_value=fill_value,
                    dtype=array_type
                )
                result[self.notna()] = self.series
                self.series = pd.Series(
                    result,
                    index=self.isna().index,
                    dtype=self.dtype
                )

    def rectify(self) -> pd.Series:
        """Convert an improperly-formatted object series to a standardized
        numpy/pandas data type.
        """
        if pd.api.types.is_object_dtype(self):
            return self.astype(self.element_type.dtype, copy=False)
        return self.series

    ###############################
    ####    DYNAMIC WRAPPER    ####
    ###############################

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
