import ctypes
import datetime
import decimal
from types import MappingProxyType

import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED



# TODO: these should go in __init__.py



# TODO: exclude platform-dependent element types from cast tests.  These should
# be handled under tests/types/ instead.  ELEMENT_TYPES can then disregard them
# and only test the fixed-width alternatives.


MISSING_VALUES = {
    "boolean": pd.NA,
    "integer": pd.NA,
    "float": np.nan,
    "complex": complex("nan+nanj"),
}




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
