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

from pdcast.decorators import attachable
from pdcast.resolve import resolve_type
from pdcast import types
from pdcast cimport types
from pdcast.util.vector cimport as_array


######################
####    PUBLIC    ####
######################


@attachable.attachable
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


cdef tuple pandas_arrays = (
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
            return None

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
        self.dtype = data.dtype
        self.skip_na = skip_na

    def __call__(self) -> types.VectorType:
        dtype = self.dtype

        # strip sparse types
        fill_value = None
        if isinstance(dtype, pd.SparseDtype):
            fill_value = dtype.fill_value
            dtype = dtype.subtype

        # no type information
        if dtype == np.dtype(object):
            result = ElementWiseDetector(self.data, skip_na=self.skip_na)()
        else:
            # special cases for pd.Timestamp/pd.Timedelta series
            if isinstance(self.data, pandas_arrays):
                if dtype == np.dtype("M8[ns]"):
                    dtype = resolve_type(types.PandasTimestampType)
                elif dtype == np.dtype("m8[ns]"):
                    dtype = resolve_type(types.PandasTimedeltaType)

            result = resolve_type([dtype])
            if len(result) == 1:
                result = result.pop()

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
        if skip_na:
            data = data[~pd.isna(data)]

        self.data = data.astype(object, copy=False)

    def __call__(self) -> types.Type:
        result = detect_vector_type(self.data, self.aliases)
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

    This uses run-length encoding to reduce memory consumption.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object element
    cdef type element_type
    cdef types.ScalarType resolved
    cdef set observed = set()

    # for run-length encoding
    cdef list counts = []
    cdef types.ScalarType last = None
    cdef int count = 0
    cdef np.ndarray index

    # iterate through array
    for i in range(arr_length):

        # call type() on each element
        element = arr[i]
        element_type = type(element)

        # look up type in registry.aliases and call appropriate constructor
        resolved = lookup.get(element_type, None)
        if resolved is None:
            resolved = types.ObjectType(element_type)
        else:
            resolved = resolved.from_scalar(element)

        # add result to observed
        observed.add(resolved)

        # increment count if result is same as the previous index
        if resolved == last:
            count += 1
        else:
            # otherwise, append the last type and count to list
            if last is not None:
                counts.append((last, count))

            # reset count
            last = resolved
            count = 1

    # append the last type and count to list
    if last is not None:
        counts.append((last, count))

    # create structured index
    index = np.array(counts, dtype=[("type", object), ("count", np.int64)])

    # package result
    return types.CompositeType(observed, index=index)
