"""This module contains all the prepackaged integer types for the ``pdcast``
type system.
"""
import sys

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType, CompositeType
from .base import generic, register


# TODO: register_alias no longer exists.  Call Type.aliases.add() instead.


######################
####    MIXINS    ####
######################


class IntegerMixin:

    is_numeric = True

    @property
    def larger(self) -> list:
        """Get a list of types that this type can be upcasted to."""
        # get all subtypes with range wider than self
        result = []
        for back in self.backends.values():
            for x in back.subtypes:
                if x.min < self.min or x.max > self.max:
                    result.append(x)

        # collapse types that are not unique
        result = [
            x for x in result if not any(x != y and x in y for y in result)
        ]

        return sorted(result, key=lambda x: x.max - x.min)

    @property
    def smaller(self) -> list:
        """Get a list of types that this type can be downcasted to."""
        is_signed = lambda x: not x.is_subtype(UnsignedIntegerType)
        result = [
            x for x in self.root.subtypes if (
                (x.itemsize or np.inf) < (self.itemsize or np.inf) and
                is_signed(x) == is_signed(self)
            )
        ]
        return sorted(result, key=lambda x: x.itemsize)


class NumpyIntegerMixin:
    """A mixin class that allows numpy integers to automatically switch to
    their pandas equivalents when missing values are detected.
    """

    is_nullable = False

    def make_nullable(self) -> AtomicType:
        return self.generic.instance(backend="pandas", **self.kwargs)


#######################
####    GENERIC    ####
#######################


