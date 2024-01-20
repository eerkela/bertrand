"""This module describes a mechanism for automatically generating pandas
``ExtensionDtype`` and ``ExtensionArray`` objects from an associated
``ScalarType``.

Classes
-------
ObjectDtype
    A pandas ``ExtensionDtype`` that delegates certain functionality to its
    associated ``ScalarType`` and comes with its own ``ExtensionArray``.

ObjectArray
    A pandas ``ExtensionArray`` that stores arbitrary objects in an
    explicitly-labeled ``dtype: object`` array.

Functions
---------
construct_object_dtype()
    A factory for ``ObjectDtype`` definitions.

construct_array_type()
    A factory for ``ObjectArray`` definitions.
"""
# pylint: disable=unused-argument
from __future__ import annotations
from collections import Counter
import numbers
import sys
from typing import Any, Iterable, Iterator, Literal, NoReturn

import numpy as np
import pandas as pd
from pandas.api.extensions import register_extension_dtype
from pandas.core.arrays import ExtensionArray, PandasArray
from pandas.core.dtypes.base import ExtensionDtype
from pandas.core.dtypes.generic import ABCDataFrame, ABCIndex, ABCSeries  # type: ignore

from pdcast.util.type_hints import array_like, dtype_like


# TODO: merge this with the equivalent stub


# TODO: run these through pandas test suite to see if anything is broken.
# -> need to implement _accumulate()


# TODO: use a classmethod to inject the ScalarType into the array definition rather
# than using a private attribute.  Maybe do the same with the corresponding dtype,
# although these have to stay unique for each ScalarType.  We can then just implement
# everything in the main definition and prevent lint errors.

# Now that we're in pure python, we can dynamically compute __doc__ strings based on
# the associated ScalarType.  This should be done for both the dtype and array


######################
####    PUBLIC    ####
######################


def construct_object_dtype(
    pdcast_type: Any,
    is_boolean: bool,
    is_numeric: bool,
    kind: valid_kinds = "O",
    nullable: bool = True,
    common_dtype: dtype_like | None = None
) -> ObjectDtype:
    """Construct a new pandas ``ExtensionDtype`` to refer to elements
    of this type.

    This method is only invoked if no explicit ``dtype`` is assigned to
    this ``ScalarType``.
    """
    # hack - allows us to use the same name for the global and local classes
    global ObjectDtype
    _ObjectDtype = ObjectDtype
    _kind = kind

    @register_extension_dtype
    class ObjectDtype(_ObjectDtype):

        _pdcast_type = pdcast_type
        name = str(pdcast_type)
        type = pdcast_type.type_def
        kind = _kind
        na_value = pdcast_type.na_value
        _is_boolean = is_boolean
        _is_numeric = is_numeric
        _can_hold_na = nullable

        @classmethod
        def construct_array_type(cls) -> type:
            """Build an ExtensionArray class to store objects of this dtype.

            This method automatically generates a new ObjectArray class
            specifically for this type.  These act like normal ``dtype: object``
            arrays, but are explicitly labeled with this dtype.
            """
            return construct_array_type(pdcast_type)

        def _get_common_dtype(self, dtypes: list) -> dtype_like | None:
            """Return the common dtype, if one exists.

            Used in `find_common_type` implementation. This is for example used
            to determine the resulting dtype in a concat operation.

            If no common dtype exists, return None (which gives the other
            dtypes the chance to determine a common dtype). If all dtypes in
            the list return None, then the common dtype will be "object" dtype
            (this means it is never needed to return "object" dtype from this
            method itself).

            Parameters
            ----------
            dtypes : list of dtypes
                The dtypes for which to determine a common dtype. This is a
                list of np.dtype or ExtensionDtype instances.

            Returns
            -------
            Common dtype (np.dtype or ExtensionDtype) or None
            """
            if len(set(dtypes)) == 1:  # only itself
                return self
            return common_dtype

    ObjectDtype.__doc__ = (
        f"An abstract data type, automatically generated to store\n "
        f"{pdcast_type.type_def} objects."
    )
    return ObjectDtype()


