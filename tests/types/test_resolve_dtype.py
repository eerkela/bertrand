from __future__ import annotations
import datetime
import decimal
from types import MappingProxyType

import numpy as np
import pandas as pd

from tests.types.scheme import parametrize, TypeCase, TypeParameters

from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED
from pdtypes.types import (
    resolve_dtype, BooleanType, IntegerType, SignedIntegerType,
    UnsignedIntegerType, Int8Type, Int16Type, Int32Type, Int64Type, UInt8Type,
    UInt16Type, UInt32Type, UInt64Type, FloatType, Float16Type, Float32Type,
    Float64Type, LongDoubleType, ComplexType, Complex64Type, Complex128Type,
    CLongDoubleType, DecimalType, DatetimeType, PandasTimestampType,
    PyDatetimeType, NumpyDatetime64Type, TimedeltaType, PandasTimedeltaType,
    PyTimedeltaType, NumpyTimedelta64Type, StringType, ObjectType
)


# NOTE: reference on platform-specific numpy types and their ctype equivalents:
# https://numpy.org/doc/stable/reference/arrays.scalars.html#arrays-scalars-built-in

# To get an equivalent ctype for a given numpy dtype:
# https://numpy.org/doc/stable/reference/routines.ctypeslib.html#numpy.ctypeslib.as_ctypes_type


####################
####    DATA    ####
####################


# TODO: these should go in their own factory functions just like tests/cast
# -> eliminates need for MappingProxyType


dtype_element_types = {
    # bool
    np.dtype(bool): BooleanType.instance(),

    # nullable[bool]
    pd.BooleanDtype(): BooleanType.instance(nullable=True),

    # int8
    np.dtype(np.int8): Int8Type.instance(),

    # nullable[int8]
    pd.Int8Dtype(): Int8Type.instance(nullable=True),

    # int16
    np.dtype(np.int16): Int16Type.instance(),

    # nullable[int16]
    pd.Int16Dtype(): Int16Type.instance(nullable=True),

    # int32
    np.dtype(np.int32): Int32Type.instance(),

    # nullable[int32]
    pd.Int32Dtype(): Int32Type.instance(nullable=True),

    # int64
    np.dtype(np.int64): Int64Type.instance(),

    # nullable[int64]
    pd.Int64Dtype(): Int64Type.instance(nullable=True),

    # uint8
    np.dtype(np.uint8): UInt8Type.instance(),

    # nullable[uint8]
    pd.UInt8Dtype(): UInt8Type.instance(nullable=True),

    # uint16
    np.dtype(np.uint16): UInt16Type.instance(),

    # nullable[uint16]
    pd.UInt16Dtype(): UInt16Type.instance(nullable=True),

    # uint32
    np.dtype(np.uint32): UInt32Type.instance(),

    # nullable[uint32]
    pd.UInt32Dtype(): UInt32Type.instance(nullable=True),

    # uint64
    np.dtype(np.uint64): UInt64Type.instance(),

    # nullable[uint64]
    pd.UInt64Dtype(): UInt64Type.instance(nullable=True),

    # float16
    np.dtype(np.float16): Float16Type.instance(),

    # float32
    np.dtype(np.float32): Float32Type.instance(),

    # float64
    np.dtype(np.float64): Float64Type.instance(),

    # longdouble = C long double
    np.dtype(np.longdouble): LongDoubleType.instance(),

    # complex64
    np.dtype(np.complex64): Complex64Type.instance(),

    # complex128
    np.dtype(np.complex128): Complex128Type.instance(),

    # clongdouble = (complex) C long double
    np.dtype(np.clongdouble): CLongDoubleType.instance(),

    # datetime[numpy]
    np.dtype("M8"): NumpyDatetime64Type.instance(),
    # NOTE: "M8" with specific units/step size handled in special case

    # timedelta[numpy]
    np.dtype("m8"): NumpyTimedelta64Type.instance(),
    # NOTE: "m8" with specific units/step size handled in special case

    # string
    np.dtype(str): StringType.instance(),
    # NOTE: "U" with specific length handled in special case

    # string[python]
    pd.StringDtype("python"): StringType.instance(storage="python"),

    # object
    np.dtype("O"): ObjectType.instance(),
}


