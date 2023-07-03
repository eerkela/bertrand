"""This module describes the ``detect_type()`` function, which can infer types
from vectorized example data.

The classes that are recognized by this function can be managed via the
``register_alias()``, ``remove_alias()``, and ``clear_aliases()`` methods that
are attached to every ``ScalarType`` and ``DecoratorType`` definition.
"""
from typing import Any, Iterable

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd

from pdcast.decorators.attachable import attachable
from pdcast.resolve import resolve_type
from pdcast import types
from pdcast cimport types
from pdcast.util.vector cimport as_array


# TODO: if a composite categorical is given, split the data up into homogenous
# chunks and resolve each chunk separately.  We can then avoid the need to
# encapsulate composite data in a DecoratorType.


######################
####    PUBLIC    ####
######################


@attachable
def detect_type(data: Any, skip_na: bool = True) -> types.Type | dict:
    """Infer types from example data.

    Arguments
    ---------
    data : Any
        The example data whose type will be inferred.  This can be a scalar
        or list-like iterable of any kind.
    skip_na : bool, default True
        If ``True``, drop missing values from the example data before
        inferring.

    Returns
    -------
    ScalarType | DecoratorType | CompositeType
        The observed type of the example data.  If the example is homogenous,
        this will be an :class:`ScalarType` or :class:`DecoratorType` instance.
        If the example contains elements of mixed type, it will be a
        :class:`CompositeType` object.

    See Also
    --------
    ScalarType.detect : customizable detection of scalar objects.
    ScalarType.from_dtype : customizable resolution of
        :ref:`numpy <resolve_type.type_specifiers.numpy>`\ /\ 
        :ref:`pandas <resolve_type.type_specifiers.pandas>` data types.
    DecoratorType.from_dtype : customizable resolution of
        :ref:`numpy <resolve_type.type_specifiers.numpy>`\ /\ 
        :ref:`pandas <resolve_type.type_specifiers.pandas>` data types.
    """
    cdef object fill_value = None
    cdef types.Type result = None
    cdef type data_type = type(data)

    # trivial case: example is already a type object
    if issubclass(data_type, types.Type):
        return data

    # DataFrame (columnwise) case
    if issubclass(data_type, pd.DataFrame):
        columnwise = {}
        for col in data.columns:
            columnwise[col] = detect_type(data[col], skip_na=skip_na)
        return columnwise

    # build factory
    if hasattr(data, "__iter__") and not isinstance(data, type):
        if data_type in types.ScalarType.registry.aliases:
            factory = ScalarDetector(data, data_type)
        elif hasattr(data, "dtype"):
            factory = ArrayDetector(data, skip_na=skip_na)
        else:
            factory = ElementWiseDetector(data, skip_na=skip_na)
    else:
        factory = ScalarDetector(data, data_type)

    return factory()


#######################
####    PRIVATE    ####
#######################  


cdef tuple PANDAS_ARRAYS = (
    pd.Series, pd.Index, pd.api.extensions.ExtensionArray
)


cdef class Detector:
    """A factory that returns type objects from example data."""

    def __init__(self):
        self.aliases = dict(types.registry.aliases)

    def __call__(self) -> types.Type:
        raise NotImplementedError(f"{type(self)} does not implement __call__")


cdef class ScalarDetector(Detector):
    """A factory that constructs types from scalar examples"""

    def __init__(self, object example, type example_type):
        super().__init__()
        self.example = example
        self.example_type = example_type

    def __call__(self) -> types.ScalarType:
        if pd.isna(self.example):
            return types.NullType

        cdef types.ScalarType result

        result = self.aliases.get(self.example_type, None)
        if result is None:
            return types.ObjectType(self.example_type)
        return result.from_scalar(self.example)


