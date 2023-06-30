"""Experimental support for numpy.datetime64-backed extension arrays.

It is not currently possible to support these due to limitations in the way
pandas formats repr() strings for M8 arrays.
"""
import numbers
from typing import Any, Iterator

import numpy as np
import pandas as pd
from pandas.api.extensions import register_extension_dtype
from pandas.core.arrays import ExtensionArray, ExtensionScalarOpsMixin
from pandas.core.dtypes.base import ExtensionDtype
from pandas.core.dtypes.generic import ABCDataFrame, ABCIndex, ABCSeries

import pdcast.convert as convert

import pdcast.util.time as time
from pdcast.util.type_hints import type_specifier


# NOTE: it is currently not possible to store np.datetime64 objects in a
# pandas series with units other than `1ns`.  Pandas internally formats these
# by checking the ExtensionDtype's `kind` and `type` attributes, which can
# be worked around.  What cannot be worked around is when pandas formats the
# underlying array itself, which we have no control over.  The only way to
# bypass this would be to change the behavior of np.asarray() on our
# ExtensionArrays or store them in a non-M8 container, which negates all the
# benefits of making an explicit ExtensionDtype in the first place.  As such,
# this module is currently dead in the water, though I haven't deleted it in
# case the internal pandas implementation changes in the future.


@register_extension_dtype
class M8Dtype(ExtensionDtype):
    """An ``ExtensionDtype`` that stores literal ``np.datetime64`` objects."""

    # NOTE: can't use kind="M" or type=np.datetime64 because it causes pandas
    # to auto-format results as pd.Timestamp objects during repr() calls, which
    # fails for any units other than 1ns.

    kind = "O"  # workaround for above
    type = type(None)  # workaround for above
    itemsize = 8
    na_value = np.datetime64("nat")
    _is_boolean = False
    _is_numeric = False
    _can_hold_na = True
    _metadata = ("unit", "step_size")

    def __init__(self, unit: str = "ns", step_size: int = 1):
        if unit not in time.valid_units:
            raise TypeError(
                f"unit must be one of {time.valid_units}, not {repr(unit)}"
            )
        if step_size < 1:
            raise TypeError(f"step_size must be >= 1, not {step_size}")
        self.unit = unit
        self.step_size = step_size

    @property
    def name(self) -> str:
        if self.step_size == 1:
            return f"M8[{self.unit}]"
        return f"M8[{self.step_size}{self.unit}]"

    @classmethod
    def construct_array_type(cls):
        return M8Array

    @classmethod
    def construct_from_string(cls: type, string: str) -> ExtensionDtype:
        """Construct this type from a string.

        This is useful mainly for data types that accept parameters.
        For example, a period dtype accepts a frequency parameter that
        can be set as ``period[H]`` (where H means hourly frequency).
        By default, in the abstract class, just the name of the type is
        expected. But subclasses can overwrite this method to accept
        parameters.

        Parameters
        ----------
        string : str
            The name of the type, for example ``category``.

        Returns
        -------
        ExtensionDtype
            Instance of the dtype.

        Raises
        ------
        TypeError
            If a class cannot be constructed from this 'string'.

        Examples
        --------
        For extension dtypes with arguments the following may be an
        adequate implementation.

        >>> import re
        >>> @classmethod
        ... def construct_from_string(cls, string):
        ...     pattern = re.compile(r"^my_type\[(?P<arg_name>.+)\]$")
        ...     match = pattern.match(string)
        ...     if match:
        ...         return cls(**match.groupdict())
        ...     else:
        ...         raise TypeError(
        ...             f"Cannot construct a '{cls.__name__}' from "
        ...             f"'{string}'"
        ...         )
        """
        if not isinstance(string, str):
            raise TypeError(
                f"'construct_from_string' expects a string, got {type(string)}"
            )

        # disable string construction - use ``pdcast.resolve_type()`` instead
        raise TypeError(f"Cannot construct a '{cls.__name__}' from '{string}'")

    def _get_common_dtype(self, dtypes: list):
        if len(set(dtypes)) == 1:  # only itself
            return self
        return np.dtype(f"M8[{self.step_size}{self.unit}]")

    def __repr__(self) -> str:
        return f"M8Dtype(unit={repr(self.unit)}, step_size={self.step_size})"


