import ctypes
import datetime
import decimal
from types import MappingProxyType

import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED


# TODO: put M8/datetime64/m8/timedelta aliases with specific units/step sizes
# into a separate test category.


# TODO: these tables should probably go in a separate types/.__init__.py
# test suite


# NOTE: reference on platform-specific numpy types and their ctype equivalents:
# https://numpy.org/doc/stable/reference/arrays.scalars.html#arrays-scalars-built-in

# To get an equivalent ctype for a given numpy dtype:
# https://numpy.org/doc/stable/reference/routines.ctypeslib.html#numpy.ctypeslib.as_ctypes_type


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


ELEMENT_TYPES = {
    # some types are platform-dependent.  For reference, these are indicated
    # with a comment showing the equivalent type as seen by the compiler.

    # boolean
    "bool": {
        "aliases": {
            "atomic": [bool, np.bool_],
            "dtype": [np.dtype(bool)],
            "string": ["bool", "boolean", "bool_", "bool8", "b1", "?"]
        },
        "output_type": np.dtype(bool),
    },
    "nullable[bool]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.BooleanDtype()],
            "string": [
                "nullable[bool]", "nullable[boolean]", "nullable[bool_]",
                "nullable[bool8]", "nullable[b1]", "nullable[?]", "Boolean"
            ],
        },
        "output_type": pd.BooleanDtype(),
    },

    # integer
    "int": {
        "aliases": {
            "atomic": [int, np.integer],
            "dtype": [np.dtype(np.int64)],
            "string": ["int", "integer"],
        },
        "output_type": np.dtype(np.int64),
    },
    "nullable[int]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.Int64Dtype()],
            "string": ["nullable[int]", "nullable[integer]"]
        },
        "output_type": pd.Int64Dtype(),
    },
    "signed": {
        "aliases": {
            "atomic": [np.signedinteger],
            "dtype": [np.dtype(np.int64)],
            "string": ["signed", "signed integer", "signed int", "i"]
        },
        "output_type": np.dtype(np.int64),
    },
    "nullable[signed]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.Int64Dtype()],
            "string": [
                "nullable[signed]", "nullable[signed integer]",
                "nullable[signed int]", "nullable[i]"
            ]
        },
        "output_type": pd.Int64Dtype(),
    },
    "unsigned": {
        "aliases": {
            "atomic": [np.unsignedinteger],
            "dtype": [np.dtype(np.uint64)],
            "string": [
                "unsigned", "unsigned integer", "unsigned int", "uint", "u"
            ]
        },
        "output_type": np.dtype(np.uint64),
    },
    "nullable[unsigned]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.UInt64Dtype()],
            "string": [
                "nullable[unsigned]", "nullable[unsigned integer]",
                "nullable[unsigned int]", "nullable[uint]", "nullable[u]"
            ]
        },
        "output_type": pd.UInt64Dtype(),
    },
    "int8": {
        "aliases": {
            "atomic": [np.int8],
            "dtype": [np.dtype(np.int8)],
            "string": ["int8", "i1"]
        },
        "output_type": np.dtype(np.int8),
    },
    "nullable[int8]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.Int8Dtype()],
            "string": ["nullable[int8]", "nullable[i1]", "Int8"]
        },
        "output_type": pd.Int8Dtype(),
    },
    "int16": {
        "aliases": {
            "atomic": [np.int16],
            "dtype": [np.dtype(np.int16)],
            "string": ["int16", "i2"]
        },
        "output_type": np.dtype(np.int16),
    },
    "nullable[int16]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.Int16Dtype()],
            "string": ["nullable[int16]", "nullable[i2]", "Int16"]
        },
        "output_type": pd.Int16Dtype(),
    },
    "int32": {
        "aliases": {
            "atomic": [np.int32],
            "dtype": [np.dtype(np.int32)],
            "string": ["int32", "i4"],
        },
        "output_type": np.dtype(np.int32),
    },
    "nullable[int32]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.Int32Dtype()],
            "string": ["nullable[int32]", "nullable[i4]", "Int32"]
        },
        "output_type": pd.Int32Dtype(),
    },
    "int64": {
        "aliases": {
            "atomic": [np.int64],
            "dtype": [np.dtype(np.int64)],
            "string": ["int64", "i8"]
        },
        "output_type": np.dtype(np.int64),
    },
    "nullable[int64]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.Int64Dtype()],
            "string": ["nullable[int64]", "nullable[i8]", "Int64"]
        },
        "output_type": pd.Int64Dtype(),
    },
    "uint8": {
        "aliases": {
            "atomic": [np.uint8],
            "dtype": [np.dtype(np.uint8)],
            "string": ["uint8", "u1"],
        },
        "output_type": np.dtype(np.uint8),
    },
    "nullable[uint8]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.UInt8Dtype()],
            "string": ["nullable[uint8]", "nullable[u1]", "UInt8"]
        },
        "output_type": pd.UInt8Dtype(),
    },
    "uint16": {
        "aliases": {
            "atomic": [np.uint16],
            "dtype": [np.dtype(np.uint16)],
            "string": ["uint16", "u2"]
        },
        "output_type": np.dtype(np.uint16),
    },
    "nullable[uint16]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.UInt16Dtype()],
            "string": ["nullable[uint16]", "nullable[u2]", "UInt16"]
        },
        "output_type": pd.UInt16Dtype(),
    },
    "uint32": {
        "aliases": {
            "atomic": [np.uint32],
            "dtype": [np.dtype(np.uint32)],
            "string": ["uint32", "u4"]
        },
        "output_type": np.dtype(np.uint32),
    },
    "nullable[uint32]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.UInt32Dtype()],
            "string": ["nullable[uint32]", "nullable[u4]", "UInt32"],
        },
        "output_type": pd.UInt32Dtype(),
    },
    "uint64": {
        "aliases": {
            "atomic": [np.uint64],
            "dtype": [np.dtype(np.uint64)],
            "string": ["uint64", "u8"]
        },
        "output_type": np.dtype(np.uint64),
    },
    "nullable[uint64]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.UInt64Dtype()],
            "string": ["nullable[uint64]", "nullable[u8]", "UInt64"]
        },
        "output_type": pd.UInt64Dtype(),
    },
    "char": {  # = C char
        "aliases": {
            "atomic": [np.byte],
            "dtype": [np.dtype(np.byte)],
            "string": ["char", "signed char", "byte", "b"]
        },
        "output_type": np.dtype(np.byte),
    },
    "nullable[char]": {  # = nullable extensions to C char
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "nullable[char]", "nullable[signed char]", "nullable[byte]",
                "nullable[b]"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.byte)],
    },
    "short": {  # = C short
        "aliases": {
            "atomic": [np.short],
            "dtype": [np.dtype(np.short)],
            "string": [
                "short", "short int", "signed short", "signed short int", "h"
            ]
        },
        "output_type": np.dtype(np.short),
    },
    "nullable[short]": {  # = nullable extensions to C short
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "nullable[short]", "nullable[short int]", "nullable[signed short]",
                "nullable[signed short int]", "nullable[h]"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.short)],
    },
    "intc": {  # = C int
        "aliases": {
            "atomic": [np.intc],
            "dtype": [np.dtype(np.intc)],
            "string": ["intc", "cint", "signed intc", "signed cint"]
        },
        "output_type": np.dtype(np.intc),
    },
    "nullable[intc]": {  # = nullable extensions to C int
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "nullable[intc]", "nullable[cint]", "nullable[signed intc]",
                "nullable[signed cint]"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.intc)],
    },
    "long": {  # = C long
        "aliases": {
            "atomic": [np.int_],
            "dtype": [np.dtype(np.int_)],
            "string": [
                "long", "long int", "signed long", "signed long int", "l"
            ]
        },
        "output_type": np.dtype(np.int_),
    },
    "nullable[long]": {  # = nullable extensions to C long
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "nullable[long]", "nullable[long int]", "nullable[signed long]",
                "nullable[signed long int]", "nullable[l]"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.int_)],
    },
    "long long": {  # = C long long
        "aliases": {
            "atomic": [np.longlong],
            "dtype": [np.dtype(np.longlong)],
            "string": [
                "long long", "longlong", "long long int", "signed long long",
                "signed longlong", "signed long long int", "q"
            ]
        },
        "output_type": np.dtype(np.longlong),
    },
    "nullable[long long]": {  # = nullable extensions to C long long
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "nullable[long long]", "nullable[longlong]",
                "nullable[long long int]", "nullable[signed long long]",
                "nullable[signed longlong]", "nullable[signed long long int]",
                "nullable[q]"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.longlong)],
    },
    "ssize_t": {  # = C ssize_t
        "aliases": {
            "atomic": [np.intp],
            "dtype": [np.dtype(np.intp)],
            "string": ["ssize_t", "intp", "int0", "p"]
        },
        "output_type": np.dtype(np.intp),
    },
    "nullable[ssize_t]": {  # = nullable extensions to C ssize_t
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "nullable[ssize_t]", "nullable[intp]", "nullable[int0]",
                "nullable[p]"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.intp)],
    },
    "unsigned char": {  # = C unsigned char
        "aliases": {
            "atomic": [np.ubyte],
            "dtype": [np.dtype(np.ubyte)],
            "string": ["unsigned char", "unsigned byte", "ubyte", "B"]
        },
        "output_type": np.dtype(np.ubyte),
    },
    "nullable[unsigned char]": {  # = nullable extensions to C unsigned char
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "nullable[unsigned char]", "nullable[unsigned byte]",
                "nullable[ubyte]", "nullable[B]"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.ubyte)],
    },
    "unsigned short": {  # = C unsigned short
        "aliases": {
            "atomic": [np.ushort],
            "dtype": [np.dtype(np.ushort)],
            "string": [
                "unsigned short", "unsigned short int", "ushort", "H"
            ]
        },
        "output_type": np.dtype(np.ushort),
    },
    "nullable[unsigned short]": {  # = nullable extensions to C unsigned short
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "nullable[unsigned short]", "nullable[unsigned short int]",
                "nullable[ushort]", "nullable[H]"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.ushort)],
    },
    "unsigned intc": {  # = C unsigned int
        "aliases": {
            "atomic": [np.uintc],
            "dtype": [np.dtype(np.uintc)],
            "string": [
                "unsigned intc", "unsigned cint", "uintc", "ucint", "I"
            ]
        },
        "output_type": np.dtype(np.uintc),
    },
    "nullable[unsigned intc]": {  # = nullable extensions to C unsigned int
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "nullable[unsigned intc]", "nullable[unsigned cint]",
                "nullable[uintc]", "nullable[ucint]", "nullable[I]"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.uintc)],
    },
    "unsigned long": {  # = C unsigned long
        "aliases": {
            "atomic": [np.uint],
            "dtype": [np.dtype(np.uint)],
            "string": ["unsigned long", "unsigned long int", "L"]
        },
        "output_type": np.dtype(np.uint),
    },
    "nullable[unsigned long]": {  # = nullable extensions to C unsigned long
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "nullable[unsigned long]", "nullable[unsigned long int]",
                "nullable[L]"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.uint)],
    },
    "unsigned long long": {  # = C unsigned long long
        "aliases": {
            "atomic": [np.ulonglong],
            "dtype": [np.dtype(np.ulonglong)],
            "string": [
                "nullable[unsigned long long]", "nullable[unsigned longlong]",
                "nullable[unsigned long long int]", "nullable[Q]"
            ]
        },
        "output_type": np.dtype(np.ulonglong),
    },
    "nullable[unsigned long long]": {  # = nullable extensions to C unsigned long long
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "unsigned long long", "unsigned longlong",
                "unsigned long long int", "Q"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.ulonglong)],
    },
    "size_t": {  # = C size_t
        "aliases": {
            "atomic": [np.uintp],
            "dtype": [np.dtype(np.uintp)],
            "string": ["size_t", "uintp", "uint0", "P"]
        },
        "output_type": np.dtype(np.uintp),
    },
    "nullable[size_t]": {  # = nullable extensions to C size_t
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": [
                "nullable[size_t]", "nullable[uintp]", "nullable[uint0]",
                "nullable[P]"
            ]
        },
        "output_type": EXTENSION_TYPES[np.dtype(np.uintp)],
    },

    # float
    "float": {
        "aliases": {
            "atomic": [float, np.floating],
            "dtype": [np.dtype(np.float64)],
            "string": ["float", "floating", "f"]
        },
        "output_type": np.dtype(np.float64),
    },
    "float16": {
        "aliases": {
            "atomic": [np.float16, np.half],
            "dtype": [np.dtype(np.float16)],
            "string": ["float16", "f2", "half", "e"]
        },
        "output_type": np.dtype(np.float16),
    },
    "float32": {
        "aliases": {
            "atomic": [np.float32, np.single],
            "dtype": [np.dtype(np.float32)],
            "string": ["float32", "f4", "single"]
        },
        "output_type": np.dtype(np.float32),
    },
    "float64": {
        "aliases": {
            "atomic": [np.float64, np.double],
            "dtype": [np.dtype(np.float64)],
            "string": ["float64", "f8", "float_", "double", "d"]
        },
        "output_type": np.dtype(np.float64),
    },
    "longdouble": {
        "aliases": {
            "atomic": [np.longdouble],  # TODO: np.float96/np.float128?
            "dtype": [np.dtype(np.longdouble)],
            "string": [
                "longdouble", "longfloat", "long double", "long float", "float96",
                "float128", "f12", "f16", "g"
            ]
        },
        "output_type": np.dtype(np.longdouble),
    },

    # complex
    "complex": {
        "aliases": {
            "atomic": [complex, np.complexfloating],
            "dtype": [np.dtype(np.complex128)],
            "string": [
                "complex", "complex floating", "complex float", "cfloat", "c"
            ]
        },
        "output_type": np.dtype(np.complex128),
    },
    "complex64": {
        "aliases": {
            "atomic": [np.complex64, np.csingle],
            "dtype": [np.dtype(np.complex64)],
            "string": [
                "complex64", "c8", "complex single", "csingle", "singlecomplex",
                "F"
            ]
        },
        "output_type": np.dtype(np.complex64),
    },
    "complex128": {
        "aliases": {
            "atomic": [np.complex128, np.cdouble],
            "dtype": [np.dtype(np.complex128)],
            "string": [
                "complex128", "c16", "complex double", "cdouble", "complex_", "D"
            ]
        },
        "output_type": np.dtype(np.complex128),
    },
    "clongdouble": {
        "aliases": {
            "atomic": [np.clongdouble],  # TODO: np.complex192/np.complex256?
            "dtype": [np.dtype(np.clongdouble)],
            "string": [
                "clongdouble", "clongfloat", "complex longdouble",
                "complex longfloat", "complex long double", "complex long float",
                "complex192", "complex256", "c24", "c32", "G"
            ]
        },
        "output_type": np.dtype(np.clongdouble),
    },

    # decimal
    "decimal": {
        "aliases": {
            "atomic": [decimal.Decimal],
            "dtype": [],
            "string": ["decimal"]
        },
        "output_type": np.dtype("O"),
    },

    # datetime
    "datetime": {
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": ["datetime"]
        },
        "output_type": np.dtype("M8[ns]"),
    },
    "datetime[pandas]": {
        "aliases": {
            "atomic": [pd.Timestamp],
            "dtype": [],
            "string": [
                "datetime[pandas]", "pandas.Timestamp", "pandas Timestamp",
                "pd.Timestamp"
            ]
        },
        "output_type": np.dtype("M8[ns]"),
    },
    "datetime[python]": {
        "aliases": {
            "atomic": [datetime.datetime],
            "dtype": [],
            "string": [
                "datetime[python]", "pydatetime", "datetime.datetime"
            ]
        },
        "output_type": np.dtype("O"),
    },
    "datetime[numpy]": {
        "aliases": {
            "atomic": [np.datetime64],
            "dtype": [
                np.dtype("M8"), np.dtype("M8[ns]"), np.dtype("M8[5us]"),
                np.dtype("M8[50ms]"), np.dtype("M8[2s]"), np.dtype("M8[30m]"),
                np.dtype("M8[h]"), np.dtype("M8[3D]"), np.dtype("M8[2W]"),
                np.dtype("M8[3M]"), np.dtype("M8[10Y]"),
            ],
            "string": [
                "datetime[numpy]", "numpy.datetime64", "numpy datetime64",
                "np.datetime64", "M8", "M8[ns]", "datetime64[5us]", "M8[50ms]",
                "M8[2s]", "datetime64[30m]", "datetime64[h]", "M8[3D]",
                "datetime64[2W]", "M8[3M]", "datetime64[10Y]"
            ]
        },
        "output_type": np.dtype("O"),
    },

    # timedelta
    "timedelta": {
        "aliases": {
            "atomic": [],
            "dtype": [],
            "string": ["timedelta"]
        },
        "output_type": np.dtype("m8[ns]"),
    },
    "timedelta[pandas]": {
        "aliases": {
            "atomic": [pd.Timedelta],
            "dtype": [],
            "string": [
                "timedelta[pandas]", "pandas.Timedelta", "pandas Timedelta",
                "pd.Timedelta"
            ]
        },
        "output_type": np.dtype("m8[ns]"),
    },
    "timedelta[python]": {
        "aliases": {
            "atomic": [datetime.timedelta],
            "dtype": [],
            "string": [
                "timedelta[python]", "pytimedelta", "datetime.timedelta"
            ]
        },
        "output_type": np.dtype("O"),
    },
    "timedelta[numpy]": {
        "aliases": {
            "atomic": [np.timedelta64],
            "dtype": [
                np.dtype("m8"), np.dtype("m8[ns]"), np.dtype("m8[5us]"),
                np.dtype("m8[50ms]"), np.dtype("m8[2s]"), np.dtype("m8[30m]"),
                np.dtype("m8[h]"), np.dtype("m8[3D]"), np.dtype("m8[2W]"),
                np.dtype("m8[3M]"), np.dtype("m8[10Y]"),
            ],
            "string": [
                "timedelta[numpy]", "numpy.timedelta64", "numpy timedelta64",
                "np.timedelta64", "m8", "m8[ns]", "timedelta64[5us]",
                "m8[50ms]", "m8[2s]", "timedelta64[30m]", "timedelta64[h]",
                "m8[3D]", "timedelta64[2W]", "m8[3M]", "timedelta64[10Y]"
            ]
        },
        "output_type": np.dtype("O"),
    },

    # string
    "str": {
        "aliases": {
            "atomic": [str, np.str_],
            "dtype": [np.dtype(str), np.dtype("U32")],
            "string": [
                "str", "string", "unicode", "U", "str0", "str_", "unicode_"
            ]
        },
        "output_type": DEFAULT_STRING_DTYPE,
    },
    "str[python]": {
        "aliases": {
            "atomic": [],
            "dtype": [pd.StringDtype("python")],
            "string": [
                "str[python]", "string[python]", "unicode[python]", "pystr",
                "pystring", "python string"
            ]
        },
        "output_type": pd.StringDtype("python"),
    },

    # object
    "object": {
        "aliases": {
            "atomic": [object],
            "dtype": [np.dtype("O")],
            "string": [
                "object", "obj", "O", "pyobject", "object_", "object0"
            ]
        },
        "output_type": np.dtype("O"),
    },
}
if PYARROW_INSTALLED:
    ELEMENT_TYPES["str[pyarrow]"] = {
        "aliases": {
            "atomic": [],
            "dtype": [pd.StringDtype("pyarrow")],
            "string": [
                "str[pyarrow]", "string[pyarrow]", "unicode[pyarrow]",
                "pyarrow str", "pyarrow string"
            ],
        },
        "output_type": pd.StringDtype("pyarrow")
    }