atomic_element_types = {
    # some types are platform-dependent.  For reference, these are indicated
    # with a comment showing the equivalent type as seen by compilers.

    # bool
    bool: BooleanType.instance(),
    np.bool_: BooleanType.instance(),

    # int
    int: IntegerType.instance(),
    np.integer: IntegerType.instance(),

    # signed
    np.signedinteger: SignedIntegerType.instance(),

    # unsigned
    np.unsignedinteger: UnsignedIntegerType.instance(),

    # int8
    np.int8: Int8Type.instance(),

    # int16
    np.int16: Int16Type.instance(),

    # int32
    np.int32: Int32Type.instance(),

    # int64
    np.int64: Int64Type.instance(),

    # uint8
    np.uint8: UInt8Type.instance(),

    # uint16
    np.uint16: UInt16Type.instance(),

    # uint32
    np.uint32: UInt32Type.instance(),

    # uint64
    np.uint64: UInt64Type.instance(),

    # char = C char
    np.byte: dtype_element_types[np.dtype(np.byte)],

    # short = C short
    np.short: dtype_element_types[np.dtype(np.short)],

    # intc = C int
    np.intc: dtype_element_types[np.dtype(np.intc)],

    # long = C long
    np.int_: dtype_element_types[np.dtype(np.int_)],

    # long long = C long long
    np.longlong: dtype_element_types[np.dtype(np.longlong)],

    # ssize_t = C ssize_t
    np.intp: dtype_element_types[np.dtype(np.intp)],

    # unsigned char = C unsigned char
    np.ubyte: dtype_element_types[np.dtype(np.ubyte)],

    # unsigned short = C unsigned short
    np.ushort: dtype_element_types[np.dtype(np.ushort)],

    # unsigned intc = C unsigned int
    np.uintc: dtype_element_types[np.dtype(np.uintc)],

    # unsigned long = C unsigned long
    np.uint: dtype_element_types[np.dtype(np.uint)],

    # unsigned long long = C unsigned long long
    np.ulonglong: dtype_element_types[np.dtype(np.ulonglong)],

    # size_t = C size_t
    np.uintp: dtype_element_types[np.dtype(np.uintp)],

    # float
    float: FloatType.instance(),
    np.floating: FloatType.instance(),

    # float16
    np.float16: Float16Type.instance(),
    np.half: Float16Type.instance(),

    # float32
    np.float32: Float32Type.instance(),
    np.single: Float32Type.instance(),

    # float64
    np.float64: Float64Type.instance(),
    np.double: Float64Type.instance(),

    # longdouble = C long double
    # TODO: np.float96/np.float128
    np.longdouble: LongDoubleType.instance(),

    # complex
    complex: ComplexType.instance(),
    np.complexfloating: ComplexType.instance(),

    # complex64
    np.complex64: Complex64Type.instance(),
    np.csingle: Complex64Type.instance(),

    # complex128
    np.complex128: Complex128Type.instance(),
    np.cdouble: Complex128Type.instance(),

    # clongdouble = (complex) C long double
    # TODO: np.complex192/np.complex256?
    np.clongdouble: CLongDoubleType.instance(),

    # decimal
    decimal.Decimal: DecimalType.instance(),

    # datetime[pandas]
    pd.Timestamp: PandasTimestampType.instance(),

    # datetime[python]
    datetime.datetime: PyDatetimeType.instance(),

    # datetime[numpy]
    np.datetime64: NumpyDatetime64Type.instance(),

    # timedelta[pandas]
    pd.Timedelta: PandasTimedeltaType.instance(),

    # timedelta[python]
    datetime.timedelta: PyTimedeltaType.instance(),

    # timedelta[numpy]
    np.timedelta64: NumpyTimedelta64Type.instance(),

    # string
    str: StringType.instance(),
    np.str_: StringType.instance(),

    # object
    object: ObjectType.instance(),
}