@register
@generic
class IntegerType(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Generic integer supertype."""

    name = "int"
    aliases = {"int", "integer"}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = int
    max = 2**63 - 1
    min = -2**63


@register
@generic
@IntegerType.subtype
class SignedIntegerType(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Generic signed integer supertype."""

    name = "signed"
    aliases = {"signed", "signed int", "signed integer", "i"}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = int
    max = 2**63 - 1
    min = -2**63


@register
@generic
@IntegerType.subtype
class UnsignedIntegerType(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Generic 8-bit unsigned integer type."""

    name = "unsigned"
    aliases = {"unsigned", "unsigned int", "unsigned integer", "uint", "u"}
    dtype = np.dtype(np.uint64)
    itemsize = 8
    type_def = np.uint64
    max = 2**64 - 1
    min = 0


@register
@generic
@SignedIntegerType.subtype
class Int8Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Generic 8-bit signed integer type."""

    name = "int8"
    aliases = {"int8", "i1"}
    dtype = np.dtype(np.int8)
    itemsize = 1
    type_def = np.uint8
    max = 2**7 - 1
    min = -2**7


@register
@generic
@SignedIntegerType.subtype
class Int16Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Generic 16-bit signed integer type."""

    name = "int16"
    aliases = {"int16", "i2"}
    dtype = np.dtype(np.int16)
    itemsize = 2
    type_def = np.uint16
    max = 2**15 - 1
    min = -2**15


@register
@generic
@SignedIntegerType.subtype
class Int32Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Generic 32-bit signed integer type."""

    name = "int32"
    aliases = {"int32", "i4"}
    dtype = np.dtype(np.int32)
    itemsize = 4
    type_def = np.uint32
    max = 2**31 - 1
    min = -2**31


@register
@generic
@SignedIntegerType.subtype
class Int64Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Generic 64-bit signed integer type."""

    name = "int64"
    aliases = {"int64", "i8"}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = np.uint64
    max = 2**63 - 1
    min = -2**63


@register
@generic
@UnsignedIntegerType.subtype
class UInt8Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Generic 8-bit unsigned integer type."""

    name = "uint8"
    aliases = {"uint8", "unsigned int8", "u1"}
    dtype = np.dtype(np.uint8)
    itemsize = 1
    type_def = np.uint8
    max = 2**8 - 1
    min = 0


@register
@generic
@UnsignedIntegerType.subtype
class UInt16Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Generic 16-bit unsigned integer type."""

    name = "uint16"
    aliases = {"uint16", "unsiged int16", "u2"}
    dtype = np.dtype(np.uint16)
    itemsize = 2
    type_def = np.uint16
    max = 2**16 - 1
    min = 0


@register
@generic
@UnsignedIntegerType.subtype
class UInt32Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Generic 32-bit unsigned integer type."""

    name = "uint32"
    aliases = {"uint32", "unsigned int32", "u4"}
    dtype = np.dtype(np.uint32)
    itemsize = 4
    type_def = np.uint32
    max = 2**32 - 1
    min = 0


@register
@generic
@UnsignedIntegerType.subtype
class UInt64Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Generic 64-bit unsigned integer type."""

    name = "uint64"
    aliases = {"uint64", "unsigned int64", "u8"}
    dtype = np.dtype(np.uint64)
    itemsize = 8
    type_def = np.uint64
    max = 2**64 - 1
    min = 0


#####################
####    NUMPY    ####
#####################


@register
@IntegerType.implementation("numpy")
@generic
class NumpyIntegerType(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Numpy integer type."""

    aliases = {np.integer}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@register
@NumpyIntegerType.subtype
@SignedIntegerType.implementation("numpy")
@generic
class NumpySignedIntegerType(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Numpy signed integer type."""

    aliases = {np.signedinteger}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@register
@NumpyIntegerType.subtype
@UnsignedIntegerType.implementation("numpy")
@generic
class NumpyUnsignedIntegerType(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """Numpy unsigned integer type."""

    aliases = {np.unsignedinteger}
    dtype = np.dtype(np.uint64)
    itemsize = 8
    type_def = np.uint64
    max = 2**64 - 1
    min = 0


@register
@NumpySignedIntegerType.subtype
@Int8Type.implementation("numpy")
class NumpyInt8Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """8-bit numpy integer subtype."""

    aliases = {np.int8, np.dtype(np.int8)}
    dtype = np.dtype(np.int8)
    itemsize = 1
    type_def = np.int8
    max = 2**7 - 1
    min = -2**7


@register
@NumpySignedIntegerType.subtype
@Int16Type.implementation("numpy")
class NumpyInt16Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """16-bit numpy integer subtype."""

    aliases = {np.int16, np.dtype(np.int16)}
    dtype = np.dtype(np.int16)
    itemsize = 2
    type_def = np.int16
    max = 2**15 - 1
    min = -2**15


@register
@NumpySignedIntegerType.subtype
@Int32Type.implementation("numpy")
class NumpyInt32Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """32-bit numpy integer subtype."""

    aliases = {np.int32, np.dtype(np.int32)}
    dtype = np.dtype(np.int32)
    itemsize = 4
    type_def = np.int32
    max = 2**31 - 1
    min = -2**31


@register
@NumpySignedIntegerType.subtype
@Int64Type.implementation("numpy")
class NumpyInt64Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """64-bit numpy integer subtype."""

    aliases = {np.int64, np.dtype(np.int64)}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@register
@NumpyUnsignedIntegerType.subtype
@UInt8Type.implementation("numpy")
class NumpyUInt8Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """8-bit numpy unsigned integer subtype."""

    aliases = {np.uint8, np.dtype(np.uint8)}
    dtype = np.dtype(np.uint8)
    itemsize = 1
    type_def = np.uint8
    max = 2**8 - 1
    min = 0


@register
@NumpyUnsignedIntegerType.subtype
@UInt16Type.implementation("numpy")
class NumpyUInt16Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """16-bit numpy unsigned integer subtype."""

    aliases = {np.uint16, np.dtype(np.uint16)}
    dtype = np.dtype(np.uint16)
    itemsize = 2
    type_def = np.uint16
    max = 2**16 - 1
    min = 0


@register
@NumpyUnsignedIntegerType.subtype
@UInt32Type.implementation("numpy")
class NumpyUInt32Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """32-bit numpy unsigned integer subtype."""

    aliases = {np.uint32, np.dtype(np.uint32)}
    dtype = np.dtype(np.uint32)
    itemsize = 4
    type_def = np.uint32
    max = 2**32 - 1
    min = 0


@register
@NumpyUnsignedIntegerType.subtype
@UInt64Type.implementation("numpy")
class NumpyUInt64Type(IntegerMixin, NumpyIntegerMixin, AtomicType):
    """64-bit numpy unsigned integer subtype."""

    aliases = {np.uint64, np.dtype(np.uint64)}
    dtype = np.dtype(np.uint64)
    itemsize = 8
    type_def = np.uint64
    max = 2**64 - 1
    min = 0


######################
####    PANDAS    ####
######################


@register
@IntegerType.implementation("pandas")
@generic
class PandasIntegerType(IntegerMixin, AtomicType):
    """Pandas integer supertype."""

    aliases = set()
    dtype = pd.Int64Dtype()
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@register
@PandasIntegerType.subtype
@SignedIntegerType.implementation("pandas")
@generic
class PandasSignedIntegerType(IntegerMixin, AtomicType):
    """Python signed integer supertype."""

    aliases = set()
    dtype = pd.Int64Dtype()
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@register
@PandasIntegerType.subtype
@UnsignedIntegerType.implementation("pandas")
@generic
class PandasUnsignedIntegerType(IntegerMixin, AtomicType):
    """Numpy unsigned integer type."""

    aliases = set()
    dtype = pd.UInt64Dtype()
    itemsize = 8
    type_def = np.uint64
    max = 2**64 - 1
    min = 0


@register
@PandasSignedIntegerType.subtype
@Int8Type.implementation("pandas")
class PandasInt8Type(IntegerMixin, AtomicType):
    """8-bit numpy integer subtype."""

    aliases = {pd.Int8Dtype, "Int8"}
    dtype = pd.Int8Dtype()
    itemsize = 1
    type_def = np.int8
    max = 2**7 - 1
    min = -2**7


@register
@PandasSignedIntegerType.subtype
@Int16Type.implementation("pandas")
class PandasInt16Type(IntegerMixin, AtomicType):
    """16-bit numpy integer subtype."""

    aliases = {pd.Int16Dtype, "Int16"}
    dtype = pd.Int16Dtype()
    itemsize = 2
    type_def = np.int16
    max = 2**15 - 1
    min = -2**15


@register
@PandasSignedIntegerType.subtype
@Int32Type.implementation("pandas")
class PandasInt32Type(IntegerMixin, AtomicType):
    """32-bit numpy integer subtype."""

    aliases = {pd.Int32Dtype, "Int32"}
    dtype = pd.Int32Dtype()
    itemsize = 4
    type_def = np.int32
    max = 2**31 - 1
    min = -2**31


@register
@PandasSignedIntegerType.subtype
@Int64Type.implementation("pandas")
class PandasInt64Type(IntegerMixin, AtomicType):
    """64-bit numpy integer subtype."""

    aliases = {pd.Int64Dtype, "Int64"}
    dtype = pd.Int64Dtype()
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@register
@PandasUnsignedIntegerType.subtype
@UInt8Type.implementation("pandas")
class PandasUInt8Type(IntegerMixin, AtomicType):
    """8-bit numpy integer subtype."""

    aliases = {pd.UInt8Dtype, "UInt8"}
    dtype = pd.UInt8Dtype()
    itemsize = 1
    type_def = np.uint8
    max = 2**8 - 1
    min = 0


@register
@PandasUnsignedIntegerType.subtype
@UInt16Type.implementation("pandas")
class PandasUInt16Type(IntegerMixin, AtomicType):
    """16-bit numpy integer subtype."""

    aliases = {pd.UInt16Dtype, "UInt16"}
    dtype = pd.UInt16Dtype()
    itemsize = 2
    type_def = np.uint16
    max = 2**16 - 1
    min = 0


@register
@PandasUnsignedIntegerType.subtype
@UInt32Type.implementation("pandas")
class PandasUInt32Type(IntegerMixin, AtomicType):
    """32-bit numpy integer subtype."""

    aliases = {pd.UInt32Dtype, "UInt32"}
    dtype = pd.UInt32Dtype()
    itemsize = 4
    type_def = np.uint32
    max = 2**32 - 1
    min = 0


@register
@PandasUnsignedIntegerType.subtype
@UInt64Type.implementation("pandas")
class PandasUInt64Type(IntegerMixin, AtomicType):
    """64-bit numpy integer subtype."""

    aliases = {pd.UInt64Dtype, "UInt64"}
    dtype = pd.UInt64Dtype()
    itemsize = 8
    type_def = np.uint64
    max = 2**64 - 1
    min = 0


######################
####    PYTHON    ####
######################


@register
@IntegerType.implementation("python")
@SignedIntegerType.implementation("python")
class PythonIntegerType(IntegerMixin, AtomicType):
    """Python integer supertype."""

    aliases = {int}
    type_def = int
    max = np.inf
    min = -np.inf


#######################
####    PRIVATE    ####
#######################


# these aliases are platform-specific and may be assigned to different integer
# types based on hardware configuration.  Luckily, numpy's dtype() factory
# automatically resolves these, so we can just piggyback off it.
cdef dict platform_specific_aliases = {
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
    AtomicType.registry.aliases[lookup].register_alias(alias)