# MappingProxyType is immutable - prevents test-related side effects
EXTENSION_TYPES = MappingProxyType(EXTENSION_TYPES)
ELEMENT_TYPES = MappingProxyType(ELEMENT_TYPES)




# ATOMIC_ELEMENT_TYPES = {
#     "boolean": {
#         bool: np.dtype(bool),
#         np.bool_: np.dtype(bool),
#     },
#     "integer": {
#         int: np.dtype(np.int64),
#         np.integer: np.dtype(np.int64),
#         np.signedinteger: np.dtype(np.int64),
#         np.unsignedinteger: np.dtype(np.uint64),
#         np.int8: np.dtype(np.int8),
#         np.int16: np.dtype(np.int16),
#         np.int32: np.dtype(np.int32),
#         np.int64: np.dtype(np.int64),
#         np.uint8: np.dtype(np.uint8),
#         np.uint16: np.dtype(np.uint16),
#         np.uint32: np.dtype(np.uint32),
#         np.uint64: np.dtype(np.uint64),
#     },
#     "float": {
#         float: np.dtype(np.float64),
#         np.floating: np.dtype(np.float64),
#         np.float16: np.dtype(np.float16),
#         np.float32: np.dtype(np.float32),
#         np.float64: np.dtype(np.float64),
#         np.longdouble: np.dtype(np.longdouble)
#     },
#     "complex": {
#         complex: np.dtype(np.complex128),
#         np.complexfloating: np.dtype(np.complex128),
#         np.complex64: np.dtype(np.complex64),
#         np.complex128: np.dtype(np.complex128),
#         np.clongdouble: np.dtype(np.clongdouble),
#     },
#     "decimal": {
#         decimal.Decimal: np.dtype("O"),
#     },
#     "datetime": {
#         pd.Timestamp: np.dtype("M8[ns]"),
#         datetime.datetime: np.dtype("O"),
#         np.datetime64: np.dtype("O"),
#     },
#     "timedelta": {
#         pd.Timedelta: np.dtype("m8[ns]"),
#         datetime.timedelta: np.dtype("O"),
#         np.timedelta64: np.dtype("O"),
#     },
#     "string": {
#         str: DEFAULT_STRING_DTYPE,
#     },
#     "object": {
#         object: np.dtype("O"),
#     }
# }