def construct_array_type(pdcast_type: Any) -> type[ObjectArray]:
    """Create a new ExtensionArray definition to store objects of the given
    type.
    """
    # hack - allows us to use the same name for the global and local classes
    global ObjectArray
    _ObjectArray = ObjectArray

    class ObjectArray(_ObjectArray):

        def __init__(self, *args, **kwargs):
            self._pdcast_type = pdcast_type
            super().__init__(*args, **kwargs)

    # replace docstring
    ObjectArray.__doc__ = (
        f"An abstract data type, automatically generated to store\n "
        f"{str(pdcast_type)} objects."
    )
    return ObjectArray


#######################
####    PRIVATE    ####
#######################


# pylint: disable=invalid-name
boolean_array = np.ndarray[np.bool_, np.dtype[np.bool_]]
valid_kinds = Literal['b', 'i', 'u', 'f', 'c', 'm', 'M', 'O', 'S', 'U', 'V']



class ObjectDtype(ExtensionDtype):
    """Base class for automatically-generated ExtensionDtype definitions.

    This class allows :class:`ScalarType` definitions that do not define an
    explicit ``.dtype`` field to automatically generate one according to
    :ref:`existing pandas guidelines <pandas:extending>`.  The resulting arrays
    are essentially identical to ``dtype: object`` arrays, but are explicitly
    labeled and have better integration with base pandas.  They may also be
    slightly more performant in some cases.
    """

    def __init__(self):
        # require use of construct_object_dtype() factory
        if not hasattr(self, "_pdcast_type"):
            raise NotImplementedError(
                "ObjectDtype must have an associated ScalarType"
            )

    @classmethod
    def construct_from_string(cls, string: str) -> NoReturn:
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
        # disable string construction - use ``pdcast.resolve_type()`` instead
        raise TypeError(f"Cannot construct a '{cls.__name__}' from '{string}'")

    def __getattr__(self, name: str) -> Any:
        """Delegate attribute lookups to the underlying ScalarType."""
        try:
            return self._pdcast_type.kwargs[name]
        except KeyError as err:
            err_msg = (
                f"{repr(type(self).__name__)} object has no attribute: "
                f"{repr(name)}"
            )
            raise AttributeError(err_msg) from err

    def __dir__(self) -> list:
        # direct ObjectDtype attributes
        result = dir(type(self))
        result += list(self.__dict__.keys())

        # ScalarType kwargs
        result += [x for x in self._pdcast_type.kwargs if x not in result]
        return result

    def __repr__(self) -> str:
        return f"{type(self).__name__}({str(self._pdcast_type)})"


