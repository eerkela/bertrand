from typing import Any

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd

cimport pdcast.resolve as resolve
import pdcast.resolve as resolve
cimport pdcast.types as types
import pdcast.types as types

from pdcast.util.structs import as_series


# TODO: detect .dtype interaction:
# -> 


######################
####    PUBLIC    ####
######################


def detect_type(example: Any, skip_na: bool = True) -> types.BaseType:
    """Detect types from example data."""
    # trivial case: example is already a type object
    if isinstance(example, types.BaseType):
        return example

    cdef object fill_value = None
    cdef types.BaseType result = None

    # check if example is iterable
    if hasattr(example, "__iter__") and not isinstance(example, type):
        # if example type has been explicitly registered, interpret as scalar
        if type(example) in types.AtomicType.registry.aliases:
            return detect_scalar_type(example)

        # use .dtype field if available
        dtype = getattr(example, "dtype", None)
        if dtype is not None:
            # unwrap sparse types
            if isinstance(dtype, pd.SparseDtype):
                fill_value = dtype.fill_value
                dtype = dtype.subtype
            if dtype != np.dtype("O"):
                result = resolve.resolve_type({dtype})

        # no dtype or dtype=object, loop through and interpret
        if result is None:
            example = as_series(example)
            if skip_na:
                example = example.dropna()
            result = detect_vector_type(example.to_numpy(dtype="O"))

        # parse resulting CompositeType
        if not result:  # empty set
            return None
        if len(result) == 1:  # homogenous
            if fill_value is not None:  # reapply sparse wrapper
                return types.SparseType(result.pop(), fill_value)
            return result.pop()
        return result  # non-homogenous

    # example is not iterable
    return detect_scalar_type(example)


#######################
####    PRIVATE    ####
#######################  


cdef types.AtomicType detect_scalar_type(object example):
    """Given a scalar example of a particular data type, return a corresponding
    AtomicType object.
    """
    # look up example type
    cdef type example_type = type(example)
    cdef dict lookup = types.AtomicType.registry.aliases
    cdef type class_def = lookup.get(example_type, None)

    # delegate to class_def.detect(), defaulting to ObjectType
    if class_def is None:
        return types.ObjectType.instance(example_type)
    return class_def.detect(example)


@cython.boundscheck(False)
@cython.wraparound(False)
cdef types.CompositeType detect_vector_type(np.ndarray[object] arr):
    """Loop through an object array and return a CompositeType that corresponds
    to its elements.
    """
    cdef set atomic_types = set()
    cdef dict lookup = types.AtomicType.registry.aliases
    cdef unsigned int arr_length = arr.shape[0]
    cdef unsigned int i
    cdef object element
    cdef type element_type
    cdef type class_def
    cdef types.AtomicType result
    cdef np.ndarray[object] index = np.empty(arr_length, dtype="O")

    # loop through input array (fast)
    for i in range(arr_length):
        # call type() on each element
        element = arr[i]
        element_type = type(element)

        # look up element_type to get AtomicType definition
        class_def = lookup.get(element_type, None)

        # delegate to class_def.detect(), defaulting to ObjectType
        if class_def is None:
            result = types.ObjectType.instance(element_type)
        else:
            result = class_def.detect(element)

        # add result to both atomic_types set and index buffer
        atomic_types.add(result)
        index[i] = result

    # create CompositeType from atomic_types + index buffer
    return types.CompositeType(atomic_types, index=index)