class M8Array(ExtensionArray, ExtensionScalarOpsMixin):
    """An ``ExtensionArray`` for storing literal ``np.datetime64`` objects."""

    __array_priority__ = 1000  # this is used in pandas test code

    def __init__(self, values, dtype=None, copy=False):
        self._data = np.asarray(values, dtype=dtype._get_common_dtype([]))

        # aliases for common attributes to ensure pandas support
        self._items = self.data = self._ndarray = self._data
        self._dtype = dtype

        self.tz = None

    @classmethod
    def _from_sequence(cls, scalars, dtype=None, copy=False):
        """Construct a new ExtensionArray from a sequence of scalars.

        Parameters
        ----------
        scalars : Sequence
            Each element will be an instance of the scalar type for this
            array, ``cls.dtype.type`` or be converted into this type in this
            method.
        dtype : dtype, optional
            Construct for this particular dtype.  This should be a Dtype
            compatible with the ExtensionArray.
        copy : bool, default False
            If True, copy the underlying data.

        Returns
        -------
        ExtensionArray
        """
        return cls(scalars, dtype=dtype, copy=copy)

    @classmethod
    def _from_factorized(cls, values, original):
        """Reconstruct an ExtensionArray after factorization.

        Parameters
        ----------
        values : ndarray
            An integer ndarray with the factorized values.
        original : ExtensionArray
            The original ExtensionArray that factorize was called on.

        See Also
        --------
        factorize : Top-level factorize method that dispatches here.
        ExtensionArray.factorize : Encode the array as an enumerated type.
        """
        return cls(values, dtype=original.dtype)

    def __getitem__(self, item):
        """Select a subset of self.

        Parameters
        ----------
        item : int, slice, or ndarray
            * int: The position in 'self' to get.
            * slice: A slice object, where 'start', 'stop', and 'step' are
              integers or None
            * ndarray: A 1-d boolean NumPy ndarray the same length as 'self'
            * list[int]:  A list of int

        Returns
        -------
        item : scalar or ExtensionArray

        Notes
        -----
        For scalar ``item``, return a scalar value suitable for the array's
        type. This should be an instance of ``self.dtype.type``.

        For slice ``key``, return an instance of ``ExtensionArray``, even
        if the slice is length 0 or 1.

        For a boolean mask, return an instance of ``ExtensionArray``, filtered
        to the values where ``item`` is True.
        """
        if isinstance(item, numbers.Integral):
            return self._data[item]
        return type(self)(self._data[item])

    def __setitem__(self, key, value):
        """Set one or more values inplace.

        This method is not required to satisfy the pandas extension array
        interface.

        Parameters
        ----------
        key : int, ndarray, or slice
            When called from, e.g. ``Series.__setitem__``, ``key`` will be
            one of
            * scalar int
            * ndarray of integers.
            * boolean ndarray
            * slice object
        value : ExtensionDtype.type, Sequence[ExtensionDtype.type], or object
            value or values to be set of ``key``.

        Returns
        -------
        None
        """
        if pd.api.types.is_list_like(value):
            if pd.api.types.is_scalar(key):
                raise ValueError(
                    "setting an array element with a sequence."
                )
            value = np.asarray(convert.cast(value, self._pdcast_type))
        # else:
        #     value = convert.cast(value, self._pdcast_type)[0]
        self._data[key] = value

    def __len__(self) -> int:
        """Length of this array

        Returns
        -------
        length : int
        """
        return len(self._data)

    def __iter__(self) -> Iterator[Any]:
        """Iterate over elements of the array."""
        return iter(self._data)

    def __eq__(self, other: type_specifier):
        """Return for `self == other` (element-wise equality)."""
        if isinstance(other, (pd.Series, pd.Index, pd.DataFrame)):
            return NotImplemented
        return self._data == other

    # def __repr__(self) -> str:
    #     if self.ndim > 1:
    #         return self._repr_2d()

    #     return "hello world"


    # def _formatter(self, boxed: bool = False) -> Callable[[Any], str | None]:
    #     """Formatting function for scalar values.

    #     This is used in the default '__repr__'. The returned formatting
    #     function receives instances of your scalar type.

    #     Parameters
    #     ----------
    #     boxed : bool, default False
    #         An indicated for whether or not your array is being printed
    #         within a Series, DataFrame, or Index (True), or just by
    #         itself (False). This may be useful if you want scalar values
    #         to appear differently within a Series versus on its own (e.g.
    #         quoted or not).

    #     Returns
    #     -------
    #     Callable[[Any], str]
    #         A callable that gets instances of the scalar type and
    #         returns a string. By default, :func:`repr` is used
    #         when ``boxed=False`` and :func:`str` is used when
    #         ``boxed=True``.
    #     """
    #     return lambda *args, **kwargs: "hello world"

    @property
    def dtype(self):
        """An instance of 'ExtensionDtype'."""
        return self._dtype

    @property
    def nbytes(self):
        """The number of bytes needed to store this object in memory."""
        return self.dtype.itemsize * len(self)

    def astype(self, dtype, copy: bool = True):
        """Cast to a NumPy array or ExtensionArray with 'dtype'.

        Parameters
        ----------
        dtype : str or dtype
            Typecode or data-type to which the array is cast.
        copy : bool, default True
            Whether to copy the data, even if not necessary. If False, a copy
            is made only if the old dtype does not match the new dtype.

        Returns
        -------
        array : np.ndarray or ExtensionArray
            An ExtensionArray if dtype is ExtensionDtype, otherwise a NumPy
            ndarray with 'dtype' for its dtype.
        """
        dtype = pd.api.types.pandas_dtype(dtype)
        if pd.core.dtypes.common.is_dtype_equal(dtype, self.dtype):
            if not copy:
                return self
            return self.copy()

        if isinstance(dtype, ExtensionDtype):
            cls = dtype.construct_array_type()
            return cls._from_sequence(self, dtype=dtype, copy=copy)

        return self._data.astype(dtype=dtype, copy=copy)

    def isna(self):
        return np.isnan(self._data)

    def _values_for_argsort(self) -> np.ndarray:
        """Return values for sorting.

        Returns
        -------
        ndarray
            The transformed values should maintain the ordering between values
            within the array.

        See Also
        --------
        ExtensionArray.argsort : Return the indices that would sort this array.

        Notes
        -----
        The caller is responsible for *not* modifying these values in-place, so
        it is safe for implementors to give views on `self`.

        Functions that use this (e.g. ExtensionArray.argsort) should ignore
        entries with missing values in the original array (according to
        `self.isna()`). This means that the corresponding entries in the
        returned array don't need to be modified to sort correctly.
        """
        # Note: this is used in `ExtensionArray.argsort/argmin/argmax`.
        return self._data

    def _values_for_factorize(self) -> tuple:
        """Return an array and missing value suitable for factorization.

        Returns
        -------
        values : ndarray
            An array suitable for factorization. This should maintain order
            and be a supported dtype (Float64, Int64, UInt64, String, Object).
            By default, the extension array is cast to object dtype.
        na_value : object
            The value in `values` to consider missing. This will be treated
            as NA in the factorization routines, so it will be coded as
            `na_sentinel` and not included in `uniques`.

        Notes
        -----
        The values returned by this method are also used in
        :func:`pandas.util.hash_pandas_object`.
        """
        return self._data, self.dtype.na_value

    def take(self, indexer, allow_fill=False, fill_value=None):
        """Take elements from an array.

        Parameters
        ----------
        indices : sequence of int or one-dimensional np.ndarray of int
            Indices to be taken.
        allow_fill : bool, default False
            How to handle negative values in `indices`.
            * False: negative values in `indices` indicate positional indices
              from the right (the default). This is similar to
              :func:`numpy.take`.
            * True: negative values in `indices` indicate
              missing values. These values are set to `fill_value`. Any other
              other negative values raise a ``ValueError``.
        fill_value : any, optional
            Fill value to use for NA-indices when `allow_fill` is True.
            This may be ``None``, in which case the default NA value for
            the type, ``self.dtype.na_value``, is used.
            For many ExtensionArrays, there will be two representations of
            `fill_value`: a user-facing "boxed" scalar, and a low-level
            physical NA value. `fill_value` should be the user-facing version,
            and the implementation should handle translating that to the
            physical version for processing the take if necessary.

        Returns
        -------
        ExtensionArray

        Raises
        ------
        IndexError
            When the indices are out of bounds for the array.
        ValueError
            When `indices` contains negative values other than ``-1``
            and `allow_fill` is True.
    
        See Also
        --------
        numpy.take : Take elements from an array along an axis.
        api.extensions.take : Take elements from an array.

        Notes
        -----
        ExtensionArray.take is called by ``Series.__getitem__``, ``.loc``,
        ``iloc``, when `indices` is a sequence of values. Additionally,
        it's called by :meth:`Series.reindex`, or any other method
        that causes realignment, with a `fill_value`.

        Examples
        --------
        Here's an example implementation, which relies on casting the
        extension array to object dtype. This uses the helper method
        :func:`pandas.api.extensions.take`.

        .. code-block:: python

           def take(self, indices, allow_fill=False, fill_value=None):
               from pandas.core.algorithms import take

               # If the ExtensionArray is backed by an ndarray, then
               # just pass that here instead of coercing to object.
               data = self.astype(object)

               if allow_fill and fill_value is None:
                   fill_value = self.dtype.na_value

               # fill value should always be translated from the scalar
               # type for the array, to the physical storage type for
               # the data, before passing to take.

               result = take(data, indices, fill_value=fill_value,
                             allow_fill=allow_fill)
               return self._from_sequence(result, dtype=self.dtype)
        """
        from pandas.core.algorithms import take

        data = self._data
        if allow_fill and fill_value is None:
            fill_value = self.dtype.na_value

        result = take(
            data,
            indexer,
            fill_value=fill_value,
            allow_fill=allow_fill
        )
        return self._from_sequence(result, dtype=self.dtype)

    def copy(self):
        """Return a copy of the array.

        Returns
        -------
        ExtensionArray
        """
        return type(self)(self._data.copy())

    @classmethod
    def _concat_same_type(cls, to_concat):
        """Concatenate multiple array of this dtype.

        Parameters
        ----------
        to_concat : sequence of this type

        Returns
        -------
        ExtensionArray
        """
        return cls(np.concatenate([x._data for x in to_concat]))

    def _reduce(self, name, skipna=True, **kwargs):
        """Return a scalar result of performing the reduction operation.

        Parameters
        ----------
        name : str
            Name of the function, supported values are:
            { any, all, min, max, sum, mean, median, prod,
            std, var, sem, kurt, skew }.
        skipna : bool, default True
            If True, skip NaN values.
        **kwargs
            Additional keyword arguments passed to the reduction function.
            Currently, `ddof` is the only supported kwarg.

        Returns
        -------
        scalar

        Raises
        ------
        TypeError : subclass does not define reductions
        """
        # NOTE: this implementation is taken from pandas test suite

        if skipna:
            # If we don't have any NAs, we can ignore skipna
            if self.isna().any():
                other = self[~self.isna()]
                return other._reduce(name, **kwargs)

        if name == "sum" and len(self) == 0:
            # GH#29630 avoid returning int 0 or np.bool_(False) on old numpy
            item_type = self._pdcast_type.type_def
            return item_type(0)

        try:
            op = getattr(self.data, name)
        except AttributeError:
            raise NotImplementedError(
                f"{str(self._pdcast_type)} does not support the {name} "
                f"operation"
            )
        return op(axis=0)

    def __array_ufunc__(self, ufunc: np.ufunc, method: str, *inputs, **kwargs):
        """Allows ObjectArrays to utilize numpy ufuncs natively, without
        being coerced to dtype: object.

        This implementation is adapted from the
        :class:`example <numpy.lib.mixins.NDArrayOperatorsMixin>` given by
        numpy.  It might require further modifications.
        """
        # pandas unboxes these so we don't need to implement them ourselves
        if any(
            isinstance(other, (ABCSeries, ABCIndex, ABCDataFrame))
            for other in inputs
        ):
            return NotImplemented

        # get handled values
        array_like = (np.ndarray, ExtensionArray)
        scalar_like = (self._pdcast_type.type_def,)
        if self.dtype._is_numeric:
            scalar_like += (numbers.Number,)

        if not all(isinstance(t, array_like + scalar_like) for t in inputs):
            return NotImplemented

        inputs = tuple(
            x._data if isinstance(x, ExtensionArray) else x for x in inputs
        )
        result = getattr(ufunc, method)(*inputs, **kwargs)

        def reconstruct(x):
            if isinstance(x, scalar_like):
                return x
            return type(self)._from_sequence(x)

        if isinstance(result, tuple):
            return tuple(reconstruct(x) for x in result)
        return reconstruct(result)

    def __arrow_array__(self, type=None):
        """Convert the underlying array values into a pyarrow Array.

        This method ensures that pyarrow knowns how to convert the specific
        extension array into a pyarrow.Array (also when included as a column in
        a pandas DataFrame).
        """
        import pyarrow

        return pyarrow.array(self._data, type=type)

    def __from_arrow__(self, array) -> ExtensionArray:
        """Convert a pyarrow array into a pandas ExtensionArray.

        This method controls the conversion back from pyarrow to a pandas
        ExtensionArray.  It receives a pyarrow Array or ChunkedArray as its
        only argument and is expected to return the appropriate pandas
        ExtensionArray for this dtype and the passed values
        """
        return self._from_sequence(array)

    ###########################
    ####    NEW METHODS    ####
    ###########################

    # NOTE: these are not defined in the base ExtensionArray class, but are
    # called in several pandas operations.

    # def round(self, *args, **kwargs) -> ExtensionArray:
    #     """Default round implementation.  This simply passes through to
    #     np.round().
    #     """
    #     return self._from_sequence(self._data.round(*args, **kwargs))

    # def value_counts(self, dropna: bool = True) -> pd.Series:
    #     """Returns a Series containing counts of each unique value.

    #     Parameters
    #     ----------
    #     dropna : bool, default True
    #         Don't include counts of missing values.

    #     Returns
    #     -------
    #     counts : Series

    #     See Also
    #     --------
    #     Series.value_counts
    #     """
    #     # compute counts without nans
    #     mask = self.isna()
    #     counts = Counter(self._data[~mask])
    #     result = pd.Series(
    #         counts.values(),
    #         index=pd.Index(list(counts.keys()), dtype=self.dtype)
    #     )
    #     if dropna:
    #         return result.astype("Int64")

    #     # if we want to include nans, count mask
    #     nans = pd.Series(
    #         [mask.sum()],
    #         index=pd.Index([self.dtype.na_value], dtype=self.dtype)
    #     )
    #     result = pd.concat([result, nans])
    #     return result.astype("Int64")

    ###############################
    ####    UNARY OPERATORS    ####
    ###############################

    def __neg__(self):
        # NOTE: shim allows unary negation (-)
        if self.dtype._is_boolean:
            return self.__invert__()
        return self._from_sequence(-self._data)

    def __pos__(self):
        # NOTE: shim allows unary +
        return self._from_sequence(+self._data)

    def __invert__(self):
        # NOTE: shim allows unary inversion (~)
        if self.dtype._is_boolean:
            return self.__xor__(True)
        return self._from_sequence(~self._data)

    ################################
    ####    BINARY OPERATORS    ####
    ################################

    def __and__(self, other):
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):
            return NotImplemented

        # NOTE: shim allows binary and (&)
        return self._from_sequence(self._data & other)

    def __xor__(self, other):
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):
            return NotImplemented

        # NOTE: shim allows binary xor (^)
        return self._from_sequence(self._data ^ other)

    def __or__(self, other):
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):
            return NotImplemented

        # NOTE: shim allows binary or (|)
        return self._from_sequence(self._data | other)


# add scalar ops via ExtensionScalarOpsMixin
M8Array._add_arithmetic_ops()
M8Array._add_comparison_ops()
