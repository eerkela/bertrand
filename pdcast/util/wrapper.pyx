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

import pdcast.convert.standalone as standalone

import pdcast.util.array as array
from pdcast.util.error import shorten_list
from pdcast.util.type_hints import array_like, numeric, type_specifier


# TODO: tol should clip overflowing values if they are within the window.
# -> force boundscheck to accept ``tol``.


# TODO: SparseType works, but not in all cases.
# -> pd.NA disallows non-missing fill values
# -> Timestamps must be sparsified manually by converting to object and then
# to sparse
# -> Timedeltas just don't work at all.  astype() rejects pd.SparseDtype("m8")
# entirely.


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
        hasnans: bool = None,
        element_type: types.BaseType = None,
    ):
        self.series = series
        self.hasnans = hasnans
        self.element_type = element_type

    ##########################
    ####    PROPERTIES    ####
    ##########################

    @property
    def element_type(self) -> types.BaseType:
        """The inferred type of the series.

        Parameters
        ----------
        val : type specifier
            A new element type to assign to the series.  Note that this does
            not perform any conversions, it merely re-labels the
            ``SeriesWrapper``'s ``element_type`` field.  This can be in any
            format recognized by :func:`resolve_type`

        Returns
        -------
        BaseType
            The inferred type of the series.

        See Also
        --------
        detect_type : type inference from example data.
        """
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
        """Indicates whether missing values were detected in the series.

        Parameters
        ----------
        val : bool
            Allows users to override this setting during wrapped logic.  This
            may be useful if a transformation drops values from the series or
            otherwise injects missing values into it.

        Returns
        -------
        bool
            ``True`` if missing values were detected in the wrapped series.
            ``False`` otherwise.
        """
        if self._hasnans is None:
            self._hasnans = self.isna().any()
        return self._hasnans

    @hasnans.setter
    def hasnans(self, val: bool) -> None:
        self._hasnans = val

    @property
    def imag(self) -> SeriesWrapper:
        """Get the imaginary component of the wrapped series.

        This is a convenience attribute that mimics the behavior of
        ``numpy.imag()``, but wraps the output as a new ``SeriesWrapper``
        instance.

        Returns
        -------
        SeriesWrapper
            The imaginary component of the series.

        See Also
        --------
        SeriesWrapper.real : real equivalent.
        """
        target = getattr(self.element_type, "equiv_float", self.element_type)

        # NOTE: np.imag() fails when applied over object arrays that may
        # contain complex values.  In this case, we reduce it to a loop.
        if self.series.dtype.kind == "O":
            result = np.frompyfunc(np.imag, 1, 1)(self.series)
            if isinstance(target.dtype, array.AbstractDtype):
                result = result.astype(target.dtype)
        else:
            result = pd.Series(np.imag(self.series), index=self.index)

        return SeriesWrapper(
            result,
            hasnans=self._hasnans,
            element_type=target
        )

    @property
    def max(self) -> Any:
        """A cached version of pd.Series.max()."""
        if self._max is None:
            self._max = self.series.max()
        return self._max

    @property
    def min(self) -> Any:
        """A cached version of pd.Series.min()."""
        if self._min is None:
            self._min = self.series.min()
        return self._min

    @property
    def real(self) -> SeriesWrapper:
        """Get the real component of the wrapped series.

        This is a convenience attribute that mimics the behavior of
        ``numpy.real()``, but wraps the output as a new ``SeriesWrapper``
        instance.

        Returns
        -------
        SeriesWrapper
            The real component of the series.

        See Also
        --------
        SeriesWrapper.imag : imaginary equivalent.
        """
        target = getattr(self.element_type, "equiv_float", self.element_type)

        # NOTE: np.real() fails when applied over object arrays that may
        # contain complex values.  In this case, we reduce it to a loop.
        if self.series.dtype.kind == "O":
            result = np.frompyfunc(np.real, 1, 1)(self.series)
            if isinstance(target.dtype, array.AbstractDtype):
                result = result.astype(target.dtype)
        else:
            result = pd.Series(np.real(self.series), index=self.index)

        return SeriesWrapper(
            result,
            hasnans=self._hasnans,
            element_type=target
        )

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
        target = dtype.dtype

        # apply dtype.type_def elementwise if not astype-compliant
        if (
            target.kind == "O" and
            dtype.type_def != object and
            not isinstance(target, pd.StringDtype)
        ):
            return self.apply_with_errors(
                call=lambda x: (
                    x if isinstance(x, dtype.type_def) else dtype.type_def(x)
                ),
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

    ###########################
    ####    NEW METHODS    ####
    ###########################

    def __enter__(self) -> SeriesWrapper:
        """Enter a :class:`SeriesWrapper`'s context block.

        This strips problematic information from the series.  See the
        :class:`SeriesWrapper` documentation for details.
        """
        # record shape, name
        self._orig_shape = self.series.shape
        self._orig_name = self.series.name

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

        # enter context block
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Exit a :class:`SeriesWrapper`'s context block.

        This replaces the information that was stripped in
        :meth:`SeriesWrapper.__enter__`.  See the :class:`SeriesWrapper`
        documentation for more details.
        """
        # replace missing values, aligning on index
        if self.hasnans:
            result = pd.Series(
                np.full(
                    self._orig_shape,
                    getattr(self.element_type, "na_value", pd.NA),
                    dtype="O"
                ),
                dtype=object
            )
            result.update(self.series)
            self.series = result.astype(self.dtype, copy=False)

        # replace original index
        if self._orig_index is not None:
            self.series.index = self._orig_index
            self._orig_index = None

        # replace original name
        self.series.name = self._orig_name

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

    def apply_with_errors(
        self,
        call: Callable,
        errors: str = "raise",
        element_type: types.ScalarType = None
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

    def boundscheck(
        self,
        dtype: types.ScalarType,
        errors: str = "raise"
    ) -> tuple[SeriesWrapper, types.ScalarType]:
        """Ensure that the series fits within the allowable range of a given
        type.

        If overflow is detected, this function will attempt to
        :meth:`upcast <AtomicType.upcast>` the data type to fit the series.  If
        this fails and ``errors="coerce"``, then it will drop overflowing
        values from the series to fit the data type instead.

        Parameters
        ----------
        dtype : ScalarType
            An :class:`AtomicType` or :class:`AdapterType` whose range will be
            used for the check.  May be upcasted.
        errors : str, default "raise"
            The error-handling rule to apply to the range check.  Must be one
            of "raise", "ignore", or "coerce".

        Returns
        -------
        series : SeriesWrapper
            A series whose elements fit within the range of the specified type.
            In most cases, this will be the original series, but if overflow is
            detected and ``errors="coerce"``, then it may be a subset of the
            original.
        dtype : ScalarType
            A type that fits the observed range of the series.  In most cases,
            this will be the original data type, but if overflow is detected
            and the type is upcastable, then it may be larger.

        Raises
        ------
        OverflowError
            If ``dtype`` cannot fit the observed range of the series, cannot
            be upcasted to fit, and ``errors != "coerce"``

        See Also
        --------
        AtomicType.upcast : upcast a data type to fit a series.

        Notes
        -----
        In most cases, this is a simple identity function.  It only changes the
        inputs in the event that overflow is detected.
        """
        series = self

        # NOTE: we convert to python int to prevent inconsistent comparisons
        if self.element_type is not None:
            min_val = int(series.min - bool(series.min % 1))  # round floor
            max_val = int(series.max + bool(series.max % 1))  # round ceiling

            # check for overflow
            if min_val < dtype.min or max_val > dtype.max:
                try:  # attempt to upcast dtype to fit series
                    return series, dtype.upcast(series)
                except OverflowError:
                    pass

                # TODO: clip to within tolerance?
                # if min_val > dtype.min - tol and max_val < dtype.max + tol:
                #     series.clip(dtype.min, dtype.max)
                # else:
                #     see below

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

    def isinf(self) -> SeriesWrapper:
        """Return a boolean mask indicating the position of infinities in the
        series.

        This works exactly like ``SeriesWrapper.isna()``, but checks for infs
        rather than NAs.
        """
        return self.isin([np.inf, -np.inf])

    def rectify(self) -> SeriesWrapper:
        """If a :class:`SeriesWrapper`'s ``.dtype`` field does not match
        ``self.element_type.dtype``, then ``astype()`` it to match.

        This method is used to convert a ``dtype: object`` series to a standard
        numpy/pandas data type.
        """
        if self.series.dtype != self.element_type.dtype:
            self.series = self.series.astype(self.element_type.dtype)
        return self

    def within_tol(self, other, tol: numeric) -> SeriesWrapper:
        """Check if every element of a series is within tolerance of a given
        value or other series.

        This is used to detect precision loss during :ref:`conversions`.

        Parameters
        ----------
        other : numeric, np.array, pd.Series, or SeriesWrapper
            The value to compare against.
        tol : numeric
            The available tolerance.  If any elements of the series differ from
            ``other`` by more than this amount, then the corresponding index in
            the result will be set to ``False``.

        Returns
        -------
        SeriesWrapper
            A boolean mask indicating which elements of ``self`` are within
            tolerance of ``other``.
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
    # regenerated when you next request it (which is safer).

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