string_element_types = {
    # some types are platform-dependent.  For reference, these are indicated
    # with a comment showing the equivalent type as seen by compilers.

    # bool
    "bool": BooleanType.instance(),
    "boolean": BooleanType.instance(),
    "bool_": BooleanType.instance(),
    "bool8": BooleanType.instance(),
    "b1": BooleanType.instance(),
    "?": BooleanType.instance(),

    # nullable[bool]
    "nullable[bool]": BooleanType.instance(nullable=True),
    "nullable[boolean]": BooleanType.instance(nullable=True),
    "nullable[bool_]": BooleanType.instance(nullable=True),
    "nullable[bool8]": BooleanType.instance(nullable=True),
    "nullable[b1]": BooleanType.instance(nullable=True),
    "nullable[?]": BooleanType.instance(nullable=True),
    "Boolean": BooleanType.instance(nullable=True),

    # int
    "int": IntegerType.instance(),
    "integer": IntegerType.instance(),

    # nullable[int]
    "nullable[int]": IntegerType.instance(nullable=True),
    "nullable[integer]": IntegerType.instance(nullable=True),

    # signed
    "signed": SignedIntegerType.instance(),
    "signed integer": SignedIntegerType.instance(),
    "signed int": SignedIntegerType.instance(),
    "i": SignedIntegerType.instance(),

    # nullable[signed]
    "nullable[signed]": SignedIntegerType.instance(nullable=True),
    "nullable[signed integer]": SignedIntegerType.instance(nullable=True),
    "nullable[signed int]": SignedIntegerType.instance(nullable=True),
    "nullable[i]": SignedIntegerType.instance(nullable=True),

    # unsigned
    "unsigned": UnsignedIntegerType.instance(),
    "unsigned integer": UnsignedIntegerType.instance(),
    "unsigned int": UnsignedIntegerType.instance(),
    "uint": UnsignedIntegerType.instance(),
    "u": UnsignedIntegerType.instance(),

    # nullable[unsigned]
    "nullable[unsigned]": UnsignedIntegerType.instance(nullable=True),
    "nullable[unsigned integer]": UnsignedIntegerType.instance(nullable=True),
    "nullable[unsigned int]": UnsignedIntegerType.instance(nullable=True),
    "nullable[uint]": UnsignedIntegerType.instance(nullable=True),
    "nullable[u]": UnsignedIntegerType.instance(nullable=True),

    # int8
    "int8": Int8Type.instance(),
    "i1": Int8Type.instance(),

    # nullable[int8]
    "nullable[int8]": Int8Type.instance(nullable=True),
    "nullable[i1]": Int8Type.instance(nullable=True),
    "Int8": Int8Type.instance(nullable=True),

    # int16
    "int16": Int16Type.instance(),
    "i2": Int16Type.instance(),

    # nullable[int16]
    "nullable[int16]": Int16Type.instance(nullable=True),
    "nullable[i2]": Int16Type.instance(nullable=True),
    "Int16": Int16Type.instance(nullable=True),

    # int32
    "int32": Int32Type.instance(),
    "i4": Int32Type.instance(),

    # nullable[int32]
    "nullable[int32]": Int32Type.instance(nullable=True),
    "nullable[i4]": Int32Type.instance(nullable=True),
    "Int32": Int32Type.instance(nullable=True),

    # int64
    "int64": Int64Type.instance(),
    "i8": Int64Type.instance(),

    # nullable[int64]
    "nullable[int64]": Int64Type.instance(nullable=True),
    "nullable[i8]": Int64Type.instance(nullable=True),
    "Int64": Int64Type.instance(nullable=True),

    # uint8
    "uint8": UInt8Type.instance(),
    "u1": UInt8Type.instance(),

    # nullable[uint8]
    "nullable[uint8]": UInt8Type.instance(nullable=True),
    "nullable[u1]": UInt8Type.instance(nullable=True),
    "UInt8": UInt8Type.instance(nullable=True),

    # uint16
    "uint16": UInt16Type.instance(),
    "u2": UInt16Type.instance(),

    # nullable[uint16]
    "nullable[uint16]": UInt16Type.instance(nullable=True),
    "nullable[u2]": UInt16Type.instance(nullable=True),
    "UInt16": UInt16Type.instance(nullable=True),

    # uint32
    "uint32": UInt32Type.instance(),
    "u4": UInt32Type.instance(),

    # nullable[uint32]
    "nullable[uint32]": UInt32Type.instance(nullable=True),
    "nullable[u4]": UInt32Type.instance(nullable=True),
    "UInt32": UInt32Type.instance(nullable=True),

    # uint64
    "uint64": UInt64Type.instance(),
    "u8": UInt64Type.instance(),

    # nullable[uint64]
    "nullable[uint64]": UInt64Type.instance(nullable=True),
    "nullable[u8]": UInt64Type.instance(nullable=True),
    "UInt64": UInt64Type.instance(nullable=True),

    # char = C char
    "char": dtype_element_types[np.dtype(np.byte)],
    "signed char": dtype_element_types[np.dtype(np.byte)],
    "byte": dtype_element_types[np.dtype(np.byte)],
    "b": dtype_element_types[np.dtype(np.byte)],

    # nullable[char] = nullable extensions to C char
    "nullable[char]": type(dtype_element_types[np.dtype(np.byte)]).instance(nullable=True),
    "nullable[signed char]": type(dtype_element_types[np.dtype(np.byte)]).instance(nullable=True),
    "nullable[byte]": type(dtype_element_types[np.dtype(np.byte)]).instance(nullable=True),
    "nullable[b]": type(dtype_element_types[np.dtype(np.byte)]).instance(nullable=True),

    # short = C short
    "short": dtype_element_types[np.dtype(np.short)],
    "short int": dtype_element_types[np.dtype(np.short)],
    "signed short": dtype_element_types[np.dtype(np.short)],
    "signed short int": dtype_element_types[np.dtype(np.short)],
    "h": dtype_element_types[np.dtype(np.short)],

    # nullable[short] = nullable extensions to C short
    "nullable[short]": type(dtype_element_types[np.dtype(np.short)]).instance(nullable=True),
    "nullable[short int]": type(dtype_element_types[np.dtype(np.short)]).instance(nullable=True),
    "nullable[signed short]": type(dtype_element_types[np.dtype(np.short)]).instance(nullable=True),
    "nullable[signed short int]": type(dtype_element_types[np.dtype(np.short)]).instance(nullable=True),
    "nullable[h]": type(dtype_element_types[np.dtype(np.short)]).instance(nullable=True),

    # intc = C int
    "intc": dtype_element_types[np.dtype(np.intc)],
    "cint": dtype_element_types[np.dtype(np.intc)],
    "signed intc": dtype_element_types[np.dtype(np.intc)],
    "signed cint": dtype_element_types[np.dtype(np.intc)],

    # nullable[intc] = nullable extensions to C int
    "nullable[intc]": type(dtype_element_types[np.dtype(np.intc)]).instance(nullable=True),
    "nullable[cint]": type(dtype_element_types[np.dtype(np.intc)]).instance(nullable=True),
    "nullable[signed intc]": type(dtype_element_types[np.dtype(np.intc)]).instance(nullable=True),
    "nullable[signed cint]": type(dtype_element_types[np.dtype(np.intc)]).instance(nullable=True),

    # long = C long
    "long": dtype_element_types[np.dtype(np.int_)],
    "long int": dtype_element_types[np.dtype(np.int_)],
    "signed long": dtype_element_types[np.dtype(np.int_)],
    "signed long int": dtype_element_types[np.dtype(np.int_)],
    "l": dtype_element_types[np.dtype(np.int_)],

    # nullable[long] = nullable extensions to C long
    "nullable[long]": type(dtype_element_types[np.dtype(np.int_)]).instance(nullable=True),
    "nullable[long int]": type(dtype_element_types[np.dtype(np.int_)]).instance(nullable=True),
    "nullable[signed long]": type(dtype_element_types[np.dtype(np.int_)]).instance(nullable=True),
    "nullable[signed long int]": type(dtype_element_types[np.dtype(np.int_)]).instance(nullable=True),
    "nullable[l]": type(dtype_element_types[np.dtype(np.int_)]).instance(nullable=True),

    # long long = C long long
    "long long": dtype_element_types[np.dtype(np.longlong)],
    "longlong": dtype_element_types[np.dtype(np.longlong)],
    "long long int": dtype_element_types[np.dtype(np.longlong)],
    "signed long long": dtype_element_types[np.dtype(np.longlong)],
    "signed longlong": dtype_element_types[np.dtype(np.longlong)],
    "signed long long int": dtype_element_types[np.dtype(np.longlong)],
    "q": dtype_element_types[np.dtype(np.longlong)],

    # nullable[long long] = nullable extensions to C long long
    "nullable[long long]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
    "nullable[longlong]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
    "nullable[long long int]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
    "nullable[signed long long]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
    "nullable[signed longlong]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
    "nullable[signed long long int]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
    "nullable[q]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),

    # ssize_t = C ssize_t
    "ssize_t": dtype_element_types[np.dtype(np.intp)],
    "intp": dtype_element_types[np.dtype(np.intp)],
    "int0": dtype_element_types[np.dtype(np.intp)],
    "p": dtype_element_types[np.dtype(np.intp)],

    # nullable[ssize_t] = nullable extensions to C ssize_t
    "nullable[ssize_t]": type(dtype_element_types[np.dtype(np.intp)]).instance(nullable=True),
    "nullable[intp]": type(dtype_element_types[np.dtype(np.intp)]).instance(nullable=True),
    "nullable[int0]": type(dtype_element_types[np.dtype(np.intp)]).instance(nullable=True),
    "nullable[p]": type(dtype_element_types[np.dtype(np.intp)]).instance(nullable=True),

    # unsigned char = C unsigned char
    "unsigned char": dtype_element_types[np.dtype(np.ubyte)],
    "unsigned byte": dtype_element_types[np.dtype(np.ubyte)],
    "ubyte": dtype_element_types[np.dtype(np.ubyte)],
    "B": dtype_element_types[np.dtype(np.ubyte)],

    # nullable[unsigned char] = nullable extensions to C unsigned char
    "nullable[unsigned char]": type(dtype_element_types[np.dtype(np.ubyte)]).instance(nullable=True),
    "nullable[unsigned byte]": type(dtype_element_types[np.dtype(np.ubyte)]).instance(nullable=True),
    "nullable[ubyte]": type(dtype_element_types[np.dtype(np.ubyte)]).instance(nullable=True),
    "nullable[B]": type(dtype_element_types[np.dtype(np.ubyte)]).instance(nullable=True),

    # unsigned short = C unsigned short
    "unsigned short": dtype_element_types[np.dtype(np.ushort)],
    "unsigned short int": dtype_element_types[np.dtype(np.ushort)],
    "ushort": dtype_element_types[np.dtype(np.ushort)],
    "H": dtype_element_types[np.dtype(np.ushort)],

    # nullable[unsigned short] = nullable extensions to C unsigned short
    "nullable[unsigned short]": type(dtype_element_types[np.dtype(np.ushort)]).instance(nullable=True),
    "nullable[unsigned short int]": type(dtype_element_types[np.dtype(np.ushort)]).instance(nullable=True),
    "nullable[ushort]": type(dtype_element_types[np.dtype(np.ushort)]).instance(nullable=True),
    "nullable[H]": type(dtype_element_types[np.dtype(np.ushort)]).instance(nullable=True),

    # unsigned intc = C unsigned int
    "unsigned intc": dtype_element_types[np.dtype(np.uintc)],
    "unsigned cint": dtype_element_types[np.dtype(np.uintc)],
    "uintc": dtype_element_types[np.dtype(np.uintc)],
    "ucint": dtype_element_types[np.dtype(np.uintc)],
    "I": dtype_element_types[np.dtype(np.uintc)],

    # nullable[unsigned intc] = nullable extensions to C unsigned int
    "nullable[unsigned intc]": type(dtype_element_types[np.dtype(np.uintc)]).instance(nullable=True),
    "nullable[unsigned cint]": type(dtype_element_types[np.dtype(np.uintc)]).instance(nullable=True),
    "nullable[uintc]": type(dtype_element_types[np.dtype(np.uintc)]).instance(nullable=True),
    "nullable[ucint]": type(dtype_element_types[np.dtype(np.uintc)]).instance(nullable=True),
    "nullable[I]": type(dtype_element_types[np.dtype(np.uintc)]).instance(nullable=True),

    # unsigned long = C unsigned long
    "unsigned long": dtype_element_types[np.dtype(np.uint)],
    "unsigned long int": dtype_element_types[np.dtype(np.uint)],
    "L": dtype_element_types[np.dtype(np.uint)],

    # nullable[unsigned long] = nullable extensions to C unsigned long
    "nullable[unsigned long]": type(dtype_element_types[np.dtype(np.uint)]).instance(nullable=True),
    "nullable[unsigned long int]": type(dtype_element_types[np.dtype(np.uint)]).instance(nullable=True),
    "nullable[L]": type(dtype_element_types[np.dtype(np.uint)]).instance(nullable=True),

    # unsigned long long = C unsigned long long
    "unsigned long long": dtype_element_types[np.dtype(np.ulonglong)],
    "unsigned longlong": dtype_element_types[np.dtype(np.ulonglong)],
    "unsigned long long int": dtype_element_types[np.dtype(np.ulonglong)],
    "Q": dtype_element_types[np.dtype(np.ulonglong)],

    # nullable[unsigned long long] = nullable extensions to C unsigned long long
    "nullable[unsigned long long]": type(dtype_element_types[np.dtype(np.ulonglong)]).instance(nullable=True),
    "nullable[unsigned longlong]": type(dtype_element_types[np.dtype(np.ulonglong)]).instance(nullable=True),
    "nullable[unsigned long long int]": type(dtype_element_types[np.dtype(np.ulonglong)]).instance(nullable=True),
    "nullable[Q]": type(dtype_element_types[np.dtype(np.ulonglong)]).instance(nullable=True),

    # size_t = C size_t
    "size_t": dtype_element_types[np.dtype(np.uintp)],
    "uintp": dtype_element_types[np.dtype(np.uintp)],
    "uint0": dtype_element_types[np.dtype(np.uintp)],
    "P": dtype_element_types[np.dtype(np.uintp)],

    # nullable[size_t] = nullable extensions to C size_t
    "nullable[size_t]": type(dtype_element_types[np.dtype(np.uintp)]).instance(nullable=True),
    "nullable[uintp]": type(dtype_element_types[np.dtype(np.uintp)]).instance(nullable=True),
    "nullable[uint0]": type(dtype_element_types[np.dtype(np.uintp)]).instance(nullable=True),
    "nullable[P]": type(dtype_element_types[np.dtype(np.uintp)]).instance(nullable=True),

    # float
    "float": FloatType.instance(),
    "floating": FloatType.instance(),
    "f": FloatType.instance(),

    # float16
    "float16": Float16Type.instance(),
    "f2": Float16Type.instance(),
    "half": Float16Type.instance(),
    "e": Float16Type.instance(),

    # float32
    "float32": Float32Type.instance(),
    "f4": Float32Type.instance(),
    "single": Float32Type.instance(),

    # float64
    "float64": Float64Type.instance(),
    "f8": Float64Type.instance(),
    "float_": Float64Type.instance(),
    "double": Float64Type.instance(),
    "d": Float64Type.instance(),

    # longdouble = C long double
    "longdouble": LongDoubleType.instance(),
    "longfloat": LongDoubleType.instance(),
    "long double": LongDoubleType.instance(),
    "long float": LongDoubleType.instance(),
    "float96": LongDoubleType.instance(),
    "float128": LongDoubleType.instance(),
    "f12": LongDoubleType.instance(),
    "f16": LongDoubleType.instance(),
    "g": LongDoubleType.instance(),

    # complex
    "complex": ComplexType.instance(),
    "complex floating": ComplexType.instance(),
    "complex float": ComplexType.instance(),
    "cfloat": ComplexType.instance(),
    "c": ComplexType.instance(),

    # complex64
    "complex64": Complex64Type.instance(),
    "c8": Complex64Type.instance(),
    "complex single": Complex64Type.instance(),
    "csingle": Complex64Type.instance(),
    "singlecomplex": Complex64Type.instance(),
    "F": Complex64Type.instance(),

    # complex128
    "complex128": Complex128Type.instance(),
    "c16": Complex128Type.instance(),
    "complex double": Complex128Type.instance(),
    "cdouble": Complex128Type.instance(),
    "complex_": Complex128Type.instance(),
    "D": Complex128Type.instance(),

    # clongdouble = (complex) C long double
    "clongdouble": CLongDoubleType.instance(),
    "clongfloat": CLongDoubleType.instance(),
    "complex longdouble": CLongDoubleType.instance(),
    "complex longfloat": CLongDoubleType.instance(),
    "complex long double": CLongDoubleType.instance(),
    "complex long float": CLongDoubleType.instance(),
    "complex192": CLongDoubleType.instance(),
    "complex256": CLongDoubleType.instance(),
    "c24": CLongDoubleType.instance(),
    "c32": CLongDoubleType.instance(),
    "G": CLongDoubleType.instance(),

    # decimal
    "decimal": DecimalType.instance(),

    # datetime
    "datetime": DatetimeType.instance(),

    # datetime[pandas]
    "datetime[pandas]": PandasTimestampType.instance(),
    "pandas.Timestamp": PandasTimestampType.instance(),
    "pandas Timestamp": PandasTimestampType.instance(),
    "pd.Timestamp": PandasTimestampType.instance(),

    # datetime[python]
    "datetime[python]": PyDatetimeType.instance(),
    "pydatetime": PyDatetimeType.instance(),
    "datetime.datetime": PyDatetimeType.instance(),

    # datetime[numpy]
    "datetime[numpy]": NumpyDatetime64Type.instance(),
    "numpy.datetime64": NumpyDatetime64Type.instance(),
    "numpy datetime64": NumpyDatetime64Type.instance(),
    "np.datetime64": NumpyDatetime64Type.instance(),
    "M8": NumpyDatetime64Type.instance(),
    # TODO: these go in M8 special case
    # "M8[ns]", "datetime64[5us]", "M8[50ms]",
    # "M8[2s]", "datetime64[30m]", "datetime64[h]", "M8[3D]",
    # "datetime64[2W]", "M8[3M]", "datetime64[10Y]"

    # timedelta
    "timedelta": TimedeltaType.instance(),

    # timedelta[pandas]
    "timedelta[pandas]": PandasTimedeltaType.instance(),
    "pandas.Timedelta": PandasTimedeltaType.instance(),
    "pandas Timedelta": PandasTimedeltaType.instance(),
    "pd.Timedelta": PandasTimedeltaType.instance(),

    # timedelta[python]
    "timedelta[python]": PyTimedeltaType.instance(),
    "pytimedelta": PyTimedeltaType.instance(),
    "datetime.timedelta": PyTimedeltaType.instance(),

    # timedelta[numpy]
    "timedelta[numpy]": NumpyTimedelta64Type.instance(),
    "numpy.timedelta64": NumpyTimedelta64Type.instance(),
    "numpy timedelta64": NumpyTimedelta64Type.instance(),
    "np.timedelta64": NumpyTimedelta64Type.instance(),
    "m8": NumpyTimedelta64Type.instance(),
    # TODO: these go in m8 special case
    # "m8[ns]", "timedelta64[5us]",
    # "m8[50ms]", "m8[2s]", "timedelta64[30m]", "timedelta64[h]",
    # "m8[3D]", "timedelta64[2W]", "m8[3M]", "timedelta64[10Y]"

    # string
    "str": StringType.instance(),
    "string": StringType.instance(),
    "unicode": StringType.instance(),
    "U": StringType.instance(),
    "str0": StringType.instance(),
    "str_": StringType.instance(),
    "unicode_": StringType.instance(),

    # string[python]
    "str[python]": StringType.instance(storage="python"),
    "string[python]": StringType.instance(storage="python"),
    "unicode[python]": StringType.instance(storage="python"),
    "pystr": StringType.instance(storage="python"),
    "pystring": StringType.instance(storage="python"),
    "python string": StringType.instance(storage="python"),

    # object
    "object": ObjectType.instance(),
    "obj": ObjectType.instance(),
    "O": ObjectType.instance(),
    "pyobject": ObjectType.instance(),
    "object_": ObjectType.instance(),
    "object0": ObjectType.instance(),
}


