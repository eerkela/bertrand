from functools import wraps
import inspect
from typing import Any, Callable, Iterator

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd

cimport pdcast.detect as detect
import pdcast.detect as detect
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve
cimport pdcast.types as types
import pdcast.types as types

import pdcast.convert.default as default
import pdcast.convert.standalone as standalone

from pdcast.util.error import shorten_list
from pdcast.util.type_hints import array_like, numeric, type_specifier


# TODO: SparseType works, but not in all cases.
# -> pd.NA disallows non-missing fill values
# -> Timestamps must be sparsified manually by converting to object and then
# to sparse
# -> Timedeltas just don't work at all.  astype() rejects pd.SparseDtype("m8")
# entirely.
# -> SeriesWrappers should unwrap sparse/categorical series during dispatch.


# TODO: have to account for empty series in each conversion.


######################
####    PUBLIC    ####
######################


cdef class SeriesWrapper:
    """Wrapper for type-aware pd.Series objects.

    Implements a dynamic wrapper according to the Gang of Four's Decorator
    Pattern (not to be confused with python decorators).
    """

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        element_type: types.BaseType = None
    ):
        self.series = series
        self.hasnans = hasnans
        self.element_type = element_type

    ##########################
    ####    PROPERTIES    ####
    ##########################

    @property
    def element_type(self) -> types.BaseType:
        if self._element_type is None:
            self._element_type = detect.detect_type(self.series)
        return self._element_type

    @element_type.setter
    def element_type(self, val: type_specifier) -> None:
        if val is not None:
            val = resolve.resolve_type(val)
            if (
                isinstance(val, types.CompositeType) and
                getattr(val.index, "shape", None) != self.shape
            ):
                raise ValueError(
                    f"`element_type.index` must have the same shape as the "
                    f"series it describes"
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
        return SeriesWrapper(
            result,
            hasnans=self._hasnans,
            element_type=target
        )

    @property
    def real(self) -> SeriesWrapper:
        """Get the real component of a wrapped series."""
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
        return SeriesWrapper(
            result,
            hasnans=self._hasnans,
            element_type=target
        )

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
        self._max = None
        self._min = None

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
        dtype: type_specifier,
        errors: str = "raise"
    ) -> SeriesWrapper:
        """`astype()` equivalent for SeriesWrapper instances that works for
        object-based type specifiers.
        """
        dtype = resolve.resolve_type(dtype)
        if isinstance(dtype, types.CompositeType):
            raise ValueError(f"`dtype` must be atomic, not {repr(dtype)}")

        # apply dtype.type_def elementwise if not astype-compliant
        if dtype.unwrap().dtype == np.dtype("O"):
            result = self.apply_with_errors(
                call=dtype.type_def,
                errors=errors
            )
            result.element_type=dtype
            return result

        # default to pd.Series.astype()
        target = dtype.dtype
        if (
            pd.api.types.is_object_dtype(self.series) and
            hasattr(target, "numpy_dtype")
        ):
            # NOTE: pandas doesn't like converting arbitrary objects to
            # nullable extension types.  Luckily, numpy has no such problem,
            # and SeriesWrapper automatically filters out NAs.
            result = self.series.astype(target.numpy_dtype).astype(target)
        else:
            result = self.series.astype(target)

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

    def max(self, *args, **kwargs):
        """A cached version of pd.Series.max()."""
        if self._max is None:
            self._max = self.series.max(*args, **kwargs)
        return self._max

    def min(self, *args, **kwargs):
        """A cached version of pd.Series.min()."""
        if self._min is None:
            self._min = self.series.min(*args, **kwargs)
        return self._min

    ###########################
    ####    NEW METHODS    ####
    ###########################

    def __enter__(self) -> SeriesWrapper:
        # record shape
        self._orig_shape = self.series.shape

        # normalize index
        if not isinstance(self.series.index, pd.RangeIndex):
            self._orig_index = self.series.index
            self.series.index = pd.RangeIndex(0, self._orig_shape[0])

        # drop missing values
        is_na = self.isna()
        self.hasnans = is_na.any()
        if self._hasnans:
            self.series = self.series[~is_na]

        # detect element type if not set manually
        if self._element_type is None:
            self.element_type = detect.detect_type(self.series, skip_na=False)

        # unwrap sparse/categorical series
        if isinstance(self.element_type, types.AdapterType):
            self._orig_type = self.element_type
            self.element_type = self.element_type.unwrap()

            # NOTE: this is a pending deprecation shim.  In a future version
            # of pandas, astype() from a sparse to non-sparse dtype will return
            # a non-sparse series.  Currently, it returns a sparse equivalent.
            # When this behavior changes, delete this block.
            if isinstance(self._orig_type, types.SparseType):
                self.series = self.series.sparse.to_dense()

            self.series = self.rectify().series

        # rectify and enter context block
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        # replace missing values, aligning on index
        if self.hasnans:
            result = pd.Series(
                np.full(
                    self._orig_shape,
                    getattr(self.element_type, "na_value", pd.NA),
                    dtype="O"
                ),
                dtype=self.dtype
            )
            result.update(self.series)
            self.series = result

        # replace original index
        if self._orig_index is not None:
            self.series.index = self._orig_index
            self._orig_index = None

        # replace adapters if element_type is unchanged
        if (
            self._orig_type is not None and
            self._orig_type.unwrap() == self.element_type
        ):
            if hasattr(self._orig_type, "levels"):  # update levels
                self._orig_type = self._orig_type.replace(levels=None)
            result = self._orig_type.apply_adapters(self)
            self.series = result.series
            self.element_type = result.element_type

    def __getattr__(self, name: str) -> Any:
        # dynamically re-wrap series outputs
        attr = getattr(self.series, name)

        # method - return a decorator
        if callable(attr):

            @wraps(attr)
            def wrapper(*args, **kwargs):
                result = attr(*args, **kwargs)
                if isinstance(result, pd.Series):
                    return SeriesWrapper(result, hasnans=self._hasnans)
                return result

            return wrapper

        # attribute
        if isinstance(attr, pd.Series):
            return SeriesWrapper(attr, hasnans=self._hasnans)
        return attr

    def apply_with_errors(
        self,
        call: Callable,
        errors: str = "raise"
    ) -> SeriesWrapper:
        """Apply `call` over the series, applying the specified error handling
        rule at each index.
        """
        result, has_errors, index = _apply_with_errors(
            self.series.to_numpy(dtype="O"),
            call=call,
            errors=errors
        )
        result = pd.Series(result, index=self.index, dtype="O")
        if has_errors:
            result = result[~index]
        return SeriesWrapper(result, hasnans=has_errors or self._hasnans)

    def boundscheck(
        self,
        dtype: types.AtomicType,
        errors: str
    ) -> tuple[SeriesWrapper, types.AtomicType]:
        """Ensure that a series does not overflow past the allowable range of the
        given AtomicType.  If overflow is detected, attempt to upcast the
        AtomicType to fit or coerce the series if directed.
        """
        # NOTE: this takes advantage of SeriesWrapper's min/max caching.
        series = self
        min_val = series.min()
        max_val = series.max()

        # NOTE: we convert to pyint to prevent inconsistent comparisons
        min_int = int(min_val - bool(min_val % 1))  # round floor
        max_int = int(max_val + bool(max_val % 1))  # round ceiling
        if min_int < dtype.min or max_int > dtype.max:
            # attempt to upcast dtype to fit series
            try:
                return series, dtype.upcast(series)
            except OverflowError:
                pass

            # continue with OverflowError
            index = (series < dtype.min) | (series > dtype.max)
            if errors == "coerce":
                series = series[~index]
                series.hasnans = True
            else:
                raise OverflowError(
                    f"values exceed {dtype} range at index "
                    f"{shorten_list(series[index].index.values)}"
                )

        return series, dtype

    def dispatch(
        self,
        endpoint: str,
        submap: dict,
        original: Callable,
        *args,
        **kwargs
    ) -> SeriesWrapper:
        """For every type that is present in the series, invoke the named
        endpoint.
        """
        # series is composite
        if isinstance(self.element_type, types.CompositeType):
            return _dispatch_composite(
                self,
                endpoint,
                submap,
                original,
                *args,
                **kwargs
            )

        # series is homogenous
        return _dispatch_homogenous(
            self,
            endpoint,
            submap,
            original,
            *args,
            **kwargs
        )

    def isinf(self) -> SeriesWrapper:
        """Return a boolean mask indicating the position of infinities in the
        series.
        """
        return self.isin([np.inf, -np.inf])

    def rectify(self) -> SeriesWrapper:
        """Convert an improperly-formatted object series to a standardized
        numpy/pandas data type.
        """
        if self.series.dtype != self.element_type.dtype:
            return self.astype(self.element_type)
        return self

    def within_tol(self, other, tol: numeric) -> array_like:
        """Check if every element of a series is within tolerance of another
        series.
        """
        if not tol:  # fastpath if tolerance=0
            return self == other
        return ~((self - other).abs() > tol)

    ##########################
    ####    ARITHMETIC    ####
    ##########################

    # NOTE: math operators can change the element_type of a SeriesWrapper in
    # unexpected ways.  If you know the final element_type ahead of time, set
    # it manually after the operation by assigning to the result's
    # .element_type field.  Otherwise it will automatically be forgotten and
    # regenerated when you next request it, which can be expensive if the final
    # series has dtype="O".

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

    def __iadd__(self, other) -> SeriesWrapper:
        self.series += other
        self._element_type = None
        return self

    def __iand__(self, other) -> SeriesWrapper:
        self.series &= other
        self._element_type = None
        return self

    def __idiv__(self, other) -> SeriesWrapper:
        self.series /= other
        self._element_type = None
        return self

    def __ifloordiv__(self, other) -> SeriesWrapper:
        self.series //= other
        self._element_type = None
        return self

    def __ilshift__(self, other) -> SeriesWrapper:
        self.series <<= other
        self._element_type = None
        return self

    def __imod__(self, other) -> SeriesWrapper:
        self.series %= other
        self._element_type = None
        return self

    def __imul__(self, other) -> SeriesWrapper:
        self.series *= other
        self._element_type = None
        return self

    def __invert__(self) -> SeriesWrapper:
        return SeriesWrapper(~self.series, hasnans=self._hasnans)

    def __ior__(self, other) -> SeriesWrapper:
        self.series |= other
        self._element_type = None
        return self

    def __ipow__(self, other) -> SeriesWrapper:
        self.series **= other
        self._element_type = None
        return self

    def __irshift__(self, other) -> SeriesWrapper:
        self.series >>= other
        self._element_type = None
        return self

    def __isub__(self, other) -> SeriesWrapper:
        self.series -= other
        self._element_type = None
        return self

    def __ixor__(self, other) -> SeriesWrapper:
        self.series ^= other
        self._element_type = None
        return self

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
        result += [x for x in types.AtomicType.registry.dispatch_map]

        return result

    def __float__(self) -> float:
        return float(self.series)

    def __getitem__(self, key) -> Any:
        result = self.series[key]

        # slicing: re-wrap result
        if isinstance(result, pd.Series):
            # slicing can change element_type of result, but only if composite
            if isinstance(self._element_type, types.CompositeType):
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

    def __next__(self) -> Any:
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
        self._max = None
        self._min = None

    def __str__(self) -> str:
        return str(self.series)


