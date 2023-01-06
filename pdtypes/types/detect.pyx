from typing import Any

cimport numpy as np
import numpy as np
import pandas as pd

cimport pdtypes.types.atomic as atomic
import pdtypes.types.atomic as atomic
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


######################
####    PUBLIC    ####
######################


def detect_type(
    example: Any,
    root: bool = False
) -> atomic.AtomicType | atomic.CompositeType:
    """Detect AtomicTypes from live data."""
    # ignore AtomicType/CompositeType objects
    if isinstance(example, (atomic.AtomicType, atomic.CompositeType)):
        return example

    # check if example is iterable
    if hasattr(example, "__iter__") and not isinstance(example, type):
        # if example type has been registered, interpret as scalar
        if type(example) in atomic.AtomicType.registry.aliases:
            return detect_scalar_type(example, root=root)

        # fastpath: use .dtype field if it is present
        dtype = getattr(example, "dtype", None)
        if dtype is not None and dtype != np.dtype("O"):
            result = resolve.resolve_typespec_dtype(dtype)
            return result.root if root else result

        # no dtype or dtype=object, loop through and interpret
        if isinstance(example, (set, frozenset)):
            example = [x for x in example]
        result = detect_vector_type(np.array(example, dtype="O"), root=root)
        if not result:  # empty set
            return None
        if len(result) == 1:  # homogenous
            return result.pop()
        return result  # non-homogenous

    # example is not iterable
    return detect_scalar_type(example, root=root)


#######################
####    PRIVATE    ####
#######################


cdef atomic.AtomicType detect_scalar_type(
    object example,
    bint root = False
):
    """Given a scalar example of a particular data type, return a corresponding
    AtomicType object.
    """
    # look up example type
    cdef type example_type = type(example)
    cdef dict lookup = atomic.AtomicType.registry.aliases
    cdef atomic.AliasInfo info = lookup.get(example_type, None)

    # call AtomicType.detect() to parse result, defaulting to ObjectType
    cdef atomic.AtomicType result
    if info is None:
        result = atomic.ObjectType.instance(example_type)
    else:
        result = info.base.detect(example, **info.defaults)

    # return root type if directed
    if root:
        return result.root
    return result


cdef atomic.CompositeType detect_vector_type(
    np.ndarray[object] arr,
    bint root = False
):
    """Loop through an object array and return a CompositeType that corresponds
    to its elements.
    """
    cdef set atomic_types = set()
    cdef dict lookup = atomic.AtomicType.registry.aliases
    cdef unsigned int arr_length = arr.shape[0]
    cdef unsigned int i
    cdef object element
    cdef type element_type
    cdef atomic.AliasInfo info
    cdef atomic.AtomicType result
    cdef atomic.AtomicType[:] index = np.empty(arr_length, dtype="O")

    # loop through input array (fast)
    for i in range(arr_length):
        # call type() on each element
        element = arr[i]
        element_type = type(element)

        # look up element_type to get AliasInfo
        info = lookup.get(element_type, None)

        # call AtomicType.detect() to parse result, defaulting to ObjectType
        if info is None:
            print(element_type)
            result = atomic.ObjectType.instance(element_type)
        else:
            result = info.base.detect(element, **info.defaults)

        # get root type if directed
        if root:
            result = result.root

        # add result to both atomic_types set and index buffer
        atomic_types.add(result)
        index[i] = result

    # create CompositeType from atomic_types + index buffer
    return atomic.CompositeType(atomic_types, index=index)