cdef class ArrayDetector(Detector):
    """A factory that constructs types using an array's .dtype protocol.
    """

    def __init__(self, data: Iterable, skip_na: bool):
        super().__init__()
        self.data = data
        self.skip_na = skip_na

    def __call__(self) -> types.Type:
        cdef object dtype
        cdef object fill_value
        cdef types.Type result

        # get dtype of original array
        dtype = self.data.dtype

        # strip sparse dtypes
        fill_value = None
        if isinstance(dtype, pd.SparseDtype):
            fill_value = dtype.fill_value
            dtype = dtype.subtype

        # TODO: add a special case here for categorical dtypes.  Maybe these
        # should be special methods of DecoratorType objects, similar to
        # transform() and inverse_transform()?
        # -> maybe from_array()?  This would be called whenever an array is
        # detected with that dtype.

        # case 1: type is ambiguous
        if dtype == np.dtype(object):
            result = ElementWiseDetector(self.data, skip_na=self.skip_na)()

        # case 2: type is deterministic
        else:
            # special cases for pandas data structures
            if isinstance(self.data, PANDAS_ARRAYS):
                # NOTE: pandas uses numpy M8 and m8 dtypes for time data
                if dtype == np.dtype("M8[ns]"):
                    dtype = resolve_type(types.PandasTimestampType)
                elif dtype == np.dtype("m8[ns]"):
                    dtype = resolve_type(types.PandasTimedeltaType)

            # TODO: resolving as composite does not produce an appropriate
            # index.  This only occurs if the dtype is categorical and the data
            # are mixed.
            # -> find a way to break up the data into homogenous chunks

            # resolve dtype directly
            result = resolve_type([dtype])
            if len(result) == 1:
                result = result.pop()

            # TODO: this gets complicated if the result of the resolve_type()
            # is composite.  However, this only happens if the dtype is
            # categorical and its categories are mixed, so if we solve that
            # problem, then this one will be solved as well.

            # consider nans if skip_na=False
            if not self.skip_na:
                is_na = pd.isna(self.data)
                if is_na.any():
                    index = np.where(is_na, types.NullType, result)
                    result = types.CompositeType(
                        [result, types.NullType],
                        run_length_encode(index)
                    )

        # if data is empty or skip_na=True and all values are NA, return None
        if not result:
            return None

        # replace sparse type
        if fill_value is not None:
            if isinstance(result, types.CompositeType):
                return types.CompositeType(
                    types.SparseType(typ, fill_value) for typ in result
                )
            return types.SparseType(result, fill_value)

        return result


cdef class ElementWiseDetector(Detector):
    """A factory that constructs types elementwise, by looping through the
    vector.
    """

    def __init__(self, data: Iterable, skip_na: bool):
        super().__init__()

        data = as_array(data)
        self.skip_na = skip_na
        self.missing = pd.isna(data)
        self.hasnans = self.missing.any()
        if self.hasnans:
            data = data[~self.missing]

        self.data = data.astype(object, copy=False)

    def __call__(self) -> types.Type:
        cdef types.CompositeType result
        cdef np.ndarray index

        # detect type at each element
        result = detect_vector_type(self.data, self.aliases)

        # insert missing values
        if not self.skip_na and self.hasnans:
            index = np.full(self.missing.shape[0], types.NullType, dtype=object)
            index[~self.missing] = result.index
            result = types.CompositeType(
                result | {types.NullType},
                run_length_encode(index)
            )

        # pop singletons and return
        if not result:
            return None
        if len(result) == 1:
            return result.pop()
        return result


@cython.boundscheck(False)
@cython.wraparound(False)
cdef types.CompositeType detect_vector_type(object[:] arr, dict lookup):
    """Loop through an object array and return a CompositeType that corresponds
    to the type of each element.

    This builds up the result in a run-length encoded fashion to reduce
    memory consumption.  The result is returned as a structured array with two
    fields:

        * value: the type of the element.
        * count: the number of consecutive elements of that type.
    """
    cdef unsigned int arr_length = arr.shape[0]
    cdef unsigned int i
    cdef object element
    cdef type element_type
    cdef types.ScalarType resolved
    cdef types.ScalarType last = None
    cdef unsigned int count = 0
    cdef list counts = []
    cdef np.ndarray index
    cdef set observed = set()

    # iterate through array
    for i in range(arr_length):

        # extract element and get its type
        element = arr[i]
        element_type = type(element)

        # look up type in registry.aliases and call appropriate constructor
        resolved = lookup.get(element_type, None)
        if resolved is None:
            resolved = types.ObjectType(element_type)
        else:
            resolved = resolved.from_scalar(element)

        # increment count if result is same as the previous index
        if resolved == last:
            count += 1
        else:
            # otherwise, append the last type and count to list
            observed.add(resolved)
            if last is not None:
                counts.append((last, count))

            # reset count
            last = resolved
            count = 1

    # append the last type and count to list
    if last is not None:
        observed.add(last)
        counts.append((last, count))

    # create structured index
    index = np.array(counts, dtype=[("value", object), ("count", np.int64)])

    # package result
    return types.CompositeType(observed, index=index)


cdef np.ndarray run_length_encode(np.ndarray arr):
    """Compress an array by recording the number of consecutive elements of
    each type.

    The result is returned as a structured array with two fields:

        * value: the value of the element.
        * count: the number of consecutive elements of that value.
    """
    cdef np.ndarray[np.int64_t] idx
    cdef np.ndarray values
    cdef np.ndarray[np.int64_t] counts

    # get indices where transitions occur
    idx = np.flatnonzero(arr[:-1] != arr[1:])
    idx = np.concatenate([[0], idx + 1, [arr.shape[0]]])

    # get values at each transition
    values = arr[idx[:-1]]

    # compute lengths of runs as difference between indices
    counts = np.diff(idx)

    # return as structured array
    return np.array(
        list(zip(values, counts)),
        dtype=[("value", arr.dtype), ("count", np.int64)],
    )
