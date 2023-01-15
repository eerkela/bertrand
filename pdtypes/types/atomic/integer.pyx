"""Defines integer type hierarchy:

                     +--------------------------------+
                     |               int              |
                     +--------------------------------+
                    /                                  \
        +----------------------+            +----------------------+
        |        signed        |            |       unsigned       |
        +----------------------+            +----------------------+
       /       |        |       \          /       |        |       \
    +----+   +----+   +----+   +----+   +----+   +----+   +----+   +----+
    | i1 |   | i2 |   | i4 |   | i8 |   | u1 |   | u2 |   | u4 |   | u8 |
    +----+   +----+   +----+   +----+   +----+   +----+   +----+   +----+

"""
import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AdapterType, AtomicType
from .base import generic, subtype

from pdtypes.error import shorten_list
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast


# TODO: int[numpy/pandas].smaller currently includes uint64 (as intended), but
# its min/max caps out at 2**63.  As such, in cast methods, a range check will
# always ignore uint64[numpy/pandas], even when it should be considered.  This
# also affects the default dtype for these types.


######################
####    MIXINS    ####
######################


class IntegerMixin:

    ##############################
    ####    CUSTOMIZATIONS    ####
    ##############################

    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert integer data to a boolean data type."""
        # check for overflow
        if series.min() < 0 or series.max() > 1:
            if errors == "coerce":
                series = cast.SeriesWrapper(
                    series.abs().clip(0, 1),
                    is_na=series.isna(),
                    hasnans=series.hasnans
                )
            else:
                index = series[(series < 0) | (series > 1)].index.values
                raise OverflowError(
                    f"values exceed {dtype} range at index "
                    f"{shorten_list(index)}"
                )

        # delegate to AtomicType.to_boolean()
        return super().to_boolean(
            series=series,
            dtype=dtype,
            errors=errors,
            **unused
        )

    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert integer data to another integer data type."""
        # check for overflow
        if series.min() < dtype.min or series.max() > dtype.max:
            if errors == "coerce":
                pass
        
        raise NotImplementedError()

    ######################
    ####    EXTRAS    ####
    ######################

    def downcast(self, series: pd.Series) -> AtomicType:
        """Reduce the itemsize of an integer type to fit the observed range."""
        min_val = int(series.min())
        max_val = int(series.max())
        for s in self.smaller:
            if min_val >= s.min and max_val <= s.max:
                return s
        return self

    def force_nullable(self) -> AtomicType:
        """Create an equivalent integer type that can accept missing values."""
        if self.is_nullable:
            return self
        return self.generic.instance(backend="pandas")

    @property
    def is_nullable(self) -> bool:
        if isinstance(self.dtype, np.dtype):
            return np.issubdtype(self.dtype, "O")
        return True

    @property
    def is_signed(self) -> bool:
        return not self.is_subtype(UnsignedIntegerType)

    @property
    def smaller(self) -> list:
        """Get a list of types that `self` can be downcasted to."""
        result = [
            x for x in self.root.subtypes if (
                x.backend == self.backend and
                x.is_signed == self.is_signed and
                (x.itemsize or np.inf) < (self.itemsize or np.inf)
            )
        ]
        result.sort(key=lambda x: x.itemsize)
        if self.is_root:  # consider unsigned types
            max_signed = max(
                x.max for x in self.subtypes if (
                    x.is_signed and not np.isinf(x.max)
                )
            )
            larger = [
                x for x in UnsignedIntegerType.instance().subtypes if (
                    x.backend == self.backend and
                    x.max > max_signed and
                    x not in UnsignedIntegerType.backends.values()
                )
            ]
            larger.sort(key=lambda x: (x.itemsize or np.inf))
            result.extend(larger)
        return result


#############################
####    GENERIC TYPES    ####
#############################


@generic
class IntegerType(IntegerMixin, AtomicType):
    """Generic integer supertype."""

    name = "int"
    aliases = {int, "int", "integer"}
    min = -np.inf
    max = np.inf

    def __init__(self):
        super().__init__(
            type_def=int,
            dtype=np.dtype(np.int64),
            na_value=pd.NA,
            itemsize=None
        )


