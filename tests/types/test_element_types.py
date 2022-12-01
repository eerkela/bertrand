from __future__ import annotations
import datetime
import decimal
from types import MappingProxyType

import numpy as np
import pandas as pd
import pytest

from pdtypes.types import (
    BooleanType, IntegerType, SignedIntegerType, Int8Type, Int16Type,
    Int32Type, Int64Type, UnsignedIntegerType, UInt8Type, UInt16Type,
    UInt32Type, UInt64Type, FloatType, Float16Type, Float32Type, Float64Type,
    LongDoubleType, ComplexType, Complex64Type, Complex128Type,
    CLongDoubleType, DecimalType, DatetimeType, PandasTimestampType,
    PyDatetimeType, NumpyDatetime64Type, TimedeltaType, PandasTimedeltaType,
    PyTimedeltaType, NumpyTimedelta64Type, StringType, ObjectType
)


from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED


####################
####    DATA    ####
####################


def generate_slug(base: str, sparse: bool, categorical: bool) -> str:
    if categorical:
        base = f"categorical[{base}]"
    if sparse:
        base = f"sparse[{base}]"
    return base


data_model = {
    # boolean
    "bool": lambda sparse, categorical: {
        "factory": lambda: BooleanType.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": bool,
            "numpy_type": np.dtype(bool),
            "pandas_type": pd.BooleanDtype(),
            "slug": generate_slug("bool", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                BooleanType.instance(sparse=sparse, categorical=categorical),
                BooleanType.instance(sparse=sparse, categorical=categorical, nullable=True)
            })
        },
        "repr": f"BooleanType(sparse={sparse}, categorical={categorical}, nullable=False)",
    },
    "nullable[bool]": lambda sparse, categorical: {
        "factory": lambda: BooleanType.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": bool,
            "numpy_type": np.dtype(bool),
            "pandas_type": pd.BooleanDtype(),
            "slug": generate_slug("nullable[bool]", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                BooleanType.instance(sparse=sparse, categorical=categorical, nullable=True)
            })
        },
        "repr": f"BooleanType(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # integer supertype
    "int": lambda sparse, categorical: {
        "factory": lambda: IntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": int,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "slug": generate_slug("int", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                IntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
                IntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
                SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
                SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
                UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -np.inf,
            "max": np.inf,
        },
        "repr": f"IntegerType(sparse={sparse}, categorical={categorical}, nullable=False)",
    },
    "nullable[int]": lambda sparse, categorical: {
        "factory": lambda: IntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": int,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "slug": generate_slug("nullable[int]", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                IntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
                SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -np.inf,
            "max": np.inf,
        },
        "repr": f"IntegerType(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # signed integer supertype
    "signed": lambda sparse, categorical: {
        "factory": lambda: SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": None,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "slug": generate_slug("signed int", sparse=sparse, categorical=categorical),
            "supertype": IntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
                SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**63,
            "max": 2**63 - 1,
        },
        "repr": f"SignedIntegerType(sparse={sparse}, categorical={categorical}, nullable=False)",
    },
    "nullable[signed]": lambda sparse, categorical: {
        "factory": lambda: SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": None,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "slug": generate_slug("nullable[signed int]", sparse=sparse, categorical=categorical),
            "supertype": IntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**63,
            "max": 2**63 - 1,
        },
        "repr": f"SignedIntegerType(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # int8
    "int8": lambda sparse, categorical: {
        "factory": lambda: Int8Type.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.int8,
            "numpy_type": np.dtype(np.int8),
            "pandas_type": pd.Int8Dtype(),
            "slug": generate_slug("int8", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**7,
            "max": 2**7 - 1,
        },
        "repr": f"Int8Type(sparse={sparse}, categorical={categorical}, nullable=False)"
    },
    "nullable[int8]": lambda sparse, categorical: {
        "factory": lambda: Int8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.int8,
            "numpy_type": np.dtype(np.int8),
            "pandas_type": pd.Int8Dtype(),
            "slug": generate_slug("nullable[int8]", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**7,
            "max": 2**7 - 1,
        },
        "repr": f"Int8Type(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # int16
    "int16": lambda sparse, categorical: {
        "factory": lambda: Int16Type.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.int16,
            "numpy_type": np.dtype(np.int16),
            "pandas_type": pd.Int16Dtype(),
            "slug": generate_slug("int16", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**15,
            "max": 2**15 - 1,
        },
        "repr": f"Int16Type(sparse={sparse}, categorical={categorical}, nullable=False)",
    },
    "nullable[int16]": lambda sparse, categorical: {
        "factory": lambda: Int16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.int16,
            "numpy_type": np.dtype(np.int16),
            "pandas_type": pd.Int16Dtype(),
            "slug": generate_slug("nullable[int16]", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**15,
            "max": 2**15 - 1,
        },
        "repr": f"Int16Type(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # int32
    "int32": lambda sparse, categorical: {
        "factory": lambda: Int32Type.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.int32,
            "numpy_type": np.dtype(np.int32),
            "pandas_type": pd.Int32Dtype(),
            "slug": generate_slug("int32", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**31,
            "max": 2**31 - 1,
        },
        "repr": f"Int32Type(sparse={sparse}, categorical={categorical}, nullable=False)",
    },
    "nullable[int32]": lambda sparse, categorical: {
        "factory": lambda: Int32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.int32,
            "numpy_type": np.dtype(np.int32),
            "pandas_type": pd.Int32Dtype(),
            "slug": generate_slug("nullable[int32]", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**31,
            "max": 2**31 - 1,
        },
        "repr": f"Int32Type(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # int64
    "int64": lambda sparse, categorical: {
        "factory": lambda: Int64Type.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.int64,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "slug": generate_slug("int64", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**63,
            "max": 2**63 - 1,
        },
        "repr": f"Int64Type(sparse={sparse}, categorical={categorical}, nullable=False)",
    },
    "nullable[int64]": lambda sparse, categorical: {
        "factory": lambda: Int64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.int64,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "slug": generate_slug("nullable[int64]", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**63,
            "max": 2**63 - 1,
        },
        "repr": f"Int64Type(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # unsigned integer supertype
    "unsigned": lambda sparse, categorical: {
        "factory": lambda: UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": None,
            "numpy_type": np.dtype(np.uint64),
            "pandas_type": pd.UInt64Dtype(),
            "slug": generate_slug("unsigned int", sparse=sparse, categorical=categorical),
            "supertype": IntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
                UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**64 - 1,
        },
        "repr": f"UnsignedIntegerType(sparse={sparse}, categorical={categorical}, nullable=False)",
    },
    "nullable[unsigned]": lambda sparse, categorical: {
        "factory": lambda: UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": None,
            "numpy_type": np.dtype(np.uint64),
            "pandas_type": pd.UInt64Dtype(),
            "slug": generate_slug("nullable[unsigned int]", sparse=sparse, categorical=categorical),
            "supertype": IntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**64 - 1,
        },
        "repr": f"UnsignedIntegerType(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # uint8
    "uint8": lambda sparse, categorical: {
        "factory": lambda: UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.uint8,
            "numpy_type": np.dtype(np.uint8),
            "pandas_type": pd.UInt8Dtype(),
            "slug": generate_slug("uint8", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**8 - 1,
        },
        "repr": f"UInt8Type(sparse={sparse}, categorical={categorical}, nullable=False)",
    },
    "nullable[uint8]": lambda sparse, categorical: {
        "factory": lambda: UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.uint8,
            "numpy_type": np.dtype(np.uint8),
            "pandas_type": pd.UInt8Dtype(),
            "slug": generate_slug("nullable[uint8]", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**8 - 1,
        },
        "repr": f"UInt8Type(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # uint16
    "uint16": lambda sparse, categorical: {
        "factory": lambda: UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.uint16,
            "numpy_type": np.dtype(np.uint16),
            "pandas_type": pd.UInt16Dtype(),
            "slug": generate_slug("uint16", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**16 - 1,
        },
        "repr": f"UInt16Type(sparse={sparse}, categorical={categorical}, nullable=False)",
    },
    "nullable[uint16]": lambda sparse, categorical: {
        "factory": lambda: UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.uint16,
            "numpy_type": np.dtype(np.uint16),
            "pandas_type": pd.UInt16Dtype(),
            "slug": generate_slug("nullable[uint16]", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**16 - 1,
        },
        "repr": f"UInt16Type(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # uint32
    "uint32": lambda sparse, categorical: {
        "factory": lambda: UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.uint32,
            "numpy_type": np.dtype(np.uint32),
            "pandas_type": pd.UInt32Dtype(),
            "slug": generate_slug("uint32", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**32 - 1,
        },
        "repr": f"UInt32Type(sparse={sparse}, categorical={categorical}, nullable=False)",
    },
    "nullable[uint32]": lambda sparse, categorical: {
        "factory": lambda: UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.uint32,
            "numpy_type": np.dtype(np.uint32),
            "pandas_type": pd.UInt32Dtype(),
            "slug": generate_slug("nullable[uint32]", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**32 - 1,
        },
        "repr": f"UInt32Type(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # uint64
    "uint64": lambda sparse, categorical: {
        "factory": lambda: UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=False),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.uint64,
            "numpy_type": np.dtype(np.uint64),
            "pandas_type": pd.UInt64Dtype(),
            "slug": generate_slug("uint64", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**64 - 1,
        },
        "repr": f"UInt64Type(sparse={sparse}, categorical={categorical}, nullable=False)",
    },
    "nullable[uint64]": lambda sparse, categorical: {
        "factory": lambda: UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.uint64,
            "numpy_type": np.dtype(np.uint64),
            "pandas_type": pd.UInt64Dtype(),
            "slug": generate_slug("nullable[uint64]", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**64 - 1,
        },
        "repr": f"UInt64Type(sparse={sparse}, categorical={categorical}, nullable=True)",
    },

    # float supertype
    "float": lambda sparse, categorical: {
        "factory": lambda: FloatType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": float,
            "numpy_type": np.dtype(np.float64),
            "pandas_type": None,
            "slug": generate_slug("float", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                FloatType.instance(sparse=sparse, categorical=categorical),
                Float16Type.instance(sparse=sparse, categorical=categorical),
                Float32Type.instance(sparse=sparse, categorical=categorical),
                Float64Type.instance(sparse=sparse, categorical=categorical),
                LongDoubleType.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_complex": ComplexType.instance(sparse=sparse, categorical=categorical),
            "min": -2**53,
            "max": 2**53,
        },
        "repr": f"FloatType(sparse={sparse}, categorical={categorical})",
    },

    # float16
    "float16": lambda sparse, categorical: {
        "factory": lambda: Float16Type.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.float16,
            "numpy_type": np.dtype(np.float16),
            "pandas_type": None,
            "slug": generate_slug("float16", sparse=sparse, categorical=categorical),
            "supertype": FloatType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                Float16Type.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_complex": Complex64Type.instance(sparse=sparse, categorical=categorical),
            "min": -2**11,
            "max": 2**11,
        },
        "repr": f"Float16Type(sparse={sparse}, categorical={categorical})",
    },

    # float32
    "float32": lambda sparse, categorical: {
        "factory": lambda: Float32Type.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.float32,
            "numpy_type": np.dtype(np.float32),
            "pandas_type": None,
            "slug": generate_slug("float32", sparse=sparse, categorical=categorical),
            "supertype": FloatType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                Float32Type.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_complex": Complex64Type.instance(sparse=sparse, categorical=categorical),
            "min": -2**24,
            "max": 2**24,
        },
        "repr": f"Float32Type(sparse={sparse}, categorical={categorical})",
    },

    # float64
    "float64": lambda sparse, categorical: {
        "factory": lambda: Float64Type.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.float64,
            "numpy_type": np.dtype(np.float64),
            "pandas_type": None,
            "slug": generate_slug("float64", sparse=sparse, categorical=categorical),
            "supertype": FloatType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                Float64Type.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_complex": Complex128Type.instance(sparse=sparse, categorical=categorical),
            "min": -2**53,
            "max": 2**53,
        },
        "repr": f"Float64Type(sparse={sparse}, categorical={categorical})"
    },

    # longdouble
    "longdouble": lambda sparse, categorical: {
        "factory": lambda: LongDoubleType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.longdouble,
            "numpy_type": np.dtype(np.longdouble),
            "pandas_type": None,
            "slug": generate_slug("longdouble", sparse=sparse, categorical=categorical),
            "supertype": FloatType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                LongDoubleType.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_complex": CLongDoubleType.instance(sparse=sparse, categorical=categorical),
            "min": -2**64,
            "max": 2**64,
        },
        "repr": f"LongDoubleType(sparse={sparse}, categorical={categorical})",
    },

    # complex supertype
    "complex": lambda sparse, categorical: {
        "factory": lambda: ComplexType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": complex,
            "numpy_type": np.dtype(np.complex128),
            "pandas_type": None,
            "slug": generate_slug("complex", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                ComplexType.instance(sparse=sparse, categorical=categorical),
                Complex64Type.instance(sparse=sparse, categorical=categorical),
                Complex128Type.instance(sparse=sparse, categorical=categorical),
                CLongDoubleType.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_float": FloatType.instance(sparse=sparse, categorical=categorical),
            "min": -2**53,
            "max": 2**53,
        },
        "repr": f"ComplexType(sparse={sparse}, categorical={categorical})"
    },

    # complex64
    "complex64": lambda sparse, categorical: {
        "factory": lambda: Complex64Type.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.complex64,
            "numpy_type": np.dtype(np.complex64),
            "pandas_type": None,
            "slug": generate_slug("complex64", sparse=sparse, categorical=categorical),
            "supertype": ComplexType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                Complex64Type.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_float": Float32Type.instance(sparse=sparse, categorical=categorical),
            "min": -2**24,
            "max": 2**24,
        },
        "repr": f"Complex64Type(sparse={sparse}, categorical={categorical})",
    },

    # complex128
    "complex128": lambda sparse, categorical: {
        "factory": lambda: Complex128Type.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.complex128,
            "numpy_type": np.dtype(np.complex128),
            "pandas_type": None,
            "slug": generate_slug("complex128", sparse=sparse, categorical=categorical),
            "supertype": ComplexType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                Complex128Type.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_float": Float64Type.instance(sparse=sparse, categorical=categorical),
            "min": -2**53,
            "max": 2**53,
        },
        "repr": f"Complex128Type(sparse={sparse}, categorical={categorical})",
    },

    # clongdouble
    "clongdouble": lambda sparse, categorical: {
        "factory": lambda: CLongDoubleType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.clongdouble,
            "numpy_type": np.dtype(np.clongdouble),
            "pandas_type": None,
            "slug": generate_slug("clongdouble", sparse=sparse, categorical=categorical),
            "supertype": ComplexType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                CLongDoubleType.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_float": LongDoubleType.instance(sparse=sparse, categorical=categorical),
            "min": -2**64,
            "max": 2**64,
        },
        "repr": f"CLongDoubleType(sparse={sparse}, categorical={categorical})",
    },

    # decimal
    "decimal": lambda sparse, categorical: {
        "factory": lambda: DecimalType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": decimal.Decimal,
            "numpy_type": None,
            "pandas_type": None,
            "slug": generate_slug("decimal", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                DecimalType.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -np.inf,
            "max": np.inf,
        },
        "repr": f"DecimalType(sparse={sparse}, categorical={categorical})",
    },

    # datetime supertype
    "datetime": lambda sparse, categorical: {
        "factory": lambda: DatetimeType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": None,
            "numpy_type": None,
            "pandas_type": None,
            "slug": generate_slug("datetime", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                DatetimeType.instance(sparse=sparse, categorical=categorical),
                PandasTimestampType.instance(sparse=sparse, categorical=categorical),
                PyDatetimeType.instance(sparse=sparse, categorical=categorical),
                NumpyDatetime64Type.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -291061508645168391112243200000000000,
            "max": 291061508645168328945024000000000000,
        },
        "repr": f"DatetimeType(sparse={sparse}, categorical={categorical})",
    },

    # datetime[pandas]
    "datetime[pandas]": lambda sparse, categorical: {
        "factory": lambda: PandasTimestampType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": pd.Timestamp,
            "numpy_type": None,
            "pandas_type": None,
            "slug": generate_slug("datetime[pandas]", sparse=sparse, categorical=categorical),
            "supertype": DatetimeType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                PandasTimestampType.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -2**63 + 1,
            "max": 2**63 - 1,
        },
        "repr": f"PandasTimestampType(sparse={sparse}, categorical={categorical})",
    },

    # datetime[python]
    "datetime[python]": lambda sparse, categorical: {
        "factory": lambda: PyDatetimeType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": datetime.datetime,
            "numpy_type": None,
            "pandas_type": None,
            "slug": generate_slug("datetime[python]", sparse=sparse, categorical=categorical),
            "supertype": DatetimeType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                PyDatetimeType.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -62135596800000000000,
            "max": 253402300799999999000,
        },
        "repr": f"PyDatetimeType(sparse={sparse}, categorical={categorical})",
    },

    # datetime[numpy]
    # TODO: test with non-default units, step_size
    "datetime[numpy]": lambda sparse, categorical: {
        "factory": lambda: NumpyDatetime64Type.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.datetime64,
            "numpy_type": np.dtype("M8"),
            "pandas_type": None,
            "slug": generate_slug("M8", sparse=sparse, categorical=categorical),
            "supertype": DatetimeType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                NumpyDatetime64Type.instance(sparse=sparse, categorical=categorical),
            }),
            "unit": None,
            "step_size": 1,
            "min": -291061508645168391112243200000000000,
            "max": 291061508645168328945024000000000000,
        },
        "repr": f"NumpyDatetime64Type(unit=None, step_size=1, sparse={sparse}, categorical={categorical})",
    },

    # timedelta supertype
    "timedelta": lambda sparse, categorical: {
        "factory": lambda: TimedeltaType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": None,
            "numpy_type": None,
            "pandas_type": None,
            "slug": generate_slug("timedelta", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                TimedeltaType.instance(sparse=sparse, categorical=categorical),
                PandasTimedeltaType.instance(sparse=sparse, categorical=categorical),
                PyTimedeltaType.instance(sparse=sparse, categorical=categorical),
                NumpyTimedelta64Type.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -291061508645168391112156800000000000,
            "max": 291061508645168391112243200000000000,
        },
        "repr": f"TimedeltaType(sparse={sparse}, categorical={categorical})",
    },

    # timedelta[pandas]
    "timedelta[pandas]": lambda sparse, categorical: {
        "factory": lambda: PandasTimedeltaType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": pd.Timedelta,
            "numpy_type": None,
            "pandas_type": None,
            "slug": generate_slug("timedelta[pandas]", sparse=sparse, categorical=categorical),
            "supertype": TimedeltaType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                PandasTimedeltaType.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -2**63 + 1,
            "max": 2**63 - 1,
        },
        "repr": f"PandasTimedeltaType(sparse={sparse}, categorical={categorical})",
    },

    # timedelta[python]
    "timedelta[python]": lambda sparse, categorical: {
        "factory": lambda: PyTimedeltaType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": datetime.timedelta,
            "numpy_type": None,
            "pandas_type": None,
            "slug": generate_slug("timedelta[python]", sparse=sparse, categorical=categorical),
            "supertype": TimedeltaType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                PyTimedeltaType.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -86399999913600000000000,
            "max": 86399999999999999999000,
        },
        "repr": f"PyTimedeltaType(sparse={sparse}, categorical={categorical})",
    },

    # timedelta[numpy]
    # TODO: test with non-default units, step_size
    "timedelta[numpy]": lambda sparse, categorical: {
        "factory": lambda: NumpyTimedelta64Type.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.timedelta64,
            "numpy_type": np.dtype("m8"),
            "pandas_type": None,
            "slug": generate_slug("m8", sparse=sparse, categorical=categorical),
            "supertype": TimedeltaType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                NumpyTimedelta64Type.instance(sparse=sparse, categorical=categorical),
            }),
            "unit": None,
            "step_size": 1,
            "min": -291061508645168391112243200000000000,
            "max": 291061508645168328945024000000000000,
        },
        "repr": f"NumpyTimedelta64Type(unit=None, step_size=1, sparse={sparse}, categorical={categorical})",
    },

    # string
    "string": lambda sparse, categorical: {
        "factory": lambda: StringType.instance(sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": str,
            "numpy_type": np.dtype(str),
            "pandas_type": DEFAULT_STRING_DTYPE,
            "slug": generate_slug("string", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                StringType.instance(sparse=sparse, categorical=categorical),
                StringType.instance(storage="python", sparse=sparse, categorical=categorical),
            } | (
                {StringType.instance(storage="pyarrow", sparse=sparse, categorical=categorical)}
                if PYARROW_INSTALLED else {}
            )),
            "is_default": True,
            "storage": DEFAULT_STRING_DTYPE.storage,
        },
        "repr": f"StringType(storage=None, sparse={sparse}, categorical={categorical})",
    },
    "string[python]": lambda sparse, categorical: {
        "factory": lambda: StringType.instance(storage="python", sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": str,
            "numpy_type": np.dtype(str),
            "pandas_type": pd.StringDtype(storage="python"),
            "slug": generate_slug("string[python]", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                StringType.instance(storage="python", sparse=sparse, categorical=categorical)
            }),
            "is_default": False,
            "storage": "python",
        },
        "repr": f"StringType(storage='python', sparse={sparse}, categorical={categorical})",
    },
    # NOTE: pyarrow string type requires pyarrow dependency (handled below)

    # object
    # TODO: account for extendable atomic types

}