######################
####    PRIVATE   ####
######################


@cython.boundscheck(False)
@cython.wraparound(False)
cdef tuple _apply_with_errors(np.ndarray[object] arr, object call, str errors):
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


def _dispatch_composite(
    series: SeriesWrapper,
    endpoint: str,
    submap: dict,
    original: Callable,
    *args,
    **kwargs
) -> SeriesWrapper:
    """Given an input series of mixed type, split into groups and dispatch to
    the selected endpoint, falling back to pandas if not specified.
    """
    groups = series.series.groupby(series.element_type.index, sort=False)
    if original is not None:  # introspect before grouping
        pars = inspect.signature(original).parameters

    # NOTE: SeriesGroupBy.transform() cannot reconcile mixed int64/uint64
    # arrays, and will attempt to convert them to float.  To avoid this,
    # keep track of result.dtype.  If int64/uint64-like and opposite has
    # been observed, convert to dtype="O" and reconsider afterwards.
    observed = []
    check_uint = [False]  # transform doesn't recognize this if scalar

    def transform(grp):
        # check for corresponding AtomicType method
        call = submap.get(type(grp.name.unwrap()), None)
        if call is not None:
            result = call(
                grp.name,
                SeriesWrapper(grp, hasnans=series._hasnans),
                *args,
                **kwargs
            )
            series.hasnans = series._hasnans or result._hasnans
            result = result.series

        # fall back to pandas implementation
        else:
            kw = {k: v for k, v in kwargs.items() if k in pars}
            result = original(*args, **kw)

        # ensure final index is a subset of original index
        if not result.index.difference(grp.index).empty:
            raise RuntimeError(
                f"index mismatch: output index must be a subset of input "
                f"index for group {repr(str(grp.name))}"
            )

        # check for int64/uint64 conflict
        obs = resolve.resolve_type(result.dtype)
        signed = types.SignedIntegerType
        unsigned = types.UnsignedIntegerType
        if obs.is_subtype(signed):
            if any(x.is_subtype(unsigned) for x in observed):
                obs = resolve.resolve_type(types.ObjectType)
                result = result.astype("O")
                check_uint[0] = None if check_uint[0] is None else True
        elif obs.is_subtype(unsigned):
            if any(x.is_subtype(signed) for x in observed):
                obs = resolve.resolve_type(types.ObjectType)
                result = result.astype("O")
                check_uint[0] = None if check_uint[0] is None else True
        else:
            check_uint[0] = None
        observed.append(obs)

        return result

    result = groups.transform(transform)
    if check_uint[0]:
        if series.hasnans:
            target = resolve.resolve_type(types.PandasUnsignedIntegerType)
        else:
            target = resolve.resolve_type(types.UnsignedIntegerType)
        try:
            result = standalone.to_integer(
                result,
                dtype=target,
                downcast=kwargs.get("downcast", None),
                errors="raise"
            )
        except OverflowError:
            pass
    return SeriesWrapper(result, hasnans=series._hasnans)


def _dispatch_homogenous(
    series: SeriesWrapper,
    endpoint: str,
    submap: dict,
    original: Callable,
    *args,
    **kwargs
) -> SeriesWrapper:
    """Given a homogenously-typed input series, dispatch to the selected
    endpoint, falling back to pandas if not specified.
    """
    # check for corresponding AtomicType method
    call = submap.get(type(series.element_type.unwrap()), None)
    if call is not None:
        return call(
            series.element_type,
            series,
            *args,
            **kwargs
        )

    # fall back to pandas implementation
    pars = inspect.signature(original).parameters
    kwargs = {k: v for k, v in kwargs.items() if k in pars}
    result = original(*args, **kwargs)
    if isinstance(result, pd.Series):
        return SeriesWrapper(result, hasnans=series._hasnans)
    return result