# DTYPE_ELEMENT_TYPES = {
#     "boolean": {
#         np.dtype(bool): np.dtype(bool),
#         pd.BooleanDtype(): pd.BooleanDtype(),
#     },
#     "integer": {
#         np.dtype(np.int8): np.dtype(np.int8),
#         np.dtype(np.int16): np.dtype(np.int16),
#         np.dtype(np.int32): np.dtype(np.int32),
#         np.dtype(np.int64): np.dtype(np.int64),
#         np.dtype(np.uint8): np.dtype(np.uint8),
#         np.dtype(np.uint16): np.dtype(np.uint16),
#         np.dtype(np.uint32): np.dtype(np.uint32),
#         np.dtype(np.uint64): np.dtype(np.uint64),

#         pd.Int8Dtype(): pd.Int8Dtype(),
#         pd.Int16Dtype(): pd.Int16Dtype(),
#         pd.Int32Dtype(): pd.Int32Dtype(),
#         pd.Int64Dtype(): pd.Int64Dtype(),
#         pd.UInt8Dtype(): pd.UInt8Dtype(),
#         pd.UInt16Dtype(): pd.UInt16Dtype(),
#         pd.UInt32Dtype(): pd.UInt32Dtype(),
#         pd.UInt64Dtype(): pd.UInt64Dtype(),
#     },
#     "float": {
#         np.dtype(np.float16): np.dtype(np.float16),
#         np.dtype(np.float32): np.dtype(np.float32),
#         np.dtype(np.float64): np.dtype(np.float64),
#         np.dtype(np.longdouble): np.dtype(np.longdouble),
#     },
#     "complex": {
#         np.dtype(np.complex64): np.dtype(np.complex64),
#         np.dtype(np.complex128): np.dtype(np.complex128),
#         np.dtype(np.clongdouble): np.dtype(np.clongdouble),
#     },
#     "decimal": {},
#     "datetime": {
#         np.dtype("M8"): np.dtype("O"),
#         np.dtype("M8[ns]"): np.dtype("O"),
#         np.dtype("M8[5us]"): np.dtype("O"),
#         np.dtype("M8[50ms]"): np.dtype("O"),
#         np.dtype("M8[2s]"): np.dtype("O"),
#         np.dtype("M8[30m]"): np.dtype("O"),
#         np.dtype("M8[h]"): np.dtype("O"),
#         np.dtype("M8[3D]"): np.dtype("O"),
#         np.dtype("M8[2W]"): np.dtype("O"),
#         np.dtype("M8[3M]"): np.dtype("O"),
#         np.dtype("M8[10Y]"): np.dtype("O"),
#     },
#     "timedelta": {
#         np.dtype("m8"): np.dtype("O"),
#         np.dtype("m8[ns]"): np.dtype("O"),
#         np.dtype("m8[5us]"): np.dtype("O"),
#         np.dtype("m8[50ms]"): np.dtype("O"),
#         np.dtype("m8[2s]"): np.dtype("O"),
#         np.dtype("m8[30m]"): np.dtype("O"),
#         np.dtype("m8[h]"): np.dtype("O"),
#         np.dtype("m8[3D]"): np.dtype("O"),
#         np.dtype("m8[2W]"): np.dtype("O"),
#         np.dtype("m8[3M]"): np.dtype("O"),
#         np.dtype("m8[10Y]"): np.dtype("O"),
#     },
#     "string": {
#         np.dtype(str): DEFAULT_STRING_DTYPE,
#         np.dtype("U32"): DEFAULT_STRING_DTYPE,

