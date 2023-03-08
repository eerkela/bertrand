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


######################
####    PUBLIC    ####
######################


def detect_type(example: Any, skip_na: bool = True) -> types.BaseType:
    """Infer types from example data.

    If the example data has an appropriate ``.dtype`` field, and that dtype is
    *not* an ``object`` type, then it will be parsed directly, which is an O(1)
    operation.  Otherwise, this function essentially vectorizes the built-in
    ``type()`` function and applies it elementwise over the example data.

    Arguments
    ---------
    example : Any
        The example data whose type will be inferred.  This can be a scalar,
        array-like structure, or iterable of any kind.
    skip_na : bool, default True
        If True, drop missing values from the example data before inferring.

    Returns
    -------
    types.BaseType
        A type object representing the elements of the example data.  If the
        example is homogenous, this will be an :py:class:`AtomicType` or
        :py:class:`AdapterType` instance.  If the example contains elements of
        mixed type, it will be a :py:class:`CompositeType` object.

    See Also
    --------
    typecheck : vectorized type checking.
    resolve_type : Generalized type resolution.
    AtomicType.detect : Delegated method for inference resolutions.
    CompositeType.index : index of types for non-homogenous input data.

    Notes
    -----
    This function is one of the preferred constructors for type objects, along
    with :func:`resolve_type`.  Its behavior can be customized by overriding
    :func:`AtomicType.detect` in individual type definitions.  See that
    function for a guide on how to do this.

    If the example data is of mixed type, the returned
    :class:`CompositeType`\'s ``.index`` field will contain an array indicating
    the location of each type in the example data.  This can be used for
    ``pd.Series.groupby()`` operations and is highly memory-efficient thanks to
    :class:`AtomicType`\'s :ref:`flyweight construction <flyweight>` pattern.

    If ``pdcast.attach`` is imported, this function is directly attached to
    ``pd.Series`` objects under ``pd.Series.element_type``, allowing users to
    omit the ``data`` argument.  A similar attribute is attached to
    ``pd.DataFrame`` objects under the same name, except it returns a
    dictionary mapping column names to their inferred type(s).

    >>> import pandas as pd
    >>> import pdcast.attach

    >>> pd.Series([1, 2, 3]).element_type
    NumpyInt64Type()
    >>> pd.DataFrame({"a": [1, 2], "b": [1., 2.], "c": ["1", "2"]}).element_type
    {'a': NumpyInt64Type(), 'b': NumpyFloat64Type(), 'c': StringType()}

    Examples
    --------
    >>> import numpy as np
    >>> import pandas as pd
    >>> import pdcast

    >>> class CustomObj:
    ...     def __init__(self, x): self.x = x

    >>> pdcast.detect_type(True)
    BooleanType()
    >>> pdcast.detect_type([CustomObj(1), CustomObj(2), CustomObj(3)])
    ObjectType(type_def=<class 'CustomObj'>)
    >>> pdcast.detect_type(np.array([1, 2, 3]))
    NumpyInt64Type()
    >>> pdcast.detect_type(pd.Series([1., 2., 3.]))
    NumpyFloat64Type()
    >>> pdcast.detect_type(pd.Series([1., 2., None], dtype="Sparse"))
    SparseType(wrapped=NumpyFloat64Type(), fill_value=nan)
    >>> pdcast.detect_type([True, 2, 3., 4+0j])   # doctest: +SKIP
    CompositeType({bool, int, float, complex})
    """
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