@generic
@subtype(IntegerType)
class SignedIntegerType(IntegerMixin, AtomicType):
    """Generic signed integer supertype."""

    name = "signed"
    aliases = {"signed", "signed int", "signed integer", "i"}
    min = -np.inf
    max = np.inf

    def __init__(self):
        super().__init__(
            type_def=int,
            dtype=np.dtype(np.int64),
            na_value=pd.NA,
            itemsize=None
        )


@generic
@subtype(IntegerType)
class UnsignedIntegerType(IntegerMixin, AtomicType):
    """Generic 8-bit unsigned integer type."""

    name="unsigned"
    aliases={"unsigned", "unsigned int", "unsigned integer", "uint", "u"}
    min=0
    max=2**64 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint64,
            dtype=np.dtype(np.uint64),
            na_value=pd.NA,
            itemsize=8
        )


@generic
@subtype(SignedIntegerType)
class Int8Type(IntegerMixin, AtomicType):
    """Generic 8-bit signed integer type."""

    name="int8"
    aliases={"int8", "i1"}
    min=-2**7
    max=2**7 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int8,
            dtype=np.dtype(np.int8),
            na_value=pd.NA,
            itemsize=1
        )

@generic
@subtype(SignedIntegerType)
class Int16Type(IntegerMixin, AtomicType):
    """Generic 16-bit signed integer type."""

    name="int16"
    aliases={"int16", "i2"}
    min=-2**15
    max=2**15 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int16,
            dtype=np.dtype(np.int16),
            na_value=pd.NA,
            itemsize=2
        )


@generic
@subtype(SignedIntegerType)
class Int32Type(IntegerMixin, AtomicType):
    """Generic 32-bit signed integer type."""

    name="int32"
    aliases={"int32", "i4"}
    min=-2**31
    max=2**31 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int32,
            dtype=np.dtype(np.int32),
            na_value=pd.NA,
            itemsize=4
        )


@generic
@subtype(SignedIntegerType)
class Int64Type(IntegerMixin, AtomicType):
    """Generic 64-bit signed integer type."""

    name="int64"
    aliases={"int64", "i8"}
    min=-2**63
    max=2**63 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int64,
            dtype=np.dtype(np.int64),
            na_value=pd.NA,
            itemsize=8
        )


@generic
@subtype(UnsignedIntegerType)
class UInt8Type(IntegerMixin, AtomicType):
    """Generic 8-bit unsigned integer type."""

    name="uint8"
    aliases={"uint8", "unsigned int8", "u1"}
    min=0
    max=2**8 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint8,
            dtype=np.dtype(np.uint8),
            na_value=pd.NA,
            itemsize=1
        )


@generic
@subtype(UnsignedIntegerType)
class UInt16Type(IntegerMixin, AtomicType):
    """Generic 16-bit unsigned integer type."""

    name="uint16"
    aliases={"uint16", "unsiged int16", "u2"}
    min=0
    max=2**16 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint16,
            dtype=np.dtype(np.uint16),
            na_value=pd.NA,
            itemsize=2
        )


@generic
@subtype(UnsignedIntegerType)
class UInt32Type(IntegerMixin, AtomicType):
    """Generic 32-bit unsigned integer type."""

    name="uint32"
    aliases={"uint32", "unsigned int32", "u4"}
    min=0
    max=2**32 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint32,
            dtype=np.dtype(np.uint32),
            na_value=pd.NA,
            itemsize=4
        )


@generic
@subtype(UnsignedIntegerType)
class UInt64Type(IntegerMixin, AtomicType):
    """Generic 64-bit unsigned integer type."""

    name="uint64"
    aliases={"uint64", "unsigned int64", "u8"}
    min=0
    max=2**64 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint64,
            dtype=np.dtype(np.uint64),
            na_value=pd.NA,
            itemsize=8
        )


###########################
####    NUMPY TYPES    ####
###########################