class ObjectArray(ExtensionArray):
    """Base class for automatically-generated ExtensionArray definitions.

    This class allows :class:`ScalarType` definitions that do not define an
    explicit ``.dtype`` field to automatically generate one according to the
    :ref:`existing pandas guidelines <pandas:extending>`.  The resulting arrays
    are essentially identical to ``dtype: object`` arrays, but are explicitly
    labeled and have better integration with base pandas.  They may also be
    slightly more performant in some cases.
    """

    __array_priority__: int = 1000  # this is used in pandas test code
    data: np.ndarray[Any, np.dtype[Any]]
    _data: np.ndarray[Any, np.dtype[Any]]
    _items: np.ndarray[Any, np.dtype[Any]]

    def __init__(
        self,
        values: Iterable[Any],
        dtype: dtype_like | None = None,
        copy: bool = False,
    ):
        # NOTE: this does NOT coerce inputs; that's handled by cast() itself

        # aliases for common attributes to ensure pandas support
        self.data = self._data = self._items = np.asarray(values, dtype=object)

        # require use of construct_array_type() factory
        if not hasattr(self, "_pdcast_type"):
            raise NotImplementedError(
                "ObjectArray must have an associated ScalarType"
            )

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    @classmethod
    def _from_sequence(
        cls,
        scalars: Iterable[Any],
        dtype: dtype_like | None = None,
        copy: bool = False
    ) -> ObjectArray:
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
    def _from_factorized(
        cls, values: np.ndarray[np.int64, np.dtype[np.int64]], original: ExtensionArray
    ) -> ObjectArray:
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

    @classmethod
    def _concat_same_type(cls, to_concat: Iterable[ExtensionArray]) -> ObjectArray:
        """Concatenate multiple array of this dtype.

        Parameters
        ----------
        to_concat : Iterable[ExtensionArray]
            sequence of this type

        Returns
        -------
        ExtensionArray
        """
        return cls(np.concatenate([x.data for x in to_concat]))

    ##########################
    ####    ATTRIBUTES    ####
    ##########################

    @property
    def dtype(self):
        """An instance of 'ExtensionDtype'."""
        return self._pdcast_type.dtype

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

    ########################
    ####    INTERNAL    ####
    ########################

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
        return self.data

    def _values_for_factorize(self) -> tuple[np.ndarray[Any, np.dtype[Any]], Any]:
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
        return self.data, self.dtype.na_value

    def _reduce(self, name: str, skipna: bool = True, **kwargs: Any) -> Any:
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

    def __array_ufunc__(
        self, ufunc: np.ufunc, method: str, *inputs: Any, **kwargs: Any
    ) -> ObjectArray:
        """Allows ObjectArrays to utilize numpy ufuncs natively, without
        being coerced to ``dtype: object``.

        This implementation is adapted from the
        :class:`example <numpy.lib.mixins.NDArrayOperatorsMixin>` given by
        numpy.  It might require further modifications.
        """
        # pandas unboxes these so we don't need to implement them ourselves
        if any(
            isinstance(other, (ABCSeries, ABCIndex, ABCDataFrame))  # type: ignore
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

        inputs = tuple(x.data if isinstance(x, ExtensionArray) else x for x in inputs)
        result = getattr(ufunc, method)(*inputs, **kwargs)

        def reconstruct(x):
            if isinstance(x, scalar_like):
                return x
            return type(self)._from_sequence(x)

        if isinstance(result, tuple):
            return tuple(reconstruct(x) for x in result)
        return reconstruct(result)

    def __arrow_array__(self, type: dtype_like | None = None) -> Any:
        """Convert the underlying array values into a pyarrow Array.

        This method ensures that pyarrow knowns how to convert the specific
        extension array into a pyarrow.Array (also when included as a column in
        a pandas DataFrame).
        """
        import pyarrow

        return pyarrow.array(self.data, type=type)

    def __from_arrow__(self, array: Any) -> ObjectArray:
        """Convert a pyarrow array into a pandas ExtensionArray.

        This method controls the conversion back from pyarrow to a pandas
        ExtensionArray.  It receives a pyarrow Array or ChunkedArray as its
        only argument and is expected to return the appropriate pandas
        ExtensionArray for this dtype and the passed values
        """
        return self._from_sequence(array)

    ######################
    ####    PUBLIC    ####
    ######################

    def astype(self, dtype: dtype_like, copy: bool = True) -> array_like:
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
            return cls._from_sequence(self.data, dtype=dtype, copy=copy)

        return self.data.astype(dtype=dtype, copy=copy)

    def isna(self) -> boolean_array:
        """Detect missing values from the array."""
        return pd.isna(self.data)

    def take(
        self,
        indices: Iterable[int],
        allow_fill: bool = False,
        fill_value: Any | None = None
    ) -> ObjectArray:
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

        data = self.data
        if allow_fill and fill_value is None:
            fill_value = self.dtype.na_value

        result = take(
            data,
            indices,
            fill_value=fill_value,
            allow_fill=allow_fill
        )
        return self._from_sequence(result, dtype=self.dtype)

    def copy(self) -> ObjectArray:
        """Return a copy of the array.

        Returns
        -------
        ExtensionArray
        """
        return type(self)(self.data.copy())

    ###########################
    ####    NEW METHODS    ####
    ###########################

    # TODO: implement unique()?  Is this necessary?

    # NOTE: these are not defined in the base ExtensionArray class, but are
    # called in several pandas operations.

    def round(self, *args: Any, **kwargs: Any) -> ObjectArray:
        """Default round implementation.  This simply passes through to
        np.round().
        """
        return self._from_sequence(self.data.round(*args, **kwargs))

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
        counts = Counter(self.data[~mask])
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

    def __abs__(self) -> ObjectArray:
        """Implement the :func:`abs() <python:abs>` function for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        return rectify(abs(self.data), self._pdcast_type)

    def __neg__(self) -> ObjectArray:
        """Implement the unary negative operator ``-`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        if self.dtype._is_boolean:
            return self.__invert__()

        return rectify(-self.data, self._pdcast_type)

    def __pos__(self) -> ObjectArray:
        """Implement the unary positive operator ``+`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        return rectify(+self.data, self._pdcast_type)

    def __invert__(self) -> ObjectArray:
        """Implement the unary inversion operator ``~`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        if self.dtype._is_boolean:
            return self.__xor__(True)

        return rectify(~self.data, self._pdcast_type)

    ################################
    ####    BINARY OPERATORS    ####
    ################################

    def __and__(self, other: Any) -> ExtensionArray:
        """Implement the binary AND operator ``&`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data & np.asarray(other), self._pdcast_type)

    def __or__(self, other: Any) -> ExtensionArray:
        """Implement the binary OR operator ``|`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data & np.asarray(other), self._pdcast_type)

    def __xor__(self, other: Any) -> ExtensionArray:
        """Implement the binary XOR operator ``^`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data & np.asarray(other), self._pdcast_type)

    def __rshift__(self, other: Any) -> ExtensionArray:
        """Implement the binary right-shift operator ``>>`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data & np.asarray(other), self._pdcast_type)

    def __lshift__(self, other: Any) -> ExtensionArray:
        """Implement the binary left-shift operator ``<<`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data & np.asarray(other), self._pdcast_type)

    ########################################
    ####    REVERSE BINARY OPERATORS    ####
    ########################################

    def __rand__(self, other: Any) -> ExtensionArray:
        """Implement the reverse binary AND operator ``&`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) & self.data, self._pdcast_type)

    def __ror__(self, other: Any) -> ExtensionArray:
        """Implement the reverse binary OR operator ``|`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) | self.data, self._pdcast_type)

    def __rxor__(self, other: Any) -> ExtensionArray:
        """Implement the reverse binary XOR operator ``^`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) ^ self.data, self._pdcast_type)

    def __rrshift__(self, other: Any) -> ExtensionArray:
        """Implement the reverse binary right-shift operator ``>>`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) >> self.data, self._pdcast_type)

    def __rlshift__(self, other: Any) -> ExtensionArray:
        """Implement the reverse binary left-shift operator ``<<`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) << self.data, self._pdcast_type)

    #########################################
    ####    IN-PLACE BINARY OPERATORS    ####
    #########################################

    def __iand__(self, other: Any) -> ExtensionArray:
        """Implement the in-place binary AND operator ``&=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data &= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    def __ior__(self, other: Any) -> ExtensionArray:
        """Implement the in-place binary OR operator ``|=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data |= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    def __ixor__(self, other: Any) -> ExtensionArray:
        """Implement the in-place binary XOR operator ``^=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data ^= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    def __irshift__(self, other: Any) -> ExtensionArray:
        """Implement the in-place binary right-shift operator ``>>=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data >>= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    def __ilshift__(self, other: Any) -> ExtensionArray:
        """Implement the in-place binary left-shift operator ``<<=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data <<= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    ##############################
    ####    MATH OPERATORS    ####
    ##############################

    # NOTE: math operators can change an array's type in unexpected ways, so
    # we always follow up with a detect_type call to rectify the result.

    def __add__(self, other: Any) -> ExtensionArray:
        """Implement the addition operator ``+`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data + np.asarray(other), self._pdcast_type)

    def __sub__(self, other: Any) -> ExtensionArray:
        """Implement the subtraction operator ``-`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data - np.asarray(other), self._pdcast_type)

    def __mul__(self, other: Any) -> ExtensionArray:
        """Implement the multiplication operator ``*`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data * np.asarray(other), self._pdcast_type)

    def __matmul__(self, other: Any) -> ExtensionArray:
        """Implement the matrix multiplication operator ``@`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data @ np.asarray(other), self._pdcast_type)

    def __truediv__(self, other: Any) -> ExtensionArray:
        """Implement the true division operator ``/`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data / np.asarray(other), self._pdcast_type)

    def __floordiv__(self, other: Any) -> ExtensionArray:
        """Implement the floor division operator ``//`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data // np.asarray(other), self._pdcast_type)

    def __mod__(self, other: Any) -> ExtensionArray:
        """Implement the modulo operator ``%`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(self.data % np.asarray(other), self._pdcast_type)

    def __divmod__(self, other: Any) -> ExtensionArray:
        """Implement the :func:`divmod() <python:divmod>` function for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(
            divmod(self.data, np.asarray(other)),
            self._pdcast_type
        )

    def __pow__(self, other: Any, mod: Any | None = None) -> ExtensionArray:
        """Implement the exponentiation operator ``**`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(
            pow(self.data, np.asarray(other), mod),
            self._pdcast_type
        )

    ######################################
    ####    REVERSE MATH OPERATORS    ####
    ######################################

    def __radd__(self, other: Any) -> ExtensionArray:
        """Implement the reverse addition operator ``+`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) + self.data, self._pdcast_type)

    def __rsub__(self, other: Any) -> ExtensionArray:
        """Implement the reverse subtraction operator ``-`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) - self.data, self._pdcast_type)

    def __rmul__(self, other: Any) -> ExtensionArray:
        """Implement the reverse multiplication operator ``*`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) * self.data, self._pdcast_type)

    def __rmatmul__(self, other: Any) -> ExtensionArray:
        """Implement the reverse matrix multiplication operator ``@`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) @ self.data, self._pdcast_type)

    def __rtruediv__(self, other: Any) -> ExtensionArray:
        """Implement the reverse true division operator ``/`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) / self.data, self._pdcast_type)

    def __rfloordiv__(self, other: Any) -> ExtensionArray:
        """Implement the reverse floor division operator ``//`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) // self.data, self._pdcast_type)

    def __rmod__(self, other: Any) -> ExtensionArray:
        """Implement the reverse modulo operator ``%`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(np.asarray(other) % self.data, self._pdcast_type)

    def __rdivmod__(self, other: Any) -> ExtensionArray:
        """Implement the reverse :func:`divmod() <python:divmod>` function for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(
            divmod(np.asarray(other), self.data),
            self._pdcast_type
        )

    def __rpow__(self, other: Any, mod: Any | None = None) -> ExtensionArray:
        """Implement the reverse exponentiation operator ``**`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return rectify(
            pow(np.asarray(other), self.data, mod),
            self._pdcast_type
        )

    #######################################
    ####    IN-PLACE MATH OPERATORS    ####
    #######################################

    def __iadd__(self, other: Any) -> ExtensionArray:
        """Implement the in-place addition operator ``+=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data += np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    def __isub__(self, other: Any) -> ExtensionArray:
        """Implement the in-place subtraction operator ``-=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data -= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    def __imul__(self, other: Any) -> ExtensionArray:
        """Implement the in-place multiplication operator ``*=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data *= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    def __imatmul__(self, other: Any) -> ExtensionArray:
        """Implement the in-place matrix multiplication operator ``@=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data @= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    def __itruediv__(self, other: Any) -> ExtensionArray:
        """Implement the in-place true division operator ``/=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data /= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    def __ifloordiv__(self, other: Any) -> ExtensionArray:
        """Implement the in-place floor division operator ``//=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data //= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    def __imod__(self, other: Any) -> ExtensionArray:
        """Implement the in-place modulo operator ``%=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data %= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    def __ipow__(self, other: Any, mod: Any | None = None) -> ExtensionArray:
        """Implement the in-place exponentiation operator ``**=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        self.data **= np.asarray(other)
        return rectify(self.data, self._pdcast_type)

    ####################################
    ####    COMPARISON OPERATORS    ####
    ####################################

    def __lt__(self, other: Any) -> boolean_array:
        """Implement the less-than operator ``<`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return self.data < other

    def __le__(self, other: Any) -> boolean_array:
        """Implement the less-than-or-equal-to operator ``<=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return self.data <= other

    def __eq__(self, other: Any) -> boolean_array:  # type: ignore
        """Implement the equality operator ``==`` (elementwise) for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return self.data == other

    def __ne__(self, other: Any) -> boolean_array:  # type: ignore
        """Implement the inequality operator ``!=`` (elementwise) for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return self.data != other

    def __gt__(self, other: Any) -> boolean_array:
        """Implement the greater-than operator ``>`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return self.data > other

    def __ge__(self, other: Any) -> boolean_array:
        """Implement the greater-than-or-equal-to operator ``>=`` for
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        # NOTE: pandas recommends that we not handle these cases ourselves
        if isinstance(other, (ABCSeries, ABCIndex)):  # type: ignore
            return NotImplemented

        return self.data >= other

    #################################
    ####    CONTAINER METHODS    ####
    #################################

    def __getitem__(self, key: Any) -> Any | ObjectArray:
        """Select a subset of self.

        Parameters
        ----------
        key : int, slice, or ndarray
            * int: The position in 'self' to get.
            * slice: A slice object, where 'start', 'stop', and 'step' are
              integers or None
            * ndarray: A 1-d boolean NumPy ndarray the same length as 'self'
            * list[int]:  A list of int

        Returns
        -------
        scalar or ExtensionArray

        Notes
        -----
        For scalar ``key``, return a scalar value suitable for the array's
        type. This should be an instance of ``self.dtype.type``.

        For slice ``key``, return an instance of ``ExtensionArray``, even
        if the slice is length 0 or 1.

        For a boolean mask, return an instance of ``ExtensionArray``, filtered
        to the values where ``key`` is True.
        """
        if isinstance(key, numbers.Integral):
            return self.data[key]
        return self._from_sequence(self.data[key])

    def __setitem__(self, key: Any, value: Any) -> None:
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
        """
        from pdcast import convert

        if pd.api.types.is_list_like(value):
            if pd.api.types.is_scalar(key):
                raise ValueError(
                    "setting an array element with a sequence."
                )
            value = np.asarray(convert.cast(value, self._pdcast_type))
        else:
            value = convert.cast(value, self._pdcast_type)[0]

        self.data[key] = value

    def __delitem__(self, key: Any) -> None:
        """Delete one or more values inplace.

        This always raises an error since numpy does not support deleting
        elements from an array.
        """
        del self.data[key]

    def __iter__(self) -> Iterator[Any]:
        """Iterate over elements of the array."""
        return iter(self.data)

    def __len__(self) -> int:
        """Length of this array

        Returns
        -------
        length : int
        """
        return len(self.data)

    def __contains__(self, other: Any) -> bool:
        """Implement the ``in`` keyword for membership tests on
        :class:`ObjectArrays <pdcast.ObjectArray>`.
        """
        return other in self.data


def rectify(arr: np.ndarray[Any, np.dtype[Any]], orig_dtype) -> ExtensionArray:
    """Rectify the result of a math operation to the detected type of the
    first element.

    Parameters
    ----------
    arr : np.ndarray
        The array to rectify.  This is typically the result of a math
        operation, which may or may not coerce the result to a different type.

    Returns
    -------
    ExtensionArray
        The rectified array.  This is always returned as a pandas-compatible
        `ExtensionArray` object.

    Notes
    -----
    The output type is determined by the first non-missing element of the
    input array.
    """
    from pdcast.detect import detect_type

    # get first non-missing element and detect its type
    is_na = pd.isna(arr)
    first = next(iter(arr[~is_na]), None)
    detected = detect_type(first)

    # check for empty array
    if detected is None:  # TODO: replace with NullType
        return arr

    # fill missing values with new type's na_value
    if detected != orig_dtype:
        arr[is_na] = detected.na_value

    # get pandas array type
    dtype = detected.dtype
    if isinstance(dtype, ExtensionDtype):
        array_type = dtype.construct_array_type()
    else:
        array_type = PandasArray

    # construct output array
    return array_type._from_sequence(arr, dtype=dtype, copy=False)