#         pd.StringDtype("python"): pd.StringDtype("python"),
#     },
#     "object": {
#         np.dtype("O"): np.dtype("O"),
#     }
# }


# STRING_ELEMENT_TYPES = {
#     "boolean": {
#         "boolean": np.dtype(bool),
#         "bool_": np.dtype(bool),
#         "bool8": np.dtype(bool),
#         "b1": np.dtype(bool),
#         "bool": np.dtype(bool),
#         "?": np.dtype(bool),
#         "Boolean": pd.BooleanDtype(),
#     },
#     "integer": {
#         # integer supertype
#         "int": np.dtype(np.int64),
#         "integer": np.dtype(np.int64),

#         # signed integer supertype
#         "signed integer": np.dtype(np.int64),
#         "signed int": np.dtype(np.int64),
#         "signed": np.dtype(np.int64),
#         "i": np.dtype(np.int64),

#         # unsigned integer supertype
#         "uint": np.dtype(np.uint64),
#         "unsigned integer": np.dtype(np.uint64),
#         "unsigned int": np.dtype(np.uint64),
#         "unsigned": np.dtype(np.uint64),
#         "u": np.dtype(np.uint64),

#         # int8 (signed)
#         "int8": np.dtype(np.int8),
#         "i1": np.dtype(np.int8),
#         "byte": np.dtype(np.int8),
#         "signed char": np.dtype("byte"),  # platform-specific
#         "char": np.dtype("byte"),  # platform-specific
#         "Int8": pd.Int8Dtype(),  # pandas extension
#         "b": np.dtype(np.int8),

