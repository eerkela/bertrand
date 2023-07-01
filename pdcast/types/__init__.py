"""This package defines the structure and contents of the ``pdcast`` type
system, including abstract base classes that can be used to extend it to
arbitrary data.

Base Classes
------------
ScalarType
    Base unit of the ``pdcast`` type system.  This describes the type of a
    scalar element of the associated type.

DecoratorType
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
    Add a subclass of ``ScalarType`` or ``DecoratorType`` to the ``pdcast`` type
    system.

subtype
    Register a subclass of ``ScalarType`` as a subtype of another type.

generic
    Transform a subclass of ``ScalarType`` into a generic type, which can
    reference other types as backends.
"""
from .array import ObjectArray, ObjectDtype
from .base import (
    AbstractType, AliasManager, CacheValue, CompositeType, DecoratorType,
    PrioritySet, ScalarType, Type, TypeRegistry, VectorType, register
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
    FloatType, Float16Type, Float32Type, Float64Type, Float80Type,
    NumpyFloatType, NumpyFloat16Type, NumpyFloat32Type, NumpyFloat64Type,
    NumpyFloat80Type, PythonFloatType
)
from .complex import (
    ComplexType, Complex64Type, Complex128Type, Complex160Type,
    NumpyComplexType, NumpyComplex64Type, NumpyComplex128Type,
    NumpyComplex160Type, PythonComplexType
)
from .decimal import DecimalType, PythonDecimalType
from .datetime import (
    DatetimeType, NumpyDatetime64Type, PandasTimestampType, PythonDatetimeType
)
from .timedelta import (
    TimedeltaType, NumpyTimedelta64Type, PandasTimedeltaType,
    PythonTimedeltaType
)
from .string import StringType, PythonStringType
from .object import ObjectType
from .sparse import SparseType
from .categorical import CategoricalType


# conditional imports
try:
    from .string import PyArrowStringType
except ImportError:
    pass


# global objects
registry = Type.registry


# ``<`` and ``>`` operator overrides
registry.priority.update([
    (SparseType, CategoricalType),
])