@IntegerType.register_backend("numpy")
class NumpyIntegerType(IntegerMixin, AtomicType):
    """Numpy integer type."""

    aliases={np.integer}
    min=-2**63
    max=2**63 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int64,
            dtype=np.dtype(np.int64),
            na_value=pd.NA,
            itemsize=8
        )


@SignedIntegerType.register_backend("numpy")
@subtype(NumpyIntegerType)
class NumpySignedIntegerType(IntegerMixin, AtomicType):
    """Numpy signed integer type."""

    aliases={np.signedinteger}
    min=-2**63
    max=2**63 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int64,
            dtype=np.dtype(np.int64),
            na_value=pd.NA,
            itemsize=8
        )


@UnsignedIntegerType.register_backend("numpy")
@subtype(NumpyIntegerType)
class NumpyUnsignedIntegerType(IntegerMixin, AtomicType):
    """Numpy unsigned integer type."""

    aliases={np.unsignedinteger}
    min=0
    max=2**64 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint64,
            dtype=np.dtype(np.uint64),
            na_value=pd.NA,
            itemsize=8
        )


@Int8Type.register_backend("numpy")
@subtype(NumpySignedIntegerType)
class NumpyInt8Type(IntegerMixin, AtomicType):
    """8-bit numpy integer subtype."""

    aliases={np.int8, np.dtype(np.int8)}
    min=-2**7
    max=2**7 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int8,
            dtype=np.dtype(np.int8),
            na_value=pd.NA,
            itemsize=1
        )


@Int16Type.register_backend("numpy")
@subtype(NumpySignedIntegerType)
class NumpyInt16Type(IntegerMixin, AtomicType):
    """16-bit numpy integer subtype."""

    aliases={np.int16, np.dtype(np.int16)}
    min=-2**15
    max=2**15 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int16,
            dtype=np.dtype(np.int16),
            na_value=pd.NA,
            itemsize=2
        )


@Int32Type.register_backend("numpy")
@subtype(NumpySignedIntegerType)
class NumpyInt32Type(IntegerMixin, AtomicType):
    """32-bit numpy integer subtype."""

    aliases={np.int32, np.dtype(np.int32)}
    min=-2**31
    max=2**31 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int32,
            dtype=np.dtype(np.int32),
            na_value=pd.NA,
            itemsize=4
        )


@Int64Type.register_backend("numpy")
@subtype(NumpySignedIntegerType)
class NumpyInt64Type(IntegerMixin, AtomicType):
    """64-bit numpy integer subtype."""

    aliases={np.int64, np.dtype(np.int64)}
    min=-2**63
    max=2**63 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int64,
            dtype=np.dtype(np.int64),
            na_value=pd.NA,
            itemsize=8
        )


@UInt8Type.register_backend("numpy")
@subtype(NumpyUnsignedIntegerType)
class NumpyUInt8Type(IntegerMixin, AtomicType):
    """8-bit numpy unsigned integer subtype."""

    aliases={np.uint8, np.dtype(np.uint8)}
    min=0
    max=2**8 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint8,
            dtype=np.dtype(np.uint8),
            na_value=pd.NA,
            itemsize=1
        )


@UInt16Type.register_backend("numpy")
@subtype(NumpyUnsignedIntegerType)
class NumpyUInt16Type(IntegerMixin, AtomicType):
    """16-bit numpy unsigned integer subtype."""

    aliases={np.uint16, np.dtype(np.uint16)}
    min=0
    max=2**16 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint16,
            dtype=np.dtype(np.uint16),
            na_value=pd.NA,
            itemsize=2
        )


@UInt32Type.register_backend("numpy")
@subtype(NumpyUnsignedIntegerType)
class NumpyUInt32Type(IntegerMixin, AtomicType):
    """32-bit numpy unsigned integer subtype."""

    aliases={np.uint32, np.dtype(np.uint32)}
    min=0
    max=2**32 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint32,
            dtype=np.dtype(np.uint32),
            na_value=pd.NA,
            itemsize=4
        )


