from collections import Counter
import numbers
import sys
from typing import Any, Iterator

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd
import pandas.core.algorithms as algorithms
from pandas.api.extensions import register_extension_dtype
from pandas.core.arrays import ExtensionArray, ExtensionScalarOpsMixin
from pandas.core.dtypes.base import ExtensionDtype
from pandas.core.dtypes.generic import ABCDataFrame, ABCIndex, ABCSeries

import pdcast.convert as convert


# __setitem__ needs to account for nans.


######################
####    PUBLIC    ####
######################


def construct_extension_dtype(
    atomic_type,
    is_boolean: bool,
    is_numeric: bool,
    add_comparison_ops: bool,
    add_arithmetic_ops: bool,
    kind: str = "O",
    nullable: bool = True,
    common_dtype: np.dtype | ExtensionDtype = None
) -> pd.api.extensions.ExtensionDtype:
    """Construct a new pandas ``ExtensionDtype`` to refer to elements
    of this type.

    This method is only invoked if no explicit ``dtype`` is assigned to
    this ``AtomicType``.
    """
    _kind = kind
    class_doc = (
        f"An abstract data type, automatically generated to store\n "
        f"{atomic_type.type_def} objects."
    )

    @register_extension_dtype
    class ImplementationDtype(AbstractDtype):
        _atomic_type = atomic_type
        name = str(atomic_type)
        type = atomic_type.type_def
        kind = _kind
        na_value = atomic_type.na_value
        _is_boolean = is_boolean
        _is_numeric = is_numeric
        _can_hold_na = nullable

        @classmethod
        def construct_array_type(cls):
            return construct_array_type(
                atomic_type,
                add_arithmetic_ops=add_arithmetic_ops,
                add_comparison_ops=add_comparison_ops
            )

        def _get_common_dtype(self, dtypes: list):
            if len(set(dtypes)) == 1:  # only itself
                return self
            else:
                return common_dtype

    ImplementationDtype.__doc__ = class_doc
    return ImplementationDtype()


def construct_array_type(
    atomic_type,
    add_arithmetic_ops: bool = True,
    add_comparison_ops: bool = True
) -> ExtensionArray:
    """Create a new ExtensionArray definition to store objects of the given
    type.
    """
    class_doc = (
        f"An abstract data type, automatically generated to store\n "
        f"{str(atomic_type)} objects."
    )

    class ImplementationArray(AbstractArray):
        
        def __init__(self, *args, **kwargs):
            self._atomic_type = atomic_type
            super().__init__(*args, **kwargs)

    # add scalar operations from ExtensionScalarOpsMixin
    if add_arithmetic_ops:
        ImplementationArray._add_arithmetic_ops()
    if add_comparison_ops:
        ImplementationArray._add_comparison_ops()

    # replace docstring
    ImplementationArray.__doc__ = class_doc
    return ImplementationArray


#######################
####    PRIVATE    ####
#######################


class AbstractDtype(ExtensionDtype):
    """Base class for automatically-generated ExtensionDtype definitions.

    This class allows :class:`AtomicType` definitions that do not define an
    explicit ``.dtype`` field to automatically generate one according to
    `existing pandas guidelines <https://pandas.pydata.org/pandas-docs/stable/development/extending.html>`_.
    The resulting arrays are essentially identical to ``dtype: object`` arrays,
    but are explicitly labeled and have better integration with base pandas.
    They may also be slightly more performant in some cases.
    """

    def __init__(self):
        # require use of construct_extension_dtype() factory
        if not hasattr(self, "_atomic_type"):
            raise NotImplementedError(
                f"AbstractDtype must have an associated AtomicType"
            )

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


