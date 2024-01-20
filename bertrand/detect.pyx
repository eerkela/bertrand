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

from pdcast cimport types
from pdcast.util.vector cimport as_array

from pdcast import types
from pdcast.decorators.attachable import attachable
from pdcast.resolve import resolve_type
from pdcast.types.array import ObjectDtype
from pdcast.util.type_hints import array_like


# TODO: there's more room for optimization if we drop the detect_type() loop
# further into C.  This way, we can work with raw `PyObject*` and
# `PyTypeObject*` pointers rather than the full, reference counted types, and
# can use C/C++ data structures rather than the Python equivalents.

# -> The types we store in the registry.aliases dictionary could be broken off
# as direct `PyTypeObject*` pointers, which would be faster to look up than
# Python type()s.  This would require a custom dictionary implementation, but
# we already basically have one in the HashedList's ListTable.

# As far as hash functions go, we might even be able to use the direct memory
# address of the `PyTypeObject*` pointer itself.  This would just cast the
# pointer to a `uintptr_t` and proceed as normal.  This would have 0 overhead,
# and would cut out a call to the Python API.  In the event of a hash collision,
# we just compare the full memory addresses and proceed as normal.

# We would have to briefly cast the scalar `PyObject*` back to a Python object
# when we pass to ScalarType.from_scalar(), but this is a small price to pay
# for the speedup.



# TODO: there's also potentially a way to increase the speed of flyweight
# lookups by casting to a C string and using a C string hash function.

    # from libcpp.string cimport string
    # from libcpp.functional cimport hash

    # cdef string key = slug.encode()  # encode Python string to bytes, then to C string
    # cdef hash[string] hash_func
    # cdef unsigned long hash_value = hash_func(key)

# This could potentially be faster than looking up the Python string directly.
# We would still probabluy need to handle hash collisions, but this shouldn't
# be too terrible.





# ME:

# Ok here's what I'm thinking.  I have a system that maps `type` objects to a
# collection of Cython classes, which represent an abstract type system with
# support for runtime type detection from example data.  What actually happens
# is we are supplied a (potentially iterable) example value, and then detect
# its type at the scalar level.  If the example is iterable, we then repeat at
# the next index and so on.

# The operation that is performed at each index goes as follows:

# 1. Call `type()` on the scalar value.
# 2. Look the type up in a dictionary, which maps the type to a corresponding
#    extension class.
# 3. Instantiate the class by passing the example into a special constructor
#    for extra parsing.  Each class can override this method to extract extra
#    data from the object at the scalar level.
# 4. Store the constructed object in a set containing each unique value and
#    append it to a run-length-encoded list.  If the next value maps to an
#    identical object, then we just increment its counter and skip the set
#    operation.

# This process happens at every single index of the input iterable, so I want
# it to be as fast as possible.  Currently, each step is implemented in a tight
# cython loop and allows me to parse a list of 1 million python integers in
# about 0.07 seconds on my machine.  This is a good start, but I'm trying to
# see if I can increase performance even further by offloading more of the core
# loop functions to cythonized C or C++.

# I'm thinking about replacing my current hash table lookup with a
# `std::unordered_map` which holds `PyTypeObject*` references directly.  These
# map to the cython classes just like normal, but now that lookup happens
# entirely at the C level.  I can then just use a borrowed `PyObject*`
# reference to the example scalar, and replace the `type()` call with the
# `Py_Type()` C function.  For point #3, I can pass the object reference back
# to the Python space briefly by casting it to `<object>` so the special
# constructor can manipulate it like normal.  Finally, I can record the results
# in a `std::unordered_set` of the final objects as well as a `std::vector` of
# `Pairs`, which would be structs containing the run-length-encoded objects and
# their counts.  I could then cast the vector directly to a Cython typed
# memoryview of `Pairs`, and then from there to a no-copy numpy array using
# `np.asarray()`.  The final array would have a structured dtype with two
# columns to represent the run-length-encoded results.  I then convert the
# `std::unordered_set` into a normal Python `set` to represent the unique
# results.

# What do you think of this plan?


# GPT4:

# The plan to optimize your processing loop using Cython and C++ functionality
# seems sound and could potentially lead to significant speed improvements. Let
# me break down my thoughts on each of your steps:

# 1. Replacing Python's type() call with the Py_Type() C function: This should
# be faster as you'll be working at the C level. The benefit might not be huge,
# but every little bit helps.
# 2. Using std::unordered_map with PyTypeObject* keys: This should be faster
# than Python's built-in dictionary for the same reasons, as std::unordered_map
# is implemented in efficient, compiled C++ code. One thing to keep in mind is
# how the hash function and comparison function for PyTypeObject* pointers are
# implemented, as this could affect performance.
# 3. Passing object references back to Python space: This shouldn't have much
# impact on performance and seems like a necessary step if you need to do some
# Python-level manipulations.
# 4. Storing results in a std::unordered_set and std::vector: Both of these
# data structures are typically more efficient than their Python counterparts,
# so this should help speed things up. In particular, std::vector is known to
# be very efficient in terms of memory usage and access times, so using it for
# storing your pairs of objects and counts is a good choice.
# 5. Converting std::vector to a Cython typed memoryview and then to a NumPy
# array: This is a great way to create a no-copy array, and the conversion
# should be relatively fast.
# 6. Converting std::unordered_set to a Python set: This might be a bit slow
# depending on the size of the set, but it's likely unavoidable if you need to
# return a Python set.

# Overall, I think your plan to move more of your computation to the C/C++
# level is a good one and could significantly speed up your code. You've
# identified some good opportunities for optimization and chosen appropriate
# data structures and functions. Just keep in mind that you'll need to handle
# reference counting properly when working with PyTypeObject* and PyObject*
# pointers to avoid memory leaks or crashes.




# casting a std::vector directly to a numpy array:

# cdef struct MyStruct:
#    PyObject* value
#    size_t count

# dtype = np.dtype([("value": object), ("count": np.int64)])

# cdef vector[MyStruct] vec = ...
# cdef MyStruct[:, :] memview = <MyStruct[:, :]>vec.data()
# cdef np.ndarray arr = np.asarray(memview, dtype=dtype)


######################
####    PUBLIC    ####
######################


@attachable
def detect_type(data: Any, drop_na: bool = True) -> types.Type | dict:
    """Infer types from example data.

    Arguments
    ---------
    data : Any
        The example data whose type will be inferred.  This can be a scalar
        or list-like iterable of any kind.
    drop_na : bool, default True
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
            columnwise[col] = detect_type(data[col], drop_na=drop_na)
        return columnwise

    # build factory
    if hasattr(data, "__iter__") and not isinstance(data, type):
        if data_type in types.ScalarType.registry.aliases:
            factory = ScalarDetector(data, data_type)
        elif hasattr(data, "dtype"):
            factory = ArrayDetector(data, drop_na=drop_na)
        else:
            factory = ElementWiseDetector(data, drop_na=drop_na)
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

    def __init__(self, data: array_like, drop_na: bool):
        super().__init__()
        self.data = data
        self.drop_na = drop_na

    def __call__(self) -> types.Type:
        cdef object dtype
        cdef types.Type result
        cdef object is_na
        cdef np.ndarray index

        # get dtype of original array
        dtype = self.data.dtype

        # case 1: type is ambiguous - loop through elementwise
        if dtype == np.dtype(object):
            result = ElementWiseDetector(self.data, drop_na=self.drop_na)()

        # case 2: type is deterministic - resolve directly
        else:
            if isinstance(dtype, ObjectDtype):
                instance = dtype._pdcast_type
            else:
                instance = types.registry.aliases[type(dtype)]
            result = instance.from_dtype(dtype, self.data)

            # insert nans if drop_na=False
            if not self.drop_na:
                is_na = pd.isna(self.data)
                if is_na.any():
                    # generate index
                    index = np.full(
                        is_na.shape[0],
                        types.NullType,
                        dtype=object
                    )
                    if isinstance(result, types.CompositeType):
                        index[~is_na] = result.index
                    else:
                        index[~is_na] = result

                    # create composite
                    result = types.CompositeType(
                        [result, types.NullType],
                        run_length_encode(index)
                    )

        return result


cdef class ElementWiseDetector(Detector):
    """A factory that constructs types elementwise, by looping through the
    vector.
    """

    def __init__(self, data: Iterable[Any], drop_na: bool):
        super().__init__()

        data = as_array(data)
        self.drop_na = drop_na
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
        if not self.drop_na and self.hasnans:
            index = np.full(
                self.missing.shape[0],
                types.NullType,
                dtype=object
            )
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
