"""This module contains all the prepackaged integer types for the ``pdcast``
type system.
"""
import numpy as np
import pandas as pd

from .base import REGISTRY, Type


class Int(Type):
    """Abstract integer type."""

    aliases = {"int", "integer"}


######################
####    SIGNED    ####
######################


@Int.default
class Signed(Int):
    """Abstract signed integer type."""

    aliases = {"signed", "signed int", "signed integer", "i"}


class PythonInt(Signed, backend="python"):
    """Python signed integer type."""

    aliases = {int}
    scalar = int
    dtype = np.dtype(object)  # TODO: synthesize dtype
    max = np.inf
    min = -np.inf
    is_nullable = True
    missing = None


@Signed.default
class Int64(Signed):
    """Abstract 64-bit integer type."""

    aliases = {"int64", "i8"}
    max = 2**63 - 1
    min = -2**63


@Int64.default
class NumpyInt64(Int64, backend="numpy"):
    """Numpy 64-bit integer type."""

    aliases = {np.int64, np.dtype(np.int64)}
    dtype = np.dtype(np.int64)
    is_nullable = False


@NumpyInt64.nullable
class PandasInt64(Int64, backend="pandas"):
    """Pandas 64-bit integer type."""

    aliases = {pd.Int64Dtype}
    dtype = pd.Int64Dtype()
    is_nullable = True


class Int32(Signed):
    """Abstract 32-bit integer type."""

    aliases = {"int32", "i4"}
    max = 2**31 - 1
    min = -2**31


@Int32.default
class NumpyInt32(Int32, backend="numpy"):
    """Numpy 32-bit integer type."""

    aliases = {np.int32, np.dtype(np.int32)}
    dtype = np.dtype(np.int32)
    is_nullable = False


@NumpyInt32.nullable
class PandasInt32(Int32, backend="pandas"):
    """Pandas 32-bit integer type."""

    aliases = {pd.Int32Dtype}
    dtype = pd.Int32Dtype()
    is_nullable = True


class Int16(Signed):
    """Abstract 16-bit integer type."""

    aliases = {"int16", "i2"}
    max = 2**15 - 1
    min = -2**15


@Int16.default
class NumpyInt16(Int16, backend="numpy"):
    """Numpy 16-bit integer type."""

    aliases = {np.int16, np.dtype(np.int16)}
    dtype = np.dtype(np.int16)
    is_nullable = False


@NumpyInt16.nullable
class PandasInt16(Int16, backend="pandas"):
    """Pandas 16-bit integer type."""

    aliases = {pd.Int16Dtype}
    dtype = pd.Int16Dtype()
    is_nullable = True


class Int8(Signed):
    """Abstract 8-bit integer type."""

    aliases = {"int8", "i1"}
    max = 2**7 - 1
    min = -2**7


@Int8.default
class NumpyInt8(Int8, backend="numpy"):
    """Numpy 8-bit integer type."""

    aliases = {np.int8, np.dtype(np.int8)}
    dtype = np.dtype(np.int8)
    is_nullable = False



@NumpyInt8.nullable
class PandasInt8(Int8, backend="pandas"):
    """Pandas 8-bit integer type."""

    aliases = {pd.Int8Dtype}
    dtype = pd.Int8Dtype()
    is_nullable = True


########################
####    UNSIGNED    ####
########################


class Unsigned(Int):
    """Abstract unsigned integer type."""

    aliases = {"unsigned", "unsigned int", "unsigned integer", "u"}


@Unsigned.default
class UInt64(Unsigned):
    """Abstract 64-bit unsigned integer type."""

    aliases = {"uint64", "unsigned int64", "u8"}
    max = 2**64 - 1
    min = 0


@UInt64.default
class NumpyUInt64(UInt64, backend="numpy"):
    """Numpy 64-bit unsigned integer type."""

    aliases = {np.uint64, np.dtype(np.uint64)}
    dtype = np.dtype(np.uint64)
    is_nullable = False


@NumpyUInt64.nullable
class PandasUInt64(UInt64, backend="pandas"):
    """Pandas 64-bit unsigned integer type."""

    aliases = {pd.UInt64Dtype}
    dtype = pd.UInt64Dtype()
    is_nullable = True


class UInt32(Unsigned):
    """Abstract 32-bit unsigned integer type."""

    aliases = {"uint32", "unsigned int32", "u4"}
    max = 2**32 - 1
    min = 0


@UInt32.default
class NumpyUInt32(UInt32, backend="numpy"):
    """Numpy 32-bit unsigned integer type."""

    aliases = {np.uint32, np.dtype(np.uint32)}
    dtype = np.dtype(np.uint32)
    is_nullable = False


