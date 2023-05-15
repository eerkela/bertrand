"""This module describes a ``SeriesWrapper`` object, which offers a type-aware
view into a pandas Series that automatically handles missing values, non-unique
indices, and other common problems, as well as offering convenience methods for
conversions and ``@dispatch`` methods.
"""
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
import pdcast.types.array.abstract as abstract

from pdcast.util.error import shorten_list
from pdcast.util.type_hints import array_like, numeric, type_specifier


# TODO: drop astype() from SeriesWrapper and put it in
# AbstractArray.from_sequence().  Then handle the integer/boolean special case
# in separate overloaded cast() implementations.
# -> potentially ruins rectify() performance


# -> SeriesWrapper might be completely internal.  Apply_with_errors becomes
# a standalone function in convert.base.  We just dump the contents of the
# wrapper into the dispatched function.  No need for make_nullable() on dtype
# either - these are covered in special cases of cast().

# still need hasnans, min/max caching.  These might be the only functions that
# are exposed.  Otherwise, if a dispatched function returns a Series, it will
# simply be re-wrapped during _dispatch_scalar.


######################
####    PUBLIC    ####
######################


cdef class SeriesWrapper:
    """A type-aware wrapper for ``pandas.Series`` objects.

    This is a context manager.  When used in a corresponding ``with``
    statement, it offers a view into a ``pandas.Series`` object that strips
    certain problematic information from it.  Operations can then be performed
    on the wrapped series without considering these special cases, which are
    automatically handled when the wrapper leaves its context.

    Parameters
    ----------
    series : pd.Series
        The series to be wrapped.
    hasnans : bool, default None
        Indicates whether missing values are present in the series.  This
        defaults to ``None``, meaning that missing values will be automatically
        detected when entering this object's ``with`` statement.  Explicitly
        setting this to ``False`` will skip this step, which may improve
        performance slightly.  If it is set to ``True``, then the indices of
        each missing value will still be detected, but operations will proceed
        as if some were found even if this is not the case.
    element_type : BaseType
        Specifies the element type of the series.  Only use this if you know
        in advance what elements are stored in the series.  Providing it does
        not change functionality, but may avoid a call to :func:`detect_type`
        in the context block's ``__enter__`` clause.

    Notes
    -----
    The information that is stripped by this object includes missing values,
    non-unique indices, and sparse/categorical extensions.  When a
    ``SeriesWrapper`` is invoked in a corresponding ``with`` statement, its
    index is replaced with a default ``RangeIndex`` and the old index is
    remembered.  Then, if ``hasnans`` is set to ``True`` or ``None``, missing
    values will be detected by running ``pd.isna()`` on the series.  If any
    are found, they are dropped from the series automatically, leaving the
    index unchanged.

    When the context block is exited, a new series is constructed with the
    same size as the original, but filled with missing values.  The wrapped
    series - along with any transformations that may have been applied to it -
    are then laid into this NA series, aligning on index.  The index is then
    replaced with the original, and if its ``element_type`` is unchanged, any
    sparse/categorical extensions are dynamically reapplied.  The result is a
    series that is identical to the original, accounting for any
    transformations that are applied in the body of the context block.

    One thing to note about this approach is that if a transformation **removes
    values** from the wrapped series while within the context block, then those
    values will be automatically replaced with **missing values** according to
    its ``element_type.na_value`` field.  This is useful if the wrapped logic
    coerces some of the series into missing values, as is the case when using
    the ``errors="coerce"`` argument of the various
    :ref:`conversion functions <conversions>`.  This behavior allows the
    wrapped logic to proceed *without accounting for missing values*, which
    will never be introduced unexpectedly.

    This object is an example of the Gang of Four's
    `Decorator Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_,
    which is not to be confused with python language decorators.  The wrapper
    itself "sticky", meaning that any method that produces a ``pandas.Series``
    from this wrapper will be wrapped in turn, allowing users to manipulate
    them as if they were ``pandas.Series`` objects directly without worrying
    about re-wrapping the results whenever a method is applied.

    Lastly, this object implements several convenience methods that automate
    common tasks in wrapped logic.  See each method for details.
    """

    def __init__(
        self,
        series: pd.Series,
        element_type: type_specifier = None,
    ):
        self.series = as_series(series)

    ##########################
    ####    PROPERTIES    ####
    ##########################

    @property
    def series(self) -> pd.Series:
        """Retrieve the wrapped ``pandas.Series`` object.

        Every attribute/method that is not explicitly listed in the
        :class:`SeriesWrapper` documentation is delegated to this object.

        Parameters
        ----------
        val : pandas.Series
            Reassign the wrapped series.

        Returns
        -------
        pandas.Series
            The series being wrapped.
        """
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

    def astype(
        self,
        dtype: type_specifier,
        errors: str = "raise"
    ) -> SeriesWrapper:
        """``astype()`` equivalent for SeriesWrapper instances that works for
        object-based type specifiers.

        Parameters
        ----------
        dtype : type specifier
            The type to convert to.  This can be in any format recognized by
            :func:`resolve_type`.
        errors : str, default "raise"
            The error-handling rule to use if errors are encountered during
            the conversion.  Must be one of "raise", "ignore", or "coerce".

        Returns
        -------
        SeriesWrapper
            The result of the conversion.

        Notes
        -----
        ``errors="raise"`` and ``errors="ignore"`` both indicate that any
        errors should be propagated up the call stack.  In the case of
        ``"ignore"``, it is the caller's job to handle the raised error.
        ``errors="coerce"`` indicates that any offending values should be
        removed from the series (and therefore replaced with missing values).
        """
        dtype = resolve.resolve_type(dtype)
        if isinstance(dtype, types.CompositeType):
            raise ValueError(f"`dtype` must be atomic, not {repr(dtype)}")

        # apply dtype.type_def elementwise if not astype-compliant
        target = dtype.dtype
        if isinstance(target, abstract.AbstractDtype):
            return self.apply_with_errors(
                dtype.type_def,
                errors=errors,
                element_type=dtype
            )

        # default to pd.Series.astype()
        if self.series.dtype.kind == "O" and hasattr(target, "numpy_dtype"):
            # NOTE: pandas doesn't like converting arbitrary objects to
            # nullable extension types.  Luckily, numpy has no such problem,
            # and SeriesWrapper automatically filters out NAs.
            result = self.series.astype(target.numpy_dtype).astype(target)
        else:
            result = self.series.astype(target)

        return SeriesWrapper(
            result,
            element_type=dtype
        )

    def copy(self, *args, **kwargs) -> SeriesWrapper:
        """Duplicate a SeriesWrapper."""
        return SeriesWrapper(
            self.series.copy(*args, **kwargs),
            hasnans=self._hasnans,
            element_type=self._element_type
        )

    def __getattr__(self, name: str) -> Any:
        """`Decorator Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`
        dynamic wrapper for attribute lookups.

        This delegates all attribute lookups to the wrapped series and
        re-wraps the results if they are returned as ``pandas.Series`` objects.
        """
        attr = getattr(self.series, name)

        # method
        if callable(attr):

            @wraps(attr)
            def wrapper(*args, **kwargs):
                """A decorator (lowercase D) that re-wraps series outputs."""
                result = attr(*args, **kwargs)
                if isinstance(result, pd.Series):
                    return SeriesWrapper(result, hasnans=self._hasnans)
                return result

            return wrapper

        # attribute
        if isinstance(attr, pd.Series):
            return SeriesWrapper(attr, hasnans=self._hasnans)
        return attr

    ###########################
    ####    NEW METHODS    ####
    ###########################

    def apply_with_errors(
        self,
        call: Callable,
        errors: str = "raise",
        element_type: type_specifier = None
    ) -> SeriesWrapper:
        """Apply a callable over the series using the specified error handling
        rule at each index.

        Parameters
        ----------
        call : Callable
            The callable to apply.
        errors : str, default "raise"
            The error-handling rule to use if errors are encountered during
            the conversion.  Must be one of "raise", "ignore", or "coerce".
        element_type : ScalarType, default None   
            The element type of the returned series.  Only use this argument if
            the final type is known ahead of time.

        Returns
        -------
        SeriesWrapper
            The result of applying ``call`` at each index.

        Notes
        -----
        ``errors="raise"`` and ``errors="ignore"`` both indicate that any
        errors should be propagated up the call stack.  In the case of
        ``errors="ignore"``, it is the caller's job to handle the raised error.
        ``errors="coerce"`` indicates that any offending values should be
        removed from the series (and therefore replaced with missing values).
        """
        # loop over numpy array
        result, has_errors, index = _apply_with_errors(
            self.series.to_numpy(dtype=object),
            call=call,
            errors=errors
        )

        # remove coerced NAs.  NOTE: has_errors=True only when errors="coerce"
        series_index = self.index
        if has_errors:
            result = result[~index]
            series_index = series_index[~index]

        # apply final output type
        if element_type is None:
            result = pd.Series(
                result,
                index=series_index,
                dtype=object
            )
        else:
            element_type = resolve.resolve_type(element_type)
            if isinstance(element_type, types.CompositeType):
                raise ValueError(
                    f"`dtype` must be atomic, not {repr(element_type)}"
                )
            result = pd.Series(
                result,
                index=series_index,
                dtype=element_type.dtype
            )

        # return as SeriesWrapper
        return SeriesWrapper(
            result,
            hasnans=has_errors or self._hasnans,
            element_type=element_type
        )

    def rectify(self) -> SeriesWrapper:
        """If a :class:`SeriesWrapper`'s ``.dtype`` field does not match
        ``self.element_type.dtype``, then ``astype()`` it to match.

        This method is used to convert a ``dtype: object`` series to a standard
        numpy/pandas data type.
        """
        element_type = detect.detect_type(self)
        if self.series.dtype != element_type.dtype:
            self.series = self.series.astype(element_type.dtype)
        return self

    ##########################
    ####    ARITHMETIC    ####
    ##########################

    # NOTE: math operators can change the element_type of a SeriesWrapper in
    # unexpected ways.  If you know the final element_type ahead of time, set
    # it manually after the operation by assigning to the result's
    # .element_type field.  Otherwise it will automatically be forgotten and
    # regenerated when you next request it.

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


cpdef object as_series(object data):
    """Convert arbitrary data into a corresponding pd.Series object."""
    # pandas Series
    if isinstance(data, pd.Series):
        return data

    # SeriesWrapper
    if isinstance(data, SeriesWrapper):
        return data.series

    # numpy array
    if isinstance(data, np.ndarray):
        return pd.Series(np.atleast_1d(data))

    # scalar or non-array iterable
    return pd.Series(data, dtype="O")