#         # int16 (signed)
#         "int16": np.dtype(np.int16),
#         "i2": np.dtype(np.int16),
#         "signed short int": np.dtype("short"),  # platform-specific
#         "signed short": np.dtype("short"),  # platform-specific
#         "short int": np.dtype("short"),  # platform-specific
#         "short": np.dtype("short"),  # platform-specific
#         "Int16": pd.Int16Dtype(),  # pandas extension
#         "h": np.dtype(np.int16),

#         # int32 (signed)
#         "int32": np.dtype(np.int32),
#         "i4": np.dtype(np.int32),
#         "cint": np.dtype("intc"),  # platform-specific
#         "signed cint": np.dtype("intc"),  # platform-specific
#         "signed intc": np.dtype("intc"),  # platform-specific
#         "intc": np.dtype("intc"),  # platform-specific
#         "Int32": pd.Int32Dtype(),  # pandas extension

#         # int64 (signed)
#         "int64": np.dtype(np.int64),
#         "i8": np.dtype(np.int64),
#         "intp": np.dtype(np.int64),
#         "int0": np.dtype(np.int64),
#         "signed long long int": np.dtype("longlong"),  # platform-specific
#         "signed long long": np.dtype("longlong"),  # platform-specific
#         "signed long int": np.dtype("int_"),  # platform-specific
#         "signed long": np.dtype("int_"),  # platform-specific
#         "long long int": np.dtype("longlong"),  # platform-specific
#         "long long": np.dtype("longlong"),  # platform-specific
#         "long int": np.dtype("int_"),  # platform-specific
#         "long": np.dtype("int_"),  # platform-specific
#         "l": np.dtype("int_"),
#         "Int64": pd.Int64Dtype(),  # pandas extension
#         "p": np.dtype(np.int64),