if PYARROW_INSTALLED:  # requires PYARROW dependency
    dtype_element_types[pd.StringDtype("pyarrow")] = StringType.instance(storage="pyarrow")
    string_element_types["str[pyarrow]"] = StringType.instance(storage="pyarrow")
    string_element_types["unicode[pyarrow]"] = StringType.instance(storage="pyarrow")
    string_element_types["pyarrow str"] = StringType.instance(storage="pyarrow")
    string_element_types["pyarrow string"] = StringType.instance(storage="pyarrow")


# MappingProxyType is immutable - prevents test-related side effects
dtype_element_types = MappingProxyType(dtype_element_types)
atomic_element_types = MappingProxyType(atomic_element_types)
string_element_types = MappingProxyType(string_element_types)


# def flyweight_data():




#####################
####    TESTS    ####
#####################


# TODO: returns flyweights
# TODO: accepts numpy/pandas dtype objects
# TODO: accepts atomic types
# TODO: accepts string type specifiers
# TODO: accepts other ElementType objects


# def test_resolve_dtype_returns_flyweights(case):
#     call resolve_dtype() twice on same input and assert ids are equal


@parametrize(*[TypeCase({}, k, v) for k, v in dtype_element_types.items()])
def test_resolve_dtype_accepts_numpy_and_pandas_dtype_objects(case: TypeCase):
    # TODO: NumpyDatetime64Type.instance() does not return the same flyweight
    # as resolve_dtype()

    result = resolve_dtype(case.input, **case.kwargs)
    assert result is case.output, (
        f"resolve_dtype({', '.join([repr(case.input), case.signature()])}) "
        f"failed with input:\n"
        f"{repr(case.input)}\n"
        f"expected:\n"
        f"{repr(case.output)}\n"
        f"received:\n"
        f"{repr(result)}\n"
    )