@UInt64Type.register_backend("numpy")
@subtype(NumpyUnsignedIntegerType)
class NumpyUInt64Type(IntegerMixin, AtomicType):
    """64-bit numpy unsigned integer subtype."""

    aliases={np.uint64, np.dtype(np.uint64)}
    min=0
    max=2**64 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint64,
            dtype=np.dtype(np.uint64),
            na_value=pd.NA,
            itemsize=8
        )


############################
####    PANDAS TYPES    ####
############################


@IntegerType.register_backend("pandas")
class PandasIntegerType(IntegerMixin, AtomicType):
    """Pandas integer supertype."""

    aliases=set()
    min=-2**63
    max=2**63 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int64,
            dtype=pd.Int64Dtype(),
            na_value=pd.NA,
            itemsize=8
        )


@SignedIntegerType.register_backend("pandas")
@subtype(PandasIntegerType)
class PandasSignedIntegerType(IntegerMixin, AtomicType):
    """Python signed integer supertype."""

    aliases=set()
    min=-2**63
    max=2**63 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int64,
            dtype=pd.Int64Dtype(),
            na_value=pd.NA,
            itemsize=8
        )


@UnsignedIntegerType.register_backend("pandas")
@subtype(PandasIntegerType)
class PandasUnsignedIntegerType(IntegerMixin, AtomicType):
    """Numpy unsigned integer type."""

    aliases=set()
    min=0
    max=2**64 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint64,
            dtype=pd.UInt64Dtype(),
            na_value=pd.NA,
            itemsize=8
        )


@Int8Type.register_backend("pandas")
@subtype(PandasSignedIntegerType)
class PandasInt8Type(IntegerMixin, AtomicType):
    """8-bit numpy integer subtype."""

    aliases={pd.Int8Dtype(), "Int8"}
    min=-2**7
    max=2**7 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int8,
            dtype=pd.Int8Dtype(),
            na_value=pd.NA,
            itemsize=1
        )


@Int16Type.register_backend("pandas")
@subtype(PandasSignedIntegerType)
class PandasInt16Type(IntegerMixin, AtomicType):
    """16-bit numpy integer subtype."""

    aliases={pd.Int16Dtype(), "Int16"}
    min=-2**15
    max=2**15 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int16,
            dtype=pd.Int16Dtype(),
            na_value=pd.NA,
            itemsize=2
        )


@Int32Type.register_backend("pandas")
@subtype(PandasSignedIntegerType)
class PandasInt32Type(IntegerMixin, AtomicType):
    """32-bit numpy integer subtype."""

    aliases={pd.Int32Dtype(), "Int32"}
    min=-2**31
    max=2**31 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int32,
            dtype=pd.Int32Dtype(),
            na_value=pd.NA,
            itemsize=4
        )


@Int64Type.register_backend("pandas")
@subtype(PandasSignedIntegerType)
class PandasInt64Type(IntegerMixin, AtomicType):
    """64-bit numpy integer subtype."""

    aliases={pd.Int64Dtype(), "Int64"}
    min=-2**63
    max=2**63 - 1

    def __init__(self):
        super().__init__(
            type_def=np.int64,
            dtype=pd.Int64Dtype(),
            na_value=pd.NA,
            itemsize=8
        )


@UInt8Type.register_backend("pandas")
@subtype(PandasUnsignedIntegerType)
class PandasUInt8Type(IntegerMixin, AtomicType):
    """8-bit numpy integer subtype."""

    aliases={pd.UInt8Dtype(), "UInt8"}
    min=0
    max=2**8 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint8,
            dtype=pd.UInt8Dtype(),
            na_value=pd.NA,
            itemsize=1
        )


@UInt16Type.register_backend("pandas")
@subtype(PandasUnsignedIntegerType)
class PandasUInt16Type(IntegerMixin, AtomicType):
    """16-bit numpy integer subtype."""

    aliases={pd.UInt16Dtype(), "UInt16"}
    min=0
    max=2**16 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint16,
            dtype=pd.UInt16Dtype(),
            na_value=pd.NA,
            itemsize=2
        )


