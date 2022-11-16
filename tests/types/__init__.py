import ctypes
import datetime
import decimal
from types import MappingProxyType

import numpy as np
import pandas as pd

from pdtypes import PYARROW_INSTALLED


TYPE_ALIASES = {
    # some types are platform-dependent.  For reference, these are indicated
    # with a comment showing the equivalent type as seen by the compiler.

    # boolean
    "bool": {
        "atomic": [bool, np.bool_],
        "dtype": [np.dtype(bool)],
        "string": ["bool", "boolean", "bool_", "bool8", "b1", "?"],
    },
    "nullable[bool]": {
        "atomic": [],
        "dtype": [pd.BooleanDtype()],
        "string": [
            "nullable[bool]", "nullable[boolean]", "nullable[bool_]",
            "nullable[bool8]", "nullable[b1]", "nullable[?]", "Boolean"
        ],
    },

    # integer
    "int": {
        "atomic": [int, np.integer],
        "dtype": [np.dtype(np.int64)],
        "string": ["int", "integer"],
    },
    "nullable[int]": {
        "atomic": [],
        "dtype": [pd.Int64Dtype()],
        "string": ["nullable[int]", "nullable[integer]"],
    },
    "signed": {
        "atomic": [np.signedinteger],
        "dtype": [np.dtype(np.int64)],
        "string": ["signed", "signed integer", "signed int", "i"],
    },
    "nullable[signed]": {
        "atomic": [],
        "dtype": [pd.Int64Dtype()],
        "string": [
            "nullable[signed]", "nullable[signed integer]",
            "nullable[signed int]", "nullable[i]"
        ],
    },
    "unsigned": {
        "atomic": [np.unsignedinteger],
        "dtype": [np.dtype(np.uint64)],
        "string": [
            "unsigned", "unsigned integer", "unsigned int", "uint", "u"
        ],
    },
    "nullable[unsigned]": {
        "atomic": [],
        "dtype": [pd.UInt64Dtype()],
        "string": [
            "nullable[unsigned]", "nullable[unsigned integer]",
            "nullable[unsigned int]", "nullable[uint]", "nullable[u]"
        ],
    },
    "int8": {
        "atomic": [np.int8],
        "dtype": [np.dtype(np.int8)],
        "string": ["int8", "i1"],
    },
    "nullable[int8]": {
        "atomic": [],
        "dtype": [pd.Int8Dtype()],
        "string": ["nullable[int8]", "nullable[i1]", "Int8"],
    },
    "int16": {
        "atomic": [np.int16],
        "dtype": [np.dtype(np.int16)],
        "string": ["int16", "i2"],
    },
    "nullable[int16]": {
        "atomic": [],
        "dtype": [pd.Int16Dtype()],
        "string": ["nullable[int16]", "nullable[i2]", "Int16"],
    },
    "int32": {
        "atomic": [np.int32],
        "dtype": [np.dtype(np.int32)],
        "string": ["int32", "i4"],
    },
    "nullable[int32]": {
        "atomic": [],
        "dtype": [pd.Int32Dtype()],
        "string": ["nullable[int32]", "nullable[i4]", "Int32"],
    },
    "int64": {
        "atomic": [np.int64],
        "dtype": [np.dtype(np.int64)],
        "string": ["int64", "i8"],
    },
    "nullable[int64]": {
        "atomic": [],
        "dtype": [pd.Int64Dtype()],
        "string": ["nullable[int64]", "nullable[i8]", "Int64"],
    },
    "uint8": {
        "atomic": [np.uint8],
        "dtype": [np.dtype(np.uint8)],
        "string": ["uint8", "u1"],
    },
    "nullable[uint8]": {
        "atomic": [],
        "dtype": [pd.UInt8Dtype()],
        "string": ["nullable[uint8]", "nullable[u1]", "UInt8"],
    },
    "uint16": {
        "atomic": [np.uint16],
        "dtype": [np.dtype(np.uint16)],
        "string": ["uint16", "u2"],
    },
    "nullable[uint16]": {
        "atomic": [],
        "dtype": [pd.UInt16Dtype()],
        "string": ["nullable[uint16]", "nullable[u2]", "UInt16"],
    },
    "uint32": {
        "atomic": [np.uint32],
        "dtype": [np.dtype(np.uint32)],
        "string": ["uint32", "u4"],
    },
    "nullable[uint32]": {
        "atomic": [],
        "dtype": [pd.UInt32Dtype()],
        "string": ["nullable[uint32]", "nullable[u4]", "UInt32"],
    },
    "uint64": {
        "atomic": [np.uint64],
        "dtype": [np.dtype(np.uint64)],
        "string": ["uint64", "u8"],
    },
    "nullable[uint64]": {
        "atomic": [],
        "dtype": [pd.UInt64Dtype()],
        "string": ["nullable[uint64]", "nullable[u8]", "UInt64"],
    },
    "char": {  # = C char
        "atomic": [np.byte],
        "dtype": [np.dtype(np.byte)],
        "string": ["char", "signed char", "byte", "b"],
    },
    "nullable[char]": {  # = nullable extensions to C char
        "atomic": [],
        "dtype": [],
        "string": [
            "nullable[char]", "nullable[signed char]", "nullable[byte]",
            "nullable[b]"
        ],
    },
    "short": {  # = C short
        "atomic": [np.short],
        "dtype": [np.dtype(np.short)],
        "string": [
            "short", "short int", "signed short", "signed short int", "h"
        ],
    },
    "nullable[short]": {  # = nullable extensions to C short
        "atomic": [],
        "dtype": [],
        "string": [
            "nullable[short]", "nullable[short int]", "nullable[signed short]",
            "nullable[signed short int]", "nullable[h]"
        ],
    },
    "intc": {  # = C int
        "atomic": [np.intc],
        "dtype": [np.dtype(np.intc)],
        "string": ["intc", "cint", "signed intc", "signed cint"],
    },
    "nullable[intc]": {  # = nullable extensions to C int
        "atomic": [],
        "dtype": [],
        "string": [
            "nullable[intc]", "nullable[cint]", "nullable[signed intc]",
            "nullable[signed cint]"
        ],
    },
    "long": {  # = C long
        "atomic": [np.int_],
        "dtype": [np.dtype(np.int_)],
        "string": [
            "long", "long int", "signed long", "signed long int", "l"
        ],
    },
    "nullable[long]": {  # = nullable extensions to C long
        "atomic": [],
        "dtype": [],
        "string": [
            "nullable[long]", "nullable[long int]", "nullable[signed long]",
            "nullable[signed long int]", "nullable[l]"
        ],
    },
    "long long": {  # = C long long
        "atomic": [np.longlong],
        "dtype": [np.dtype(np.longlong)],
        "string": [
            "long long", "longlong", "long long int", "signed long long",
            "signed longlong", "signed long long int", "q"
        ],
    },
    "nullable[long long]": {  # = nullable extensions to C long long
        "atomic": [],
        "dtype": [],
        "string": [
            "nullable[long long]", "nullable[longlong]",
            "nullable[long long int]", "nullable[signed long long]",
            "nullable[signed longlong]", "nullable[signed long long int]",
            "nullable[q]"
        ],
    },
    "ssize_t": {  # = C ssize_t
        "atomic": [np.intp],
        "dtype": [np.dtype(np.intp)],
        "string": ["ssize_t", "intp", "int0", "p"],
    },
    "nullable[ssize_t]": {  # = nullable extensions to C ssize_t
        "atomic": [],
        "dtype": [],
        "string": [
            "nullable[ssize_t]", "nullable[intp]", "nullable[int0]",
            "nullable[p]"
        ],
    },
    "unsigned char": {  # = C unsigned char
        "atomic": [np.ubyte],
        "dtype": [np.dtype(np.ubyte)],
        "string": ["unsigned char", "unsigned byte", "ubyte", "B"],
    },
    "nullable[unsigned char]": {  # = nullable extensions to C unsigned char
        "atomic": [],
        "dtype": [],
        "string": [
            "nullable[unsigned char]", "nullable[unsigned byte]",
            "nullable[ubyte]", "nullable[B]"
        ],
    },
    "unsigned short": {  # = C unsigned short
        "atomic": [np.ushort],
        "dtype": [np.dtype(np.ushort)],
        "string": [
            "unsigned short", "unsigned short int", "ushort", "H"
        ],
    },
    "nullable[unsigned short]": {  # = nullable extensions to C unsigned short
        "atomic": [],
        "dtype": [],
        "string": [
            "nullable[unsigned short]", "nullable[unsigned short int]",
            "nullable[ushort]", "nullable[H]"
        ],
    },
    "unsigned intc": {  # = C unsigned int
        "atomic": [np.uintc],
        "dtype": [np.dtype(np.uintc)],
        "string": [
            "unsigned intc", "unsigned cint", "uintc", "ucint", "I"
        ],
    },
    "nullable[unsigned intc]": {  # = nullable extensions to C unsigned int
        "atomic": [],
        "dtype": [],
        "string": [
            "nullable[unsigned intc]", "nullable[unsigned cint]",
            "nullable[uintc]", "nullable[ucint]", "nullable[I]"
        ],
    },
    "unsigned long": {  # = C unsigned long
        "atomic": [np.uint],
        "dtype": [np.dtype(np.uint)],
        "string": ["unsigned long", "unsigned long int", "L"],
    },
    "nullable[unsigned long]": {  # = nullable extensions to C unsigned long
        "atomic": [],
        "dtype": [],
        "string": [
            "nullable[unsigned long]", "nullable[unsigned long int]",
            "nullable[L]"
        ],
    },
    "unsigned long long": {  # = C unsigned long long
        "atomic": [np.ulonglong],
        "dtype": [np.dtype(np.ulonglong)],
        "string": [
            "nullable[unsigned long long]", "nullable[unsigned longlong]",
            "nullable[unsigned long long int]", "nullable[Q]"
        ],
    },
    "nullable[unsigned long long]": {  # = nullable extensions to C unsigned long long
        "atomic": [],
        "dtype": [],
        "string": [
            "unsigned long long", "unsigned longlong",
            "unsigned long long int", "Q"
        ],
    },
    "size_t": {  # = C size_t
        "atomic": [np.uintp],
        "dtype": [np.dtype(np.uintp)],
        "string": ["size_t", "uintp", "uint0", "P"],
    },
    "nullable[size_t]": {  # = nullable extensions to C size_t
        "atomic": [],
        "dtype": [],
        "string": [
            "nullable[size_t]", "nullable[uintp]", "nullable[uint0]",
            "nullable[P]"
        ],
    },

    # float
    "float": {
        "atomic": [float, np.floating],
        "dtype": [np.dtype(np.float64)],
        "string": ["float", "floating", "f"],
    },
    "float16": {
        "atomic": [np.float16, np.half],
        "dtype": [np.dtype(np.float16)],
        "string": ["float16", "f2", "half", "e"],
    },
    "float32": {
        "atomic": [np.float32, np.single],
        "dtype": [np.dtype(np.float32)],
        "string": ["float32", "f4", "single"],
    },
    "float64": {
        "atomic": [np.float64, np.double],
        "dtype": [np.dtype(np.float64)],
        "string": ["float64", "f8", "float_", "double", "d"],
    },
    "longdouble": {
        "atomic": [np.longdouble],  # TODO: np.float96/np.float128?
        "dtype": [np.dtype(np.longdouble)],
        "string": [
            "longdouble", "longfloat", "long double", "long float", "float96",
            "float128", "f12", "f16", "g"
        ],
    },

    # complex
    "complex": {
        "atomic": [complex, np.complexfloating],
        "dtype": [np.dtype(np.complex128)],
        "string": [
            "complex", "complex floating", "complex float", "cfloat", "c"
        ],
    },
    "complex64": {
        "atomic": [np.complex64, np.csingle],
        "dtype": [np.dtype(np.complex64)],
        "string": [
            "complex64", "c8", "complex single", "csingle", "singlecomplex",
            "F"
        ],
    },
    "complex128": {
        "atomic": [np.complex128, np.cdouble],
        "dtype": [np.dtype(np.complex128)],
        "string": [
            "complex128", "c16", "complex double", "cdouble", "complex_", "D"
        ],
    },
    "clongdouble": {
        "atomic": [np.clongdouble],  # TODO: np.complex192/np.complex256?
        "dtype": [np.dtype(np.clongdouble)],
        "string": [
            "clongdouble", "clongfloat", "complex longdouble",
            "complex longfloat", "complex long double", "complex long float",
            "complex192", "complex256", "c24", "c32", "G"
        ]
    },

    # decimal
    "decimal": {
        "atomic": [decimal.Decimal],
        "dtype": [],
        "string": ["decimal"],
    },

    # datetime
    "datetime": {
        "atomic": [],
        "dtype": [],
        "string": ["datetime"],
    },
    "datetime[pandas]": {
        "atomic": [pd.Timestamp],
        "dtype": [],
        "string": [
            "datetime[pandas]", "pandas.Timestamp", "pandas Timestamp",
            "pd.Timestamp"
        ],
    },
    "datetime[python]": {
        "atomic": [datetime.datetime],
        "dtype": [],
        "string": [
            "datetime[python]", "pydatetime", "datetime.datetime"
        ],
    },
    "datetime[numpy]": {
        "atomic": [np.datetime64],
        "dtype": [np.dtype("M8")],
        "string": [
            "datetime[numpy]", "numpy.datetime64", "numpy datetime64",
            "np.datetime64", "M8", "M8[ns]", "datetime64[5us]", "M8[50ms]",
            "M8[2s]", "datetime64[30m]", "datetime64[h]", "M8[3D]",
            "datetime64[2W]", "M8[3M]", "datetime64[10Y]"
        ],
    },

    # timedelta
    "timedelta": {
        "atomic": [],
        "dtype": [],
        "string": ["timedelta"],
    },
    "timedelta[pandas]": {
        "atomic": [pd.Timedelta],
        "dtype": [],
        "string": [
            "timedelta[pandas]", "pandas.Timedelta", "pandas Timedelta",
            "pd.Timedelta"
        ],
    },
    "timedelta[python]": {
        "atomic": [datetime.timedelta],
        "dtype": [],
        "string": [
            "timedelta[python]", "pytimedelta", "datetime.timedelta"
        ],
    },
    "timedelta[numpy]": {
        "atomic": [np.timedelta64],
        "dtype": [np.dtype("m8")],
        "string": [
            "timedelta[numpy]", "numpy.timedelta64", "numpy timedelta64",
            "np.timedelta64", "m8", "m8[ns]", "timedelta64[5us]",
            "m8[50ms]", "m8[2s]", "timedelta64[30m]", "timedelta64[h]",
            "m8[3D]", "timedelta64[2W]", "m8[3M]", "timedelta64[10Y]"
        ],
    },

    # string
    "str": {
        "atomic": [str, np.str_],
        "dtype": [np.dtype(str), np.dtype("U32")],
        "string": [
            "str", "string", "unicode", "U", "str0", "str_", "unicode_"
        ]
    },
    "str[python]": {
        "atomic": [],
        "dtype": [pd.StringDtype("python")],
        "string": [
            "str[python]", "string[python]", "unicode[python]", "pystr",
            "pystring", "python string"
        ],
    },

    # object
    "object": {
        "atomic": [object],
        "dtype": [np.dtype("O")],
        "string": [
            "object", "obj", "O", "pyobject", "object_", "object0"
        ],
    },
}
if PYARROW_INSTALLED:
    TYPE_ALIASES["str[pyarrow]"] = {
        "atomic": [],
        "dtype": [pd.StringDtype("pyarrow")],
        "string": [
            "str[pyarrow]", "string[pyarrow]", "unicode[pyarrow]",
            "pyarrow str", "pyarrow string"
        ],
    }


# MappingProxyType is immutable - prevents test-related side effects
TYPE_ALIASES = MappingProxyType(TYPE_ALIASES)