#         # uint8 (unsigned)
#         "uint8": np.dtype(np.uint8),
#         "u1": np.dtype(np.uint8),
#         "ubyte": np.dtype(np.uint8),
#         "unsigned char": np.dtype("ubyte"),  # platform-specific
#         "UInt8": pd.UInt8Dtype(),  # pandas extension
#         "B": np.dtype(np.uint8),

#         # uint16 (unsigned)
#         "uint16": np.dtype(np.uint16),
#         "u2": np.dtype(np.uint16),
#         "ushort": np.dtype("ushort"),  # platform-specific
#         "unsigned short int": np.dtype("ushort"),  # platform-specific
#         "unsigned short": np.dtype("ushort"),  # platform-specific
#         "UInt16": pd.UInt16Dtype(),  # pandas extension
#         "H": np.dtype(np.uint16),

#         # uint32 (unsigned)
#         "uint32": np.dtype(np.uint32),
#         "u4": np.dtype(np.uint32),
#         "ucint": np.dtype("uintc"),  # platform-specific
#         "unsigned cint": np.dtype("uintc"),  # platform-specific
#         "uintc": np.dtype("uintc"),  # platform-specific
#         "unsigned intc": np.dtype("uintc"),  # platform-specific
#         "UInt32": pd.UInt32Dtype(),  # pandas extension
#         "I": np.dtype(np.uint32),

