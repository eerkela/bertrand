"""This module contains all the prepackaged integer types for the ``pdcast``
type system.
"""
import sys

import numpy as np
import pandas as pd

from .base cimport ScalarType, AbstractType, CompositeType
from .base import register


############################
####    ROOT INTEGER    ####
############################


@register
class IntegerType(AbstractType):
    """Generic integer supertype."""

    name = "int"
    aliases = {"int", "integer"}


@register
@IntegerType.implementation("numpy")
class NumpyIntegerType(AbstractType):
    """Numpy integer type."""

    aliases = {np.integer}


@register
@IntegerType.implementation("pandas")
class PandasIntegerType(AbstractType):
    """Pandas integer supertype."""

    aliases = set()


######################
####    SIGNED    ####
######################


@register
@IntegerType.default
@IntegerType.subtype
class SignedIntegerType(AbstractType):
    """Generic signed integer supertype."""

    name = "signed"
    aliases = {"signed", "signed int", "signed integer", "i"}


@register
@NumpyIntegerType.default
@NumpyIntegerType.subtype
@SignedIntegerType.implementation("numpy")
class NumpySignedIntegerType(AbstractType):
    """Numpy signed integer type."""

    aliases = {np.signedinteger}


@register
@PandasIntegerType.default
@PandasIntegerType.subtype
@SignedIntegerType.implementation("pandas")
class PandasSignedIntegerType(AbstractType):
    """Python signed integer supertype."""

    aliases = set()


@register
@SignedIntegerType.implementation("python")
@IntegerType.implementation("python")
class PythonIntegerType(ScalarType):
    """Python integer supertype."""

    aliases = {int}
    type_def = int
    is_numeric = True
    max = np.inf
    min = -np.inf


########################
####    UNSIGNED    ####
########################


@register
@IntegerType.subtype
class UnsignedIntegerType(AbstractType):
    """Generic 8-bit unsigned integer type."""

    name = "unsigned"
    aliases = {"unsigned", "unsigned int", "unsigned integer", "uint", "u"}


@register
@NumpyIntegerType.subtype
@UnsignedIntegerType.implementation("numpy")
class NumpyUnsignedIntegerType(AbstractType):
    """Numpy unsigned integer type."""

    aliases = {np.unsignedinteger}


@register
@PandasIntegerType.subtype
@UnsignedIntegerType.implementation("pandas")
class PandasUnsignedIntegerType(AbstractType):
    """Numpy unsigned integer type."""

    aliases = set()


####################
####    INT8    ####
####################


@register
@SignedIntegerType.subtype
class Int8Type(AbstractType):
    """Generic 8-bit signed integer type."""

    name = "int8"
    aliases = {"int8", "i1"}


@register
@NumpySignedIntegerType.subtype
@Int8Type.default
@Int8Type.implementation("numpy")
class NumpyInt8Type(ScalarType):
    """8-bit numpy integer subtype."""

    aliases = {np.int8, np.dtype(np.int8)}
    dtype = np.dtype(np.int8)
    itemsize = 1
    type_def = np.int8
    is_numeric = True
    max = 2**7 - 1
    min = -2**7
    is_nullable = False

    def make_nullable(self) -> ScalarType:
        """Convert this type to a nullable equivalent."""
        return self.registry[PandasInt8Type]


@register
@PandasSignedIntegerType.subtype
@Int8Type.implementation("pandas")
class PandasInt8Type(ScalarType):
    """8-bit numpy integer subtype."""

    aliases = {pd.Int8Dtype, "Int8"}
    dtype = pd.Int8Dtype()
    itemsize = 1
    type_def = np.int8
    is_numeric = True
    max = 2**7 - 1
    min = -2**7


#####################
####    INT16    ####
#####################


@register
@SignedIntegerType.subtype
class Int16Type(AbstractType):
    """Generic 16-bit signed integer type."""

    name = "int16"
    aliases = {"int16", "i2"}


@register
@NumpySignedIntegerType.subtype
@Int16Type.default
@Int16Type.implementation("numpy")
class NumpyInt16Type(ScalarType):
    """16-bit numpy integer subtype."""

    aliases = {np.int16, np.dtype(np.int16)}
    dtype = np.dtype(np.int16)
    itemsize = 2
    type_def = np.int16
    is_numeric = True
    max = 2**15 - 1
    min = -2**15
    is_nullable = False

    def make_nullable(self) -> ScalarType:
        """Convert this type to a nullable equivalent."""
        return self.registry[PandasInt16Type]