# if pyarrow is installed, add expected metadata for corresponding string type
if PYARROW_INSTALLED:
    data_model["string[pyarrow]"] = lambda sparse, categorical: {
        "factory": lambda: StringType.instance(storage="pyarrow", sparse=sparse, categorical=categorical),
        "properties": {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": str,
            "numpy_type": np.dtype(str),
            "pandas_type": pd.StringDtype(storage="pyarrow"),
            "slug": generate_slug("string[pyarrow]", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                StringType.instance(storage="pyarrow", sparse=sparse, categorical=categorical)
            }),
            "is_default": False,
            "storage": "pyarrow",
        },
        "repr": f"StringType(storage='pyarrow', sparse={sparse}, categorical={categorical})",
    }


# NOTE: MappingProxyType is immutable - prevents test-related side effects
data_model = MappingProxyType(data_model)


#####################
####    TESTS    ####
#####################


# NOTE: stacking pytest.mark.parametrize() calls like this produces a
# Cartesian product of the input arguments.


@pytest.mark.parametrize("sparse", [True, False])
@pytest.mark.parametrize("categorical", [True, False])
@pytest.mark.parametrize("model", data_model.values())
def test_element_type_instance_constructor_returns_flyweights(
    sparse, categorical, model
):
    # refine model by sparse/categorical
    model = model(sparse=sparse, categorical=categorical)

    # call factory twice
    instance_1 = model["factory"]()
    instance_2 = model["factory"]()

    # assert results are the same object
    assert instance_1 is instance_2