#         # uint64 (unsigned)
#         "uint64": np.dtype(np.uint64),
#         "u8": np.dtype(np.uint64),
#         "uintp": np.dtype(np.uint64),
#         "uint0": np.dtype(np.uint64),
#         "unsigned long long int": np.dtype("ulonglong"),  # platform-specific
#         "unsigned long long": np.dtype("ulonglong"),  # platform-specific
#         "unsigned long int": np.dtype("uint"),  # platform-specific
#         "unsigned long": np.dtype("uint"),  # platform-specific
#         "L": np.dtype("uint"),  # platform-specific
#         "UInt64": pd.UInt64Dtype(),  # pandas extension
#         "P": np.dtype(np.uint64),
#     },
#     "float": {
#         # float supertype
#         "float": np.dtype(np.float64),
#         "floating": np.dtype(np.float64),
#         "f": np.dtype(np.float64),

#         # float16
#         "float16": np.dtype(np.float16),
#         "f2": np.dtype(np.float16),
#         "half": np.dtype(np.float16),  # IEEE 754 half-precision float
#         "e": np.dtype(np.float16),

#         # float32
#         "float32": np.dtype(np.float32),
#         "f4": np.dtype(np.float32),
#         "single": np.dtype(np.float32),  # IEEE 754 single-precision float

#         # float64
#         "float64": np.dtype(np.float64),
#         "f8": np.dtype(np.float64),
#         "float_": np.dtype(np.float64),
#         "double": np.dtype(np.float64),  # IEEE 754 double-precision float
#         "d": np.dtype(np.float64),

#         # longdouble - x86 extended precision format (platform-specific)
#         "float128": np.dtype(np.longdouble),
#         "float96": np.dtype(np.longdouble),
#         "f16": np.dtype(np.longdouble),
#         "f12": np.dtype(np.longdouble),
#         "longdouble": np.dtype("longdouble"),
#         "longfloat": np.dtype("longfloat"),
#         "long double": np.dtype("longdouble"),
#         "long float": np.dtype("longfloat"),
#         "g": np.dtype("longdouble"),
#     },
#     "complex": {
#         # complex supertype
#         "cfloat": np.dtype(np.complex128),
#         "complex float": np.dtype(np.complex128),
#         "complex floating": np.dtype(np.complex128),
#         "complex": np.dtype(np.complex128),
#         "c": np.dtype(np.complex128),

#         # complex64
#         "complex64": np.dtype(np.complex64),
#         "c8": np.dtype(np.complex64),
#         "csingle": np.dtype(np.complex64),
#         "complex single": np.dtype(np.complex64),
#         "singlecomplex": np.dtype(np.complex64),
#         "F": np.dtype(np.complex64),