@NumpyUInt32.nullable
class PandasUInt32(UInt32, backend="pandas"):
    """Pandas 32-bit unsigned integer type."""

    aliases = {pd.UInt32Dtype}
    dtype = pd.UInt32Dtype()
    is_nullable = True


class UInt16(Unsigned):
    """Abstract 16-bit unsigned integer type."""

    aliases = {"uint16", "unsigned int16", "u2"}
    max = 2**16 - 1
    min = 0


@UInt16.default
class NumpyUInt16(UInt16, backend="numpy"):
    """Numpy 16-bit unsigned integer type."""

    aliases = {np.uint16, np.dtype(np.uint16)}
    scalar = np.uint16
    dtype = np.dtype(np.uint16)
    is_nullable = False


@NumpyUInt16.nullable
class PandasUInt16(UInt16, backend="pandas"):
    """Pandas 16-bit unsigned integer type."""

    aliases = {pd.UInt16Dtype}
    dtype = pd.UInt16Dtype()
    is_nullable = True


class UInt8(Unsigned):
    """Abstract 8-bit unsigned integer type."""

    aliases = {"uint8", "unsigned int8", "u1"}
    max = 2**8 - 1
    min = 0


@UInt8.default
class NumpyUInt8(UInt8, backend="numpy"):
    """Numpy 8-bit unsigned integer type."""

    aliases = {np.uint8, np.dtype(np.uint8)}
    dtype = np.dtype(np.uint8)
    is_nullable = False


@NumpyUInt8.nullable
class PandasUInt8(UInt8, backend="pandas"):
    """Pandas 8-bit unsigned integer type."""

    aliases = {pd.UInt8Dtype}
    dtype = pd.UInt8Dtype()
    is_nullable = True


#######################
####    PRIVATE    ####
#######################


# NOTE: some aliases are platform-specific and may be assigned to different
# integer types based on hardware configuration.  Luckily, numpy's dtype()
# factory automatically resolves these, so we can just piggyback off it.


platform_specific_aliases: dict[str, str] = {
    # C char
    "char": str(np.dtype(np.byte)),
    "signed char": "char",
    "byte": "char",
    "b": "char",

    # C short
    "short": str(np.dtype(np.short)),
    "short int": "short",
    "short integer": "short",
    "signed short": "short",
    "signed short int": "short",
    "signed short integer": "short",
    "h": "short",

    # C int
    "intc": str(np.dtype(np.intc)),
    "signed intc": "intc",

    # C long
    "long": str(np.dtype(np.int_)),
    "long int": "long",
    "long integer": "long",
    "signed long": "long",
    "signed long int": "long",
    "signed long integer": "long",
    "l": "long",

    # C long long
    "long long": str(np.dtype(np.longlong)),
    "long long int": "long long",
    "long long integer": "long long",
    "signed long long": "long long",
    "signed long long int": "long long",
    "signed long long integer": "long long",
    "longlong": "long long",
    "signed longlong": "long long",
    "q": "long long",

    # C ssize_t
    "ssize_t": str(np.dtype(np.intp)),
    "intp": "ssize_t",
    "int0": "ssize_t",
    "p": "ssize_t",

    # C unsigned char
    "unsigned char": str(np.dtype(np.ubyte)),
    "unsigned byte": "unsigned char",
    "ubyte": "unsigned char",
    "B": "unsigned char",

    # C unsigned short
    "unsigned short": str(np.dtype(np.ushort)),
    "unsigned short int": "unsigned short",
    "unsigned short integer": "unsigned short",
    "ushort": "unsigned short",
    "H": "unsigned short",

    # C unsigned int
    "unsigned intc": str(np.dtype(np.uintc)),
    "uintc": "unsigned intc",
    "I": "unsigned intc",

    # C unsigned long
    "unsigned long": str(np.dtype(np.uint)),
    "unsigned long int": "unsigned long",
    "unsigned long integer": "unsigned long",
    "ulong": "unsigned long",
    "L": "unsigned long",

    # C unsigned long long
    "unsigned long long": str(np.dtype(np.ulonglong)),
    "unsigned long long int": "unsigned long long",
    "unsigned long long integer": "unsigned long long",
    "ulonglong": "unsigned long long",
    "unsigned longlong": "unsigned long long",
    "Q": "unsigned long long",

    # C size_t
    "size_t": str(np.dtype(np.uintp)),
    "uintp": "size_t",
    "uint0": "size_t",
    "P": "size_t",
}


for alias, lookup in platform_specific_aliases.items():
    REGISTRY.strings[lookup].aliases.add(alias)


REGISTRY.refresh_regex()
