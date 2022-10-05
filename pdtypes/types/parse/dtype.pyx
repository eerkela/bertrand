cimport numpy as np
import numpy as np
import pandas as pd

from pdtypes import PYARROW_INSTALLED

from pdtypes.types cimport *


# TODO: dtype="O", bytes?


# TODO: categorical types with dtype="O"
# -> call parse_array and return a set if multiple types are found.


######################
####    PUBLIC    ####
######################


cdef dict parse_dtype(object typespec):
    """Parse a numpy/pandas dtype specifier into an equivalent **kwargs dict,
    for use in ElementType construction.
    """
    cdef dict result = {}
    cdef object categories
    cdef object dtype
    cdef str unit
    cdef unsigned long long step_size

    # sparse
    if pd.api.types.is_sparse(typespec):
        result["sparse"] = True
        typespec = typespec.subtype

    # categorical
    if pd.api.types.is_categorical_dtype(typespec):
        result["categorical"] = True
        categories = typespec.categories
        dtype = categories.dtype
        if pd.api.types.is_object_dtype(categories.dtype):
            # typespec = parse_array(categories)
            raise NotImplementedError("NYI: categorical with dtype='O'")
        else:
            typespec = categories.dtype

    # numpy datetime64/timedelta64
    if isinstance(typespec, np.dtype):
        # datetime64
        if np.issubdtype(typespec, "M8"):
            result["base"] = NumpyDatetime64Type
            unit, step_size = np.datetime_data(typespec)
            if unit != "generic":
                result["unit"] = unit
                result["step_size"] = step_size
            return result

        # timedelta64
        if np.issubdtype(typespec, "m8"):
            result["base"] = NumpyTimedelta64Type
            unit, step_size = np.datetime_data(typespec)
            if unit != "generic":
                result["unit"] = unit
                result["step_size"] = step_size
            return result

    # consult lookup table
    result.update(lookup[typespec])
    return result


#########################
####    CONSTANTS    ####
#########################


cdef dict lookup = {
    # boolean
    np.dtype(bool): {"base": BooleanType, "nullable": False},
    pd.BooleanDtype(): {"base": BooleanType, "nullable": True},

    # integer
    np.dtype(np.int8): {"base": Int8Type, "nullable": False},
    np.dtype(np.int16): {"base": Int16Type, "nullable": False},
    np.dtype(np.int32): {"base": Int32Type, "nullable": False},
    np.dtype(np.int64): {"base": Int64Type, "nullable": False},
    np.dtype(np.uint8): {"base": UInt8Type, "nullable": False},
    np.dtype(np.uint16): {"base": UInt16Type, "nullable": False},
    np.dtype(np.uint32): {"base": UInt32Type, "nullable": False},
    np.dtype(np.uint64): {"base": UInt64Type, "nullable": False},
    pd.Int8Dtype(): {"base": Int8Type, "nullable": True},
    pd.Int16Dtype(): {"base": Int16Type, "nullable": True},
    pd.Int32Dtype(): {"base": Int32Type, "nullable": True},
    pd.Int64Dtype(): {"base": Int64Type, "nullable": True},
    pd.UInt8Dtype(): {"base": UInt8Type, "nullable": True},
    pd.UInt16Dtype(): {"base": UInt16Type, "nullable": True},
    pd.UInt32Dtype(): {"base": UInt32Type, "nullable": True},
    pd.UInt64Dtype(): {"base": UInt64Type, "nullable": True},

    # float
    np.dtype(np.float16): {"base": Float16Type},
    np.dtype(np.float32): {"base": Float32Type},
    np.dtype(np.float64): {"base": Float64Type},
    np.dtype(np.longdouble): {"base": LongDoubleType},

    # complex
    np.dtype(np.complex64): {"base": Complex64Type},
    np.dtype(np.complex128): {"base": Complex128Type},
    np.dtype(np.clongdouble): {"base": CLongDoubleType},

    # datetime (M8) dtype handled in special case

    # timedelta (m8) dtype handled in special case

    # string
    np.dtype(str): {"base": StringType},
    pd.StringDtype("python"): {"base": StringType, "storage": "python"},
}
if PYARROW_INSTALLED:
    lookup[pd.StringDtype("pyarrow")] = {
        "base": StringType,
        "storage": "pyarrow"
    }
