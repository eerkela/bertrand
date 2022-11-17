from types import MappingProxyType

import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED


EXTENSION_TYPES = {
    np.dtype(bool): pd.BooleanDtype(),
    np.dtype(np.int8): pd.Int8Dtype(),
    np.dtype(np.int16): pd.Int16Dtype(),
    np.dtype(np.int32): pd.Int32Dtype(),
    np.dtype(np.int64): pd.Int64Dtype(),
    np.dtype(np.uint8): pd.UInt8Dtype(),
    np.dtype(np.uint16): pd.UInt16Dtype(),
    np.dtype(np.uint32): pd.UInt32Dtype(),
    np.dtype(np.uint64): pd.UInt64Dtype()
}


SERIES_TYPES = {
    # some types are platform-dependent.  For reference, these are indicated
    # with a comment showing the equivalent type as seen by the compiler.

    # boolean
    "boolean": {
        "bool": np.dtype(bool),
        "nullable[bool]": pd.BooleanDtype(),
    },

    # integer
    "integer": {
        "int": np.dtype(np.int64),
        "signed": np.dtype(np.int64),
        "unsigned": np.dtype(np.uint64),
        "int8": np.dtype(np.int8),
        "int16": np.dtype(np.int16),
        "int32": np.dtype(np.int32),
        "int64": np.dtype(np.int64),
        "uint8": np.dtype(np.uint8),
        "uint16": np.dtype(np.uint16),
        "uint32": np.dtype(np.uint32),
        "uint64": np.dtype(np.uint64),
        "nullable[int]": pd.Int64Dtype(),
        "nullable[signed]": pd.Int64Dtype(),
        "nullable[unsigned]": pd.UInt64Dtype(),
        "nullable[int8]": pd.Int8Dtype(),
        "nullable[int16]": pd.Int16Dtype(),
        "nullable[int32]": pd.Int32Dtype(),
        "nullable[int64]": pd.Int64Dtype(),
        "nullable[uint8]": pd.UInt8Dtype(),
        "nullable[uint16]": pd.UInt16Dtype(),
        "nullable[uint32]": pd.UInt32Dtype(),
        "nullable[uint64]": pd.UInt64Dtype(),
    },

    # float
    "float": {
        "float": np.dtype(np.float64),
        "float16": np.dtype(np.float16),
        "float32": np.dtype(np.float32),
        "float64": np.dtype(np.float64),
        "longdouble": np.dtype(np.longdouble),
    },

    # complex
    "complex": {
        "complex": np.dtype(np.complex128),
        "complex64": np.dtype(np.complex64),
        "complex128": np.dtype(np.complex128),
        "clongdouble": np.dtype(np.clongdouble),
    },

    # decimal
    "decimal": {
        "decimal": np.dtype("O"),
    },

    # datetime
    "datetime": {
        "datetime": np.dtype("M8[ns]"),
        "datetime[pandas]": np.dtype("M8[ns]"),
        "datetime[python]": np.dtype("O"),
        "datetime[numpy]": np.dtype("O"),
    },

    # timedelta
    "timedelta": {
        "timedelta": np.dtype("m8[ns]"),
        "timedelta[pandas]": np.dtype("m8[ns]"),
        "timedelta[python]": np.dtype("O"),
        "timedelta[numpy]": np.dtype("O"),
    },

    # string
    "string": {
        "str": DEFAULT_STRING_DTYPE,
        "str[python]": pd.StringDtype("python"),
    },

    # object
    "object": {
        "object": np.dtype("O"),
    },
}
if PYARROW_INSTALLED:
    SERIES_TYPES["string"]["str[pyarrow]"] = pd.StringDtype("pyarrow")


# MappingProxyType is immutable - prevents test-related side effects
EXTENSION_TYPES = MappingProxyType(EXTENSION_TYPES)
SERIES_TYPES = MappingProxyType(SERIES_TYPES)
