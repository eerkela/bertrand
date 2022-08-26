cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from .supertypes cimport atomic_to_supertype


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] _object_types(
    np.ndarray[object] arr,
    bint exact = True
):
    """Internal C interface for public-facing object_types() function."""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    if exact:  # conditional hoisted out of loop
        for i in range(arr_length):
            result[i] = type(arr[i])
    else:
        for i in range(arr_length):
            result[i] = atomic_to_supertype.get(type(arr[i]), object)

    return result


@cython.boundscheck(False)
@cython.wraparound(False)
def object_types(
    np.ndarray[object] arr,
    bint exact = True
) -> np.ndarray[object]:
    """Return the type/supertype of each element in a numpy object array.

    Parameters
    ----------
    arr (np.ndarray[object]):
        The array to analyze.  Must be a pyobject array with `dtype='O'`.
    exact (bint):
        If `False`, return the supertype associated with each element, rather
        than the element type directly.  Defaults to `True`.

    Returns
    -------
    np.ndarray[object]:
        The result of doing elementwise type introspection on the object array
        `arr`.
    """
    return _object_types(arr, exact)