#         # complex128
#         "complex128": np.dtype(np.complex128),
#         "c16": np.dtype(np.complex128),
#         "complex_": np.dtype(np.complex128),
#         "cdouble": np.dtype(np.complex128),
#         "complex double": np.dtype(np.complex128),
#         "D": np.dtype(np.complex128),

#         # clongdouble
#         "complex256": np.dtype(np.clongdouble),
#         "complex192": np.dtype(np.clongdouble),
#         "c32": np.dtype(np.clongdouble),
#         "c24": np.dtype(np.clongdouble),
#         "clongdouble": np.dtype("clongdouble"),
#         "complex longdouble": np.dtype("clongdouble"),
#         "complex long double": np.dtype("clongdouble"),
#         "clongfloat": np.dtype("clongdouble"),
#         "complex longfloat": np.dtype("clongdouble"),
#         "complex long float": np.dtype("clongdouble"),
#         "longcomplex": np.dtype("clongdouble"),
#         "long complex": np.dtype("clongdouble"),
#         "G": np.dtype("clongdouble"),
#     },
#     "decimal": {
#         # decimal supertype
#         "decimal": np.dtype("O"),
#         "arbitrary precision": np.dtype("O"),
#     },
#     "datetime": {
#         # datetime supertype
#         "datetime": np.dtype("M8[ns]"),

#         # pandas.Timestamp
#         "datetime[pandas]": np.dtype("M8[ns]"),
#         "pandas.Timestamp": np.dtype("M8[ns]"),
#         "pandas Timestamp": np.dtype("M8[ns]"),
#         "pd.Timestamp": np.dtype("M8[ns]"),

#         # datetime.datetime
#         "datetime[python]": np.dtype("O"),
#         "pydatetime": np.dtype("O"),
#         "datetime.datetime": np.dtype("O"),

#         # numpy.datetime64
#         "datetime[numpy]": np.dtype("O"),
#         "numpy.datetime64": np.dtype("O"),
#         "numpy datetime64": np.dtype("O"),
#         "np.datetime64": np.dtype("O"),
#         # "datetime64"/"M8" (with or without units) handled in special case
#     },
#     "timedelta": {
#         # timedelta supertype
#         "timedelta": np.dtype("m8[ns]"),

#         # pandas.Timedelta
#         "timedelta[pandas]": np.dtype("m8[ns]"),
#         "pandas.Timedelta": np.dtype("m8[ns]"),
#         "pandas Timedelta": np.dtype("m8[ns]"),
#         "pd.Timedelta": np.dtype("m8[ns]"),

#         # datetime.timedelta
#         "timedelta[python]": np.dtype("O"),
#         "pytimedelta": np.dtype("O"),
#         "datetime.timedelta": np.dtype("O"),

#         # numpy.timedelta64
#         "timedelta[numpy]": np.dtype("O"),
#         "numpy.timedelta64": np.dtype("O"),
#         "numpy timedelta64": np.dtype("O"),
#         "np.timedelta64": np.dtype("O"),
#         # "timedelta64"/"m8" (with or without units) handled in special case
#     },
#     "string": {
#         # string supertype
#         "string": DEFAULT_STRING_DTYPE,
#         "str": DEFAULT_STRING_DTYPE,
#         "unicode": DEFAULT_STRING_DTYPE,
#         "U": DEFAULT_STRING_DTYPE,
#         "str0": DEFAULT_STRING_DTYPE,
#         "str_": DEFAULT_STRING_DTYPE,
#         "unicode_": DEFAULT_STRING_DTYPE,

#         # python-backed string extension type
#         "string[python]": pd.StringDtype("python"),
#         "str[python]": pd.StringDtype("python"),
#         "unicode[python]": pd.StringDtype("python"),
#         "pystring": pd.StringDtype("python"),
#         "python string": pd.StringDtype("python"),
#     },
#     "object": {
#         # object supertype
#         "object": np.dtype("O"),
#         "obj": np.dtype("O"),
#         "O": np.dtype("O"),
#         "pyobject": np.dtype("O"),
#         "object_": np.dtype("O"),
#         "object0": np.dtype("O")
#     }
# }


# if PYARROW_INSTALLED:
#     DTYPE_ELEMENT_TYPES["string"].update({
#         pd.StringDtype("pyarrow"): pd.StringDtype("pyarrow")
#     })

#     STRING_ELEMENT_TYPES["string"].update({
#         # pyarrow-backed string extension type
#         "string[pyarrow]": pd.StringDtype("pyarrow"),
#         "str[pyarrow]": pd.StringDtype("pyarrow"),
#         "unicode[pyarrow]": pd.StringDtype("pyarrow"),
#         "pyarrow string": pd.StringDtype("pyarrow"),
#     })