@register
@PandasSignedIntegerType.subtype
@Int16Type.implementation("pandas")
class PandasInt16Type(ScalarType):
    """16-bit numpy integer subtype."""

    aliases = {pd.Int16Dtype, "Int16"}
    dtype = pd.Int16Dtype()
    itemsize = 2
    type_def = np.int16
    is_numeric = True
    max = 2**15 - 1
    min = -2**15


#####################
####    INT32    ####
#####################


@register
@SignedIntegerType.subtype
class Int32Type(AbstractType):
    """Generic 32-bit signed integer type."""

    name = "int32"
    aliases = {"int32", "i4"}


@register
@NumpySignedIntegerType.subtype
@Int32Type.default
@Int32Type.implementation("numpy")
class NumpyInt32Type(ScalarType):
    """32-bit numpy integer."""

    aliases = {np.int32, np.dtype(np.int32)}
    dtype = np.dtype(np.int32)
    itemsize = 4
    type_def = np.int32
    is_numeric = True
    max = 2**31 - 1
    min = -2**31
    is_nullable = False

    def make_nullable(self) -> ScalarType:
        """Convert this type to a nullable equivalent."""
        return self.registry[PandasInt32Type]


@register
@PandasSignedIntegerType.subtype
@Int32Type.implementation("pandas")
class PandasInt32Type(ScalarType):
    """32-bit pandas integer."""

    aliases = {pd.Int32Dtype, "Int32"}
    dtype = pd.Int32Dtype()
    itemsize = 4
    type_def = np.int32
    is_numeric = True
    max = 2**31 - 1
    min = -2**31


#####################
####    INT64    ####
#####################


@register
@SignedIntegerType.default
@SignedIntegerType.subtype
class Int64Type(AbstractType):
    """Generic 64-bit signed integer type."""

    name = "int64"
    aliases = {"int64", "i8"}


@register
@NumpySignedIntegerType.default
@NumpySignedIntegerType.subtype
@Int64Type.default
@Int64Type.implementation("numpy")
class NumpyInt64Type(ScalarType):
    """64-bit numpy integer subtype."""

    aliases = {np.int64, np.dtype(np.int64)}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = np.int64
    is_numeric = True
    max = 2**63 - 1
    min = -2**63
    is_nullable = False

    def make_nullable(self) -> ScalarType:
        """Convert this type to a nullable equivalent."""
        return self.registry[PandasInt64Type]


@register
@PandasSignedIntegerType.default
@PandasSignedIntegerType.subtype
@Int64Type.implementation("pandas")
class PandasInt64Type(ScalarType):
    """64-bit numpy integer subtype."""

    aliases = {pd.Int64Dtype, "Int64"}
    dtype = pd.Int64Dtype()
    itemsize = 8
    type_def = np.int64
    is_numeric = True
    max = 2**63 - 1
    min = -2**63


#####################
####    UINT8    ####
#####################


@register
@UnsignedIntegerType.subtype
class UInt8Type(AbstractType):
    """Generic 8-bit unsigned integer type."""

    name = "uint8"
    aliases = {"uint8", "unsigned int8", "u1"}


@register
@NumpyUnsignedIntegerType.subtype
@UInt8Type.default
@UInt8Type.implementation("numpy")
class NumpyUInt8Type(ScalarType):
    """8-bit numpy unsigned integer subtype."""

    aliases = {np.uint8, np.dtype(np.uint8)}
    dtype = np.dtype(np.uint8)
    itemsize = 1
    type_def = np.uint8
    is_numeric = True
    max = 2**8 - 1
    min = 0
    is_nullable = False

    def make_nullable(self) -> ScalarType:
        """Convert this type to a nullable equivalent."""
        return self.registry[PandasUInt8Type]


@register
@PandasUnsignedIntegerType.subtype
@UInt8Type.implementation("pandas")
class PandasUInt8Type(ScalarType):
    """8-bit numpy integer subtype."""

    aliases = {pd.UInt8Dtype, "UInt8"}
    dtype = pd.UInt8Dtype()
    itemsize = 1
    type_def = np.uint8
    is_numeric = True
    max = 2**8 - 1
    min = 0


######################
####    UINT16    ####
######################


@register
@UnsignedIntegerType.subtype
class UInt16Type(AbstractType):
    """Generic 16-bit unsigned integer type."""

    name = "uint16"
    aliases = {"uint16", "unsiged int16", "u2"}


