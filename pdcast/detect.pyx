"""This module describes the ``detect_type()`` function, which can infer types
from vectorized example data.

The classes that are recognized by this function can be managed via the
``register_alias()``, ``remove_alias()``, and ``clear_aliases()`` methods that
are attached to every ``AtomicType`` and ``AdapterType`` definition.
"""
from typing import Any, Iterable

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd

from pdcast.decorators import attachable
import pdcast.resolve as resolve
cimport pdcast.types as types
import pdcast.types as types
from pdcast.util.vector cimport as_array


######################
####    PUBLIC    ####
######################


@attachable.attachable
def detect_type(data: Any, skip_na: bool = True) -> types.BaseType | dict:
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
    AtomicType | AdapterType | CompositeType
        The observed type of the example data.  If the example is homogenous,
        this will be an :class:`AtomicType` or :class:`AdapterType` instance.
        If the example contains elements of mixed type, it will be a
        :class:`CompositeType` object.

    See Also
    --------
    AtomicType.detect : customizable detection of scalar objects.
    AtomicType.from_dtype : customizable resolution of
        :ref:`numpy <resolve_type.type_specifiers.numpy>`\ /\ 
        :ref:`pandas <resolve_type.type_specifiers.pandas>` data types.
    AdapterType.from_dtype : customizable resolution of
        :ref:`numpy <resolve_type.type_specifiers.numpy>`\ /\ 
        :ref:`pandas <resolve_type.type_specifiers.pandas>` data types.
    """
    cdef object fill_value = None
    cdef types.BaseType result = None
    cdef type data_type = type(data)

    # trivial case: example is already a type object
    if issubclass(data_type, types.BaseType):
        return data

    # DataFrame (columnwise) case
    if issubclass(data_type, pd.DataFrame):
        columnwise = {}
        for col in data.columns:
            columnwise[col] = detect_type(data[col], skip_na=skip_na)
        return columnwise

    # build factory
    if hasattr(data, "__iter__") and not isinstance(data, type):
        if data_type in types.AtomicType.registry.aliases:
            factory = ScalarFactory(data, data_type)
        elif hasattr(data, "dtype"):
            factory = ArrayFactory(data, skip_na=skip_na)
        else:
            factory = ElementWiseFactory(data, skip_na=skip_na)
    else:
        factory = ScalarFactory(data, data_type)

    return factory()


#######################
####    PRIVATE    ####
#######################  


cdef tuple pandas_arrays = (
    pd.Series, pd.Index, pd.api.extensions.ExtensionArray
)


cdef class TypeFactory:
    """A factory that returns type objects from example data."""

    def __init__(self):
        self.aliases = types.registry.aliases

    def __call__(self) -> types.BaseType:
        raise NotImplementedError(f"{type(self)} does not implement __call__")


cdef class ScalarFactory(TypeFactory):
    """A factory that constructs types from scalar examples"""

    def __init__(self, object example, type example_type):
        super().__init__()
        self.example = example
        self.example_type = example_type

    def __call__(self) -> types.AtomicType:
        if pd.isna(self.example):
            return None

        cdef types.AtomicType result

        result = self.aliases.get(self.example_type, None)
        if result is None:
            return types.ObjectType[self.example_type]
        return result.detect(self.example)


cdef class ArrayFactory(TypeFactory):
    """A factory that constructs types using an array's .dtype protocol.
    """

    def __init__(self, data: Iterable, skip_na: bool):
        super().__init__()
        self.data = data
        self.dtype = data.dtype
        self.skip_na = skip_na

    def __call__(self) -> types.ScalarType:
        dtype = self.dtype

        # strip sparse types
        fill_value = None
        if isinstance(dtype, pd.SparseDtype):
            fill_value = dtype.fill_value
            dtype = dtype.subtype

        # no type information
        if dtype == np.dtype(object):
            result = ElementWiseFactory(self.data, skip_na=self.skip_na)()
        else:
            # special cases for pd.Timestamp/pd.Timedelta series
            if isinstance(self.data, pandas_arrays):
                if dtype == np.dtype("M8[ns]"):
                    dtype = resolve.resolve_type(types.PandasTimestampType)
                elif dtype == np.dtype("m8[ns]"):
                    dtype = resolve.resolve_type(types.PandasTimedeltaType)

            result = resolve.resolve_type([dtype])
            if len(result) == 1:
                result = result.pop()

        if not result:
            return None

        # replace sparse type
        if fill_value is not None:
            if isinstance(result, types.CompositeType):
                return types.CompositeType(
                    types.SparseType[typ, fill_value] for typ in result
                )
            return types.SparseType[result, fill_value]

        return result


cdef class ElementWiseFactory(TypeFactory):
    """A factory that constructs types elementwise, by looping through the
    vector.
    """

    def __init__(self, data: Iterable, skip_na: bool):
        super().__init__()
        data = as_array(data)
        if skip_na:
            data = data[~pd.isna(data)]

        self.data = data.astype(object, copy=False)

    def __call__(self) -> types.BaseType:
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
    """
    cdef unsigned int arr_length = arr.shape[0]
    cdef np.ndarray[object] index = np.empty(arr_length, dtype="O")
    cdef set atomic_types = set()
    cdef unsigned int i
    cdef object element
    cdef type element_type
    cdef types.AtomicType result

    for i in range(arr_length):
        element = arr[i]
        element_type = type(element)

        result = lookup.get(element_type, None)
        if result is None:
            result = types.ObjectType(element_type)
        else:
            result = result.detect(element)

        atomic_types.add(result)
        index[i] = result

    # create CompositeType from atomic_types + index buffer
    return types.CompositeType(atomic_types, index=index)