@pytest.mark.parametrize("sparse", [True, False])
@pytest.mark.parametrize("categorical", [True, False])
@pytest.mark.parametrize("model", data_model.values())
def test_element_type_attributes_fit_data_model(
    sparse, categorical, model
):
    # refine model by sparse/categorical and call factory
    model = model(sparse=sparse, categorical=categorical)
    instance = model["factory"]()

    # check .attribute accessors
    for k, v in model["properties"].items():
        assert getattr(instance, k) == v

    # check str, repr, hash
    assert str(instance) == model["properties"]["slug"]
    assert repr(instance) == model["repr"]
    assert hash(instance) == hash(model["properties"]["slug"])


@pytest.mark.parametrize("sparse", [True, False])
@pytest.mark.parametrize("categorical", [True, False])
@pytest.mark.parametrize("model", data_model.values())
def test_element_type_attributes_are_immutable(
    sparse, categorical, model
):
    # refine model by sparse/categorical and call factory
    model = model(sparse=sparse, categorical=categorical)
    instance = model["factory"]()

    # attempt to reassign every .attribute accessor
    for k in model["properties"]:
        with pytest.raises(AttributeError):
            setattr(instance, k, False)


# TODO: test_element_types_contain_only_valid_subtypes
# TODO: test_element_types_compare_equal

