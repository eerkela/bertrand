"""This package defines the structure and contents of the ``pdcast`` type
system, including abstract base classes that can be used to extend it to
arbitrary data.

Base Classes
------------
AtomicType
    Base unit of the ``pdcast`` type system.  This describes the type of a
    scalar element of the associated type.

AdapterType
    A type that acts as a wrapper for another type.  Common examples of this
    are for sparse or categorical data, which can be in any type.

CompositeType
    A set-like container for types, which allows grouped membership checks and
    split-apply-combine series decomposition.

TypeRegistry
    A global registry that tracks registered types and manages the
    relationships between them.

Decorators
----------
register
    Add a subclass of ``AtomicType`` or ``AdapterType`` to the ``pdcast`` type
    system.

subtype
    Register a subclass of ``AtomicType`` as a subtype of another type.

generic
    Transform a subclass of ``AtomicType`` into a generic type, which can
    reference other types as backends.
"""
from .array import AbstractArray, AbstractDtype
from .base import (
    TypeRegistry, Type, CompositeType, ScalarType, AdapterType, AtomicType,
    ParentType, register
)
from .boolean import (
    BooleanType, NumpyBooleanType, PandasBooleanType, PythonBooleanType
)
from .integer import (
    IntegerType, SignedIntegerType, UnsignedIntegerType, Int8Type, Int16Type,
    Int32Type, Int64Type, UInt8Type, UInt16Type, UInt32Type, UInt64Type,
    NumpyIntegerType, NumpySignedIntegerType, NumpyUnsignedIntegerType,
    NumpyInt8Type, NumpyInt16Type, NumpyInt32Type, NumpyInt64Type,
    NumpyUInt8Type, NumpyUInt16Type, NumpyUInt32Type, NumpyUInt64Type,
    PandasIntegerType, PandasSignedIntegerType, PandasUnsignedIntegerType,
    PandasInt8Type, PandasInt16Type, PandasInt32Type, PandasInt64Type,
    PandasUInt8Type, PandasUInt16Type, PandasUInt32Type, PandasUInt64Type,
    PythonIntegerType
)
from .float import (
    FloatType, Float16Type, Float32Type, Float64Type, NumpyFloatType,
    NumpyFloat16Type, NumpyFloat32Type, NumpyFloat64Type, PythonFloatType
)
from .complex import (
    ComplexType, Complex64Type, Complex128Type, NumpyComplexType,
    NumpyComplex64Type, NumpyComplex128Type, PythonComplexType
)
from .decimal import DecimalType, PythonDecimalType
from .datetime import (
    DatetimeType, NumpyDatetime64Type, PandasTimestampType, PythonDatetimeType
)
# from .timedelta import (
#     TimedeltaType, NumpyTimedelta64Type, PandasTimedeltaType,
#     PythonTimedeltaType
# )
from .string import StringType, PythonStringType
from .object import ObjectType
# from .sparse import SparseType
# from .categorical import CategoricalType


# # conditional imports
# try:
#     from .float import Float80Type, NumpyFloat80Type
# except ImportError:
#     pass
# try:
#     from .complex import Complex160Type, NumpyComplex160Type
# except ImportError:
#     pass
try:
    from .string import PyArrowStringType
except ImportError:
    pass


# global objects
registry = AtomicType.registry
