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
from functools import partial

import numpy as np
cimport numpy as np
import pandas as pd
import pytz

from pdtypes.error import shorten_list

from .base cimport AtomicType, CompositeType
from .base import dispatch, generic, subtype

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve

from pdtypes.util.round cimport Tolerance
from pdtypes.util.round import round_div
from pdtypes.util.time cimport Epoch
from pdtypes.util.time import convert_unit


# TODO: int -> datetime should account for tz.  This is propagated to bool,
# datetime -> datetime


######################
####    MIXINS    ####
######################


class IntegerMixin:

    ############################
    ####    TYPE METHODS    ####
    ############################

    def downcast(
        self,
        series: cast.SeriesWrapper,
        smallest: CompositeType = None
    ) -> cast.SeriesWrapper:
        """Reduce the itemsize of an integer type to fit the observed range."""
        # get downcast candidates
        smaller = self.smaller
        if smallest is not None:
            if self in smallest:
                smaller = []
            else:
                filtered = []
                for t in reversed(smaller):
                    filtered.append(t)
                    if t in smallest:
                        break
                smaller = reversed(filtered)

        # return smallest type that fits observed range
        min_val = int(series.min())
        max_val = int(series.max())
        for t in smaller:
            if min_val < t.min or max_val > t.max:
                continue
            return super().to_integer(
                series,
                dtype=t,
                downcast=None,
                errors="raise"
            )
        return series

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
    def larger(self) -> list:
        """Get a list of types that this type can be upcasted to."""
        # get all subtypes with range wider than self
        result = [
            x for x in self.subtypes if x.min < self.min or x.max > self.max
        ]

        # collapse types that are not unique
        result = [
            x for x in result if not any(x != y and x in y for y in result)
        ]

        return sorted(result, key=lambda x: x.max - x.min)

    @property
    def smaller(self) -> list:
        """Get a list of types that this type can be downcasted to."""
        result = [
            x for x in self.root.subtypes if (
                (x.itemsize or np.inf) < (self.itemsize or np.inf) and
                x.backend == self.backend and
                x.is_signed == self.is_signed
            )
        ]
        return sorted(result, key=lambda x: x.itemsize)

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    @dispatch
    def round(
        self,
        series: cast.SeriesWrapper,
        decimals: int = 0,
        rule: str = "half_even"
    ) -> cast.SeriesWrapper:
        """Round an integer series to the given number of decimal places using
        the specified rounding rule.

        NOTE: this function does not do anything unless the input to `decimals`
        is negative.
        """
        rule = cast.validate_rounding(rule)
        if decimals < 0:
            scale = 10**(-1 * decimals)
            return cast.SeriesWrapper(
                round_div(series.series, scale, rule=rule) * scale,
                hasnans=series.hasnans,
                element_type=series.element_type
            )
        return series

    @dispatch
    def snap(self, series: cast.SeriesWrapper) -> cast.SeriesWrapper:
        """Snap each element of the series to the nearest integer if it is
        within the specified tolerance.

        For integers, this is an identity function.
        """
        return series.copy()

    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert integer data to a boolean data type."""
        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_boolean(series, dtype, errors=errors)

    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert integer data to another integer data type."""
        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_integer(
            series=series,
            dtype=dtype,
            downcast=downcast,
            errors=errors
        )

    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert integer data to a float data type."""
        result = series.astype(dtype)

        # backtrack to check for overflow/precision loss
        # NOTE: integers can always be exactly represented as long as their
        # width in bits fits within the mantissa of the specified floating
        # point type with exponent 1 (listed in the IEEE 754 specification).
        if int(series.min()) < dtype.min or int(series.max()) > dtype.max:
            # overflow
            infs = result.isinf()
            if infs.any():
                if errors == "coerce":
                    result = result[~infs]
                    result.hasnans = True
                    series = series[~infs]  # mirror on original
                else:
                    raise OverflowError(
                        f"values exceed {dtype} range at index "
                        f"{shorten_list(series[infs].index.values)}"
                    )

            # precision loss
            if errors != "coerce":  # coercion ignores precision loss
                # NOTE: we can bypass overflow/precision loss checks by
                # delegating straight to AtomicType
                reverse = super().to_integer(
                    result,
                    dtype=self.upcast(result),
                    downcast=None,
                    errors="raise"
                )
                bad = ~series.within_tol(reverse, tol.real)
                if bad.any():
                    raise ValueError(
                        f"precision loss exceeds tolerance "
                        f"{float(tol.real):g} at index "
                        f"{shorten_list(bad[bad].index.values)}"
                    )

        if downcast is not None:
            return dtype.downcast(result, smallest=downcast, tol=tol)
        return result

    def to_complex(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert integer data to a complex data type."""
        transfer_type = dtype.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_complex(
            series,
            dtype=dtype,
            tol=tol,
            downcast=downcast,
            errors=errors,
            **unused
        )

    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert integer data to a decimal data type."""
        result = series + dtype.type_def(0)  # ~2x faster than apply loop
        result.element_type = dtype
        return result

    def to_datetime(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        rounding: str,
        epoch: Epoch,
        tz: pytz.BaseTzInfo,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert integer data to a datetime data type."""
        # convert to python integer to avoid overflow
        series = cast.SeriesWrapper(
            series.series.astype("O"),
            hasnans=series.hasnans,
            element_type=resolve.resolve_type(PythonIntegerType)
        )

        # convert to ns
        if step_size != 1:
            series.series *= step_size
        series.series = convert_unit(series.series, unit, "ns", since=epoch)

        # account for non-utc epoch
        if epoch:
            series.series += epoch.offset

        # check for overflow and upcast if applicable
        series, dtype = series.boundscheck(dtype, errors=errors)

        # convert to final representation
        return dtype.from_ns(
            series,
            unit=unit,
            step_size=step_size,
            rounding=rounding,
            epoch=epoch,
            tz=tz,
            errors=errors,
            **unused
        )

    def to_timedelta(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        rounding: str,
        epoch: Epoch,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert integer data to a timedelta data type."""
        # convert to python integer to avoid overflow
        series = cast.SeriesWrapper(
            series.series.astype("O"),
            hasnans=series.hasnans,
            element_type=resolve.resolve_type(PythonIntegerType)
        )

        # convert to ns
        if step_size != 1:
            series.series *= step_size
        series.series = convert_unit(series.series, unit, "ns", since=epoch)

        # check for overflow and upcast if necessary
        series, dtype = series.boundscheck(dtype, errors=errors)

        # convert to final representation
        return dtype.from_ns(
            series,
            unit=unit,
            step_size=step_size,
            rounding=rounding,
            epoch=epoch,
            errors=errors,
            **unused
        )

    def to_string(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        base: int,
        format: str,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert integer data to a string data type in any base."""
        # use non-decimal base, in conjunction with format
        if base and base != 10:
            if format:
                call = lambda x: f"{int_to_base(x, base=base):{format}}"
            else:
                call = partial(int_to_base, base=base)
            series = series.apply_with_errors(call, errors="raise")
            series.element_type = str
            format = None

        return super().to_string(
            series=series,
            dtype=dtype,
            format=format,
            errors=errors
        )


#######################
####    GENERIC    ####
#######################


@generic
class IntegerType(IntegerMixin, AtomicType):
    """Generic integer supertype."""

    conversion_func = cast.to_integer  # all subtypes/backends inherit this
    name = "int"
    aliases = {int, "int", "integer"}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = int
    max = 2**63 - 1
    min = -2**63


@generic
@subtype(IntegerType)
class SignedIntegerType(IntegerMixin, AtomicType):
    """Generic signed integer supertype."""

    name = "signed"
    aliases = {"signed", "signed int", "signed integer", "i"}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = int
    max = 2**63 - 1
    min = -2**63


@generic
@subtype(IntegerType)
class UnsignedIntegerType(IntegerMixin, AtomicType):
    """Generic 8-bit unsigned integer type."""

    name = "unsigned"
    aliases = {"unsigned", "unsigned int", "unsigned integer", "uint", "u"}
    dtype = np.dtype(np.uint64)
    itemsize = 8
    type_def = np.uint64
    max = 2**64 - 1
    min = 0


@generic
@subtype(SignedIntegerType)
class Int8Type(IntegerMixin, AtomicType):
    """Generic 8-bit signed integer type."""

    name = "int8"
    aliases = {"int8", "i1"}
    dtype = np.dtype(np.int8)
    itemsize = 1
    type_def = np.uint8
    max = 2**7 - 1
    min = -2**7


@generic
@subtype(SignedIntegerType)
class Int16Type(IntegerMixin, AtomicType):
    """Generic 16-bit signed integer type."""

    name = "int16"
    aliases = {"int16", "i2"}
    dtype = np.dtype(np.int16)
    itemsize = 2
    type_def = np.uint16
    max = 2**15 - 1
    min = -2**15


@generic
@subtype(SignedIntegerType)
class Int32Type(IntegerMixin, AtomicType):
    """Generic 32-bit signed integer type."""

    name = "int32"
    aliases = {"int32", "i4"}
    dtype = np.dtype(np.int32)
    itemsize = 4
    type_def = np.uint32
    max = 2**31 - 1
    min = -2**31


@generic
@subtype(SignedIntegerType)
class Int64Type(IntegerMixin, AtomicType):
    """Generic 64-bit signed integer type."""

    name = "int64"
    aliases = {"int64", "i8"}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = np.uint64
    max = 2**63 - 1
    min = -2**63


@generic
@subtype(UnsignedIntegerType)
class UInt8Type(IntegerMixin, AtomicType):
    """Generic 8-bit unsigned integer type."""

    name = "uint8"
    aliases = {"uint8", "unsigned int8", "u1"}
    dtype = np.dtype(np.uint8)
    itemsize = 1
    type_def = np.uint8
    max = 2**8 - 1
    min = 0


@generic
@subtype(UnsignedIntegerType)
class UInt16Type(IntegerMixin, AtomicType):
    """Generic 16-bit unsigned integer type."""

    name = "uint16"
    aliases = {"uint16", "unsiged int16", "u2"}
    dtype = np.dtype(np.uint16)
    itemsize = 2
    type_def = np.uint16
    max = 2**16 - 1
    min = 0


@generic
@subtype(UnsignedIntegerType)
class UInt32Type(IntegerMixin, AtomicType):
    """Generic 32-bit unsigned integer type."""

    name = "uint32"
    aliases = {"uint32", "unsigned int32", "u4"}
    dtype = np.dtype(np.uint32)
    itemsize = 4
    type_def = np.uint32
    max = 2**32 - 1
    min = 0


@generic
@subtype(UnsignedIntegerType)
class UInt64Type(IntegerMixin, AtomicType):
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


@IntegerType.register_backend("numpy")
class NumpyIntegerType(IntegerMixin, AtomicType):
    """Numpy integer type."""

    aliases = {np.integer}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@subtype(NumpyIntegerType)
@SignedIntegerType.register_backend("numpy")
class NumpySignedIntegerType(IntegerMixin, AtomicType):
    """Numpy signed integer type."""

    aliases = {np.signedinteger}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@subtype(NumpyIntegerType)
@UnsignedIntegerType.register_backend("numpy")
class NumpyUnsignedIntegerType(IntegerMixin, AtomicType):
    """Numpy unsigned integer type."""

    aliases = {np.unsignedinteger}
    dtype = np.dtype(np.uint64)
    itemsize = 8
    type_def = np.uint64
    max = 2**64 - 1
    min = 0


@subtype(NumpySignedIntegerType)
@Int8Type.register_backend("numpy")
class NumpyInt8Type(IntegerMixin, AtomicType):
    """8-bit numpy integer subtype."""

    aliases = {np.int8, np.dtype(np.int8)}
    dtype = np.dtype(np.int8)
    itemsize = 1
    type_def = np.int8
    max = 2**7 - 1
    min = -2**7


@subtype(NumpySignedIntegerType)
@Int16Type.register_backend("numpy")
class NumpyInt16Type(IntegerMixin, AtomicType):
    """16-bit numpy integer subtype."""

    aliases = {np.int16, np.dtype(np.int16)}
    dtype = np.dtype(np.int16)
    itemsize = 2
    type_def = np.int16
    max = 2**15 - 1
    min = -2**15


@subtype(NumpySignedIntegerType)
@Int32Type.register_backend("numpy")
class NumpyInt32Type(IntegerMixin, AtomicType):
    """32-bit numpy integer subtype."""

    aliases = {np.int32, np.dtype(np.int32)}
    dtype = np.dtype(np.int32)
    itemsize = 4
    type_def = np.int32
    max = 2**31 - 1
    min = -2**31


@subtype(NumpySignedIntegerType)
@Int64Type.register_backend("numpy")
class NumpyInt64Type(IntegerMixin, AtomicType):
    """64-bit numpy integer subtype."""

    aliases = {np.int64, np.dtype(np.int64)}
    dtype = np.dtype(np.int64)
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@subtype(NumpyUnsignedIntegerType)
@UInt8Type.register_backend("numpy")
class NumpyUInt8Type(IntegerMixin, AtomicType):
    """8-bit numpy unsigned integer subtype."""

    aliases = {np.uint8, np.dtype(np.uint8)}
    dtype = np.dtype(np.uint8)
    itemsize = 1
    type_def = np.uint8
    max = 2**8 - 1
    min = 0


@subtype(NumpyUnsignedIntegerType)
@UInt16Type.register_backend("numpy")
class NumpyUInt16Type(IntegerMixin, AtomicType):
    """16-bit numpy unsigned integer subtype."""

    aliases = {np.uint16, np.dtype(np.uint16)}
    dtype = np.dtype(np.uint16)
    itemsize = 2
    type_def = np.uint16
    max = 2**16 - 1
    min = 0


@subtype(NumpyUnsignedIntegerType)
@UInt32Type.register_backend("numpy")
class NumpyUInt32Type(IntegerMixin, AtomicType):
    """32-bit numpy unsigned integer subtype."""

    aliases = {np.uint32, np.dtype(np.uint32)}
    dtype = np.dtype(np.uint32)
    itemsize = 4
    type_def = np.uint32
    max = 2**32 - 1
    min = 0


@subtype(NumpyUnsignedIntegerType)
@UInt64Type.register_backend("numpy")
class NumpyUInt64Type(IntegerMixin, AtomicType):
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


@IntegerType.register_backend("pandas")
class PandasIntegerType(IntegerMixin, AtomicType):
    """Pandas integer supertype."""

    aliases = set()
    dtype = pd.Int64Dtype()
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@subtype(PandasIntegerType)
@SignedIntegerType.register_backend("pandas")
class PandasSignedIntegerType(IntegerMixin, AtomicType):
    """Python signed integer supertype."""

    aliases = set()
    dtype = pd.Int64Dtype()
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@subtype(PandasIntegerType)
@UnsignedIntegerType.register_backend("pandas")
class PandasUnsignedIntegerType(IntegerMixin, AtomicType):
    """Numpy unsigned integer type."""

    aliases = set()
    dtype = pd.UInt64Dtype()
    itemsize = 8
    type_def = np.uint64
    max = 2**64 - 1
    min = 0


@subtype(PandasSignedIntegerType)
@Int8Type.register_backend("pandas")
class PandasInt8Type(IntegerMixin, AtomicType):
    """8-bit numpy integer subtype."""

    aliases = {pd.Int8Dtype(), "Int8"}
    dtype = pd.Int8Dtype()
    itemsize = 1
    type_def = np.int8
    max = 2**7 - 1
    min = -2**7


@subtype(PandasSignedIntegerType)
@Int16Type.register_backend("pandas")
class PandasInt16Type(IntegerMixin, AtomicType):
    """16-bit numpy integer subtype."""

    aliases = {pd.Int16Dtype(), "Int16"}
    dtype = pd.Int16Dtype()
    itemsize = 2
    type_def = np.int16
    max = 2**15 - 1
    min = -2**15


@subtype(PandasSignedIntegerType)
@Int32Type.register_backend("pandas")
class PandasInt32Type(IntegerMixin, AtomicType):
    """32-bit numpy integer subtype."""

    aliases = {pd.Int32Dtype(), "Int32"}
    dtype = pd.Int32Dtype()
    itemsize = 4
    type_def = np.int32
    max = 2**31 - 1
    min = -2**31


@subtype(PandasSignedIntegerType)
@Int64Type.register_backend("pandas")
class PandasInt64Type(IntegerMixin, AtomicType):
    """64-bit numpy integer subtype."""

    aliases = {pd.Int64Dtype(), "Int64"}
    dtype = pd.Int64Dtype()
    itemsize = 8
    type_def = np.int64
    max = 2**63 - 1
    min = -2**63


@subtype(PandasUnsignedIntegerType)
@UInt8Type.register_backend("pandas")
class PandasUInt8Type(IntegerMixin, AtomicType):
    """8-bit numpy integer subtype."""

    aliases = {pd.UInt8Dtype(), "UInt8"}
    dtype = pd.UInt8Dtype()
    itemsize = 1
    type_def = np.uint8
    max = 2**8 - 1
    min = 0


@subtype(PandasUnsignedIntegerType)
@UInt16Type.register_backend("pandas")
class PandasUInt16Type(IntegerMixin, AtomicType):
    """16-bit numpy integer subtype."""

    aliases = {pd.UInt16Dtype(), "UInt16"}
    dtype = pd.UInt16Dtype()
    itemsize = 2
    type_def = np.uint16
    max = 2**16 - 1
    min = 0


@subtype(PandasUnsignedIntegerType)
@UInt32Type.register_backend("pandas")
class PandasUInt32Type(IntegerMixin, AtomicType):
    """32-bit numpy integer subtype."""

    aliases = {pd.UInt32Dtype(), "UInt32"}
    dtype = pd.UInt32Dtype()
    itemsize = 4
    type_def = np.uint32
    max = 2**32 - 1
    min = 0


@subtype(PandasUnsignedIntegerType)
@UInt64Type.register_backend("pandas")
class PandasUInt64Type(IntegerMixin, AtomicType):
    """64-bit numpy integer subtype."""

    aliases = {pd.UInt64Dtype(), "UInt64"}
    dtype = pd.UInt64Dtype()
    itemsize = 8
    type_def = np.uint64
    max = 2**64 - 1
    min = 0


######################
####    PYTHON    ####
######################


@IntegerType.register_backend("python")
@SignedIntegerType.register_backend("python")
class PythonIntegerType(IntegerMixin, AtomicType):
    """Python integer supertype."""

    aliases = set()
    type_def = int
    max = np.inf
    min = -np.inf


#######################
####    PRIVATE    ####
#######################


cdef Py_UNICODE[36] base_lookup = (
    [chr(ord("0") + i) for i in range(10)] + 
    [chr(ord("A") + i) for i in range(26)]
)


cdef str int_to_base(object val, unsigned char base):
    if not val:
        return "0"

    cdef bint negative = val < 0
    if negative:
        val = abs(val)

    cdef list chars = []
    while val:
        chars.append(base_lookup[<unsigned char> val % base])
        val //= base

    cdef str result = "".join(chars[::-1])
    if negative:
        result = "-" + result
    return result


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