@register
@NumpyUnsignedIntegerType.subtype
@UInt16Type.default
@UInt16Type.implementation("numpy")
class NumpyUInt16Type(ScalarType):
    """16-bit numpy unsigned integer subtype."""

    aliases = {np.uint16, np.dtype(np.uint16)}
    dtype = np.dtype(np.uint16)
    itemsize = 2
    type_def = np.uint16
    is_numeric = True
    max = 2**16 - 1
    min = 0
    is_nullable = False

    def make_nullable(self) -> ScalarType:
        """Convert this type to a nullable equivalent."""
        return self.registry[PandasUInt16Type]

@register
@PandasUnsignedIntegerType.subtype
@UInt16Type.implementation("pandas")
class PandasUInt16Type(ScalarType):
    """16-bit numpy integer subtype."""

    aliases = {pd.UInt16Dtype, "UInt16"}
    dtype = pd.UInt16Dtype()
    itemsize = 2
    type_def = np.uint16
    is_numeric = True
    max = 2**16 - 1
    min = 0


######################
####    UINT32    ####
######################


@register
@UnsignedIntegerType.subtype
class UInt32Type(AbstractType):
    """Generic 32-bit unsigned integer type."""

    name = "uint32"
    aliases = {"uint32", "unsigned int32", "u4"}


@register
@NumpyUnsignedIntegerType.subtype
@UInt32Type.default
@UInt32Type.implementation("numpy")
class NumpyUInt32Type(ScalarType):
    """32-bit numpy unsigned integer subtype."""

    aliases = {np.uint32, np.dtype(np.uint32)}
    dtype = np.dtype(np.uint32)
    itemsize = 4
    type_def = np.uint32
    is_numeric = True
    max = 2**32 - 1
    min = 0
    is_nullable = False

    def make_nullable(self) -> ScalarType:
        """Convert this type to a nullable equivalent."""
        return self.registry[PandasUInt32Type]

@register
@PandasUnsignedIntegerType.subtype
@UInt32Type.implementation("pandas")
class PandasUInt32Type(ScalarType):
    """32-bit numpy integer subtype."""

    aliases = {pd.UInt32Dtype, "UInt32"}
    dtype = pd.UInt32Dtype()
    itemsize = 4
    type_def = np.uint32
    is_numeric = True
    max = 2**32 - 1
    min = 0


######################
####    UINT64    ####
######################


@register
@UnsignedIntegerType.default
@UnsignedIntegerType.subtype
class UInt64Type(AbstractType):
    """Generic 64-bit unsigned integer type."""

    name = "uint64"
    aliases = {"uint64", "unsigned int64", "u8"}


@register
@NumpyUnsignedIntegerType.default
@NumpyUnsignedIntegerType.subtype
@UInt64Type.default
@UInt64Type.implementation("numpy")
class NumpyUInt64Type(ScalarType):
    """64-bit numpy unsigned integer subtype."""

    aliases = {np.uint64, np.dtype(np.uint64)}
    dtype = np.dtype(np.uint64)
    itemsize = 8
    type_def = np.uint64
    is_numeric = True
    max = 2**64 - 1
    min = 0
    is_nullable = False

    def make_nullable(self) -> ScalarType:
        """Convert this type to a nullable equivalent."""
        return self.registry[PandasUInt64Type]

@register
@PandasUnsignedIntegerType.default
@PandasUnsignedIntegerType.subtype
@UInt64Type.implementation("pandas")
class PandasUInt64Type(ScalarType):
    """64-bit numpy integer subtype."""

    aliases = {pd.UInt64Dtype, "UInt64"}
    dtype = pd.UInt64Dtype()
    itemsize = 8
    type_def = np.uint64
    is_numeric = True
    max = 2**64 - 1
    min = 0


#######################
####    PRIVATE    ####
#######################


# overrides for ``<`` and ``>`` operators ``(A < B)``
ScalarType.registry.priority.update([
    (NumpyIntegerType, PandasIntegerType),
    (NumpySignedIntegerType, PandasSignedIntegerType),
    (NumpyUnsignedIntegerType, PandasUnsignedIntegerType),
    (NumpyInt8Type, PandasInt8Type),
    (NumpyInt16Type, PandasInt16Type),
    (NumpyInt32Type, PandasInt32Type),
    (NumpyInt64Type, PandasInt64Type),
    (NumpyUInt8Type, PandasUInt8Type),
    (NumpyUInt16Type, PandasUInt16Type),
    (NumpyUInt32Type, PandasUInt32Type),
    (NumpyUInt64Type, PandasUInt64Type),
])


# NOTE: some aliases are platform-specific and may be assigned to different
# integer types based on hardware configuration.  Luckily, numpy's dtype()
# factory automatically resolves these, so we can just piggyback off it.

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
    ScalarType.registry.aliases[lookup].aliases.add(alias)