@UInt32Type.register_backend("pandas")
@subtype(PandasUnsignedIntegerType)
class PandasUInt32Type(IntegerMixin, AtomicType):
    """32-bit numpy integer subtype."""

    aliases={pd.UInt32Dtype(), "UInt32"}
    min=0
    max=2**32 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint32,
            dtype=pd.UInt32Dtype(),
            na_value=pd.NA,
            itemsize=4
        )


@UInt64Type.register_backend("pandas")
@subtype(PandasUnsignedIntegerType)
class PandasUInt64Type(IntegerMixin, AtomicType):
    """64-bit numpy integer subtype."""

    aliases={pd.UInt64Dtype(), "UInt64"}
    min=0
    max=2**64 - 1

    def __init__(self):
        super().__init__(
            type_def=np.uint64,
            dtype=pd.UInt64Dtype(),
            na_value=pd.NA,
            itemsize=8
        )


############################
####    PYTHON TYPES    ####
############################


@IntegerType.register_backend("python")
@SignedIntegerType.register_backend("python")
class PythonIntegerType(IntegerMixin, AtomicType):
    """Python integer supertype."""

    aliases=set()
    min=-np.inf
    max=np.inf

    def __init__(self):
        super().__init__(
            type_def=int,
            dtype=np.dtype("O"),
            na_value=pd.NA,
            itemsize=None
        )


#########################################
####    PLATFORM-SPECIFIC ALIASES    ####
#########################################


# the following aliases are platform-specific and may be assigned to different
# AtomicTypes based on hardware configuration.  Luckily, numpy's dtype()
# factory automatically resolves these, so we can just piggyback off it.
cdef dict platform_specific_aliases = {
    # C char
    "char": np.dtype(np.byte),
    "signed char": "char",
    "byte": "char",
    "b": "char",

    # C short
    "short": np.dtype(np.short),
    "short int": "short",
    "short integer": "short",
    "signed short": "short",
    "signed short int": "short",
    "signed short integer": "short",
    "h": "short",

    # C int
    "intc": np.dtype(np.intc),
    "signed intc": "intc",

    # C long
    "long": np.dtype(np.int_),
    "long int": "long",
    "long integer": "long",
    "signed long": "long",
    "signed long int": "long",
    "signed long integer": "long",
    "l": "long",

    # C long long
    "long long": np.dtype(np.longlong),
    "long long int": "long long",
    "long long integer": "long long",
    "signed long long": "long long",
    "signed long long int": "long long",
    "signed long long integer": "long long",
    "longlong": "long long",
    "signed longlong": "long long",
    "q": "long long",

    # C ssize_t
    "ssize_t": np.dtype(np.intp),
    "intp": "ssize_t",
    "int0": "ssize_t",
    "p": "ssize_t",

    # C unsigned char
    "unsigned char": np.dtype(np.ubyte),
    "unsigned byte": "unsigned char",
    "ubyte": "unsigned char",
    "B": "unsigned char",

    # C unsigned short
    "unsigned short": np.dtype(np.ushort),
    "unsigned short int": "unsigned short",
    "unsigned short integer": "unsigned short",
    "ushort": "unsigned short",
    "H": "unsigned short",

    # C unsigned int
    "unsigned intc": np.dtype(np.uintc),
    "uintc": "unsigned intc",
    "I": "unsigned intc",

    # C unsigned long
    "unsigned long": np.dtype(np.uint),
    "unsigned long int": "unsigned long",
    "unsigned long integer": "unsigned long",
    "ulong": "unsigned long",
    "L": "unsigned long",

    # C unsigned long long
    "unsigned long long": np.dtype(np.ulonglong),
    "unsigned long long int": "unsigned long long",
    "unsigned long long integer": "unsigned long long",
    "ulonglong": "unsigned long long",
    "unsigned longlong": "unsigned long long",
    "Q": "unsigned long long",

    # C size_t
    "size_t": np.dtype(np.uintp),
    "uintp": "size_t",
    "uint0": "size_t",
    "P": "size_t",
}
for alias, lookup in platform_specific_aliases.items():
    AtomicType.registry.aliases[lookup].register_alias(alias)
