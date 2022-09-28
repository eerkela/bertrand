cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import resolve_dtype

from .base cimport ElementType
from .integer cimport *


# TODO: these would all be offloaded into check subpackage


cdef dict type_map = {
    # integers
    int: IntegerType,
    np.int8: Int8Type,
    np.int16: Int16Type,
    np.int32: Int32Type,
    np.int64: Int64Type,
    np.uint8: UInt8Type,
    np.uint16: UInt16Type,
    np.uint32: UInt32Type,
    np.uint64: UInt64Type
}


def get_type(obj, **kwargs) -> ElementType:
    """resolve an ElementType from an input scalar."""
    return ElementType.instance(type_map[type(obj)], **kwargs)


def resolve_type(dtype, **kwargs) -> ElementType:
    """resolve an ElementType from a dtype specifier."""
    dtype = resolve_dtype(dtype)
    return ElementType.instance(type_map[dtype], **kwargs)