class AbstractArray(ExtensionArray, ExtensionScalarOpsMixin):
    """Base class for automatically-generated ExtensionArray definitions.

    This class allows :class:`AtomicType` definitions that do not define an
    explicit ``.dtype`` field to automatically generate one according to the
    `existing pandas guidelines <https://pandas.pydata.org/pandas-docs/stable/development/extending.html>`_.
    The resulting arrays are essentially identical to ``dtype: object`` arrays,
    but are explicitly labeled and have better integration with base pandas.
    They may also be slightly more performant in some cases.
    """
    __array_priority__ = 1000  # this is used in pandas test code

    def __init__(self, values, dtype=None, copy=False):
        # NOTE: this does NOT coerce inputs; that's handled by cast() itself
        self._data = np.asarray(values, dtype=object)

        # aliases for common attributes to ensure pandas support
        self._items = self.data = self._data

        # require use of construct_array_type() factory
        if not hasattr(self, "_atomic_type"):
            raise NotImplementedError(
                f"AbstractArray must have an associated AtomicType"
            )

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

        Notes
        -----
        This **does not** coerce results to the specified type.  For
        conversions, see the docs on :func:`cast` and its related functions.
        All this does is wrap the scalars in an object array.
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
            value = np.asarray(convert.cast(value, self._atomic_type))
        else:
            value = convert.cast(value, self._atomic_type)[0]
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

    def __eq__(self, other: Any):  # type: ignore[override]
        """Return for `self == other` (element-wise equality)."""
        if isinstance(other, (pd.Series, pd.Index, pd.DataFrame)):
            return NotImplemented
        return self._data == other

    @property
    def dtype(self):
        """An instance of 'ExtensionDtype'."""
        return self._atomic_type.dtype

    @property
    def _dtype(self):
        """An alias for ``self.dtype`` that ensures pandas compatibility."""
        return self.dtype

    @property
    def nbytes(self):
        """The number of bytes needed to store this object in memory."""
        n = len(self)
        if n:
            return n * sys.getsizeof(self[0])
        return 0

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
            else:
                return self.copy()

        if isinstance(dtype, ExtensionDtype):
            cls = dtype.construct_array_type()
            if isinstance(dtype, AbstractDtype):
                result = convert.cast(self, dtype).array
            else:
                result = self
            return cls._from_sequence(self, dtype=dtype, copy=copy)

        return self._data.astype(dtype=dtype, copy=copy)

    def isna(self):
        return boolean_apply(self._data, self._atomic_type.is_na)

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
            item_type = self._atomic_type.type_def
            return item_type(0)

        try:
            op = getattr(self.data, name)
        except AttributeError:
            raise NotImplementedError(
                f"{str(self._atomic_type)} does not support the {name} "
                f"operation"
            )
        return op(axis=0)

    def __array_ufunc__(self, ufunc: np.ufunc, method: str, *inputs, **kwargs):
        """Allows AbstractArrays to utilize numpy ufuncs natively, without
        being coerced to dtype: object.

        This implementation is adapted from the example given by numpy.  It
        might require further modifications.

        https://numpy.org/doc/stable/reference/generated/numpy.lib.mixins.NDArrayOperatorsMixin.html
        """
        # pandas unboxes these so we don't need to implement them ourselves
        if any(
            isinstance(other, (ABCSeries, ABCIndex, ABCDataFrame))
            for other in inputs
        ):
            return NotImplemented

        # get handled values
        array_like = (np.ndarray, ExtensionArray)
        scalar_like = (self._atomic_type.type_def,)
        if self._atomic_type._is_numeric:
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

    def round(self, *args, **kwargs) -> ExtensionArray:
        """Default round implementation.  This simply passes through to
        np.round().
        """
        return self._from_sequence(self._data.round(*args, **kwargs))

    def value_counts(self, dropna: bool = True) -> pd.Series:
        """Returns a Series containing counts of each unique value.

        Parameters
        ----------
        dropna : bool, default True
            Don't include counts of missing values.

        Returns
        -------
        counts : Series

        See Also
        --------
        Series.value_counts
        """
        # compute counts without nans
        mask = self.isna()
        counts = Counter(self._data[~mask])
        result = pd.Series(
            counts.values(),
            index=pd.Index(list(counts.keys()), dtype=self.dtype)
        )
        if dropna:
            return result.astype("Int64")

        # if we want to include nans, count mask
        nans = pd.Series(
            [mask.sum()],
            index=pd.Index([self.dtype.na_value], dtype=self.dtype)
        )
        result = pd.concat([result, nans])
        return result.astype("Int64")

    ###############################
    ####    UNARY OPERATORS    ####
    ###############################

    def __neg__(self):
        # NOTE: shim allows unary negation (-)
        if self._atomic_type._is_boolean:
            return self.__invert__()
        return self._from_sequence(-self._data)

    def __pos__(self):
        # NOTE: shim allows unary +
        return self._from_sequence(+self._data)

    def __invert__(self):
        # NOTE: shim allows unary inversion (~)
        if self._atomic_type._is_boolean:
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


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[np.uint8_t, cast=True] boolean_apply(
    np.ndarray[object] arr,
    object call
):
    cdef unsigned int arr_length = arr.shape[0]
    cdef np.ndarray[np.uint8_t, cast=True] result
    cdef unsigned int i

    result = np.empty(arr_length, dtype=bool)

    for i in range(arr_length):
        result[i] = call(arr[i])

    return result
