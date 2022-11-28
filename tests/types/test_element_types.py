from __future__ import annotations
import datetime
import decimal
import itertools

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


# TODO: test all private fields for each ElementType
# -> equiv_float, equiv_complex, min/max, unit, step_size, storage


####################
####    DATA    ####
####################


element_types = {
    # boolean
    "bool": lambda: BooleanType.instance(),
    "nullable[bool]": lambda: BooleanType.instance(nullable=True),

    # integer supertype
    "int": lambda: IntegerType.instance(),
    "nullable[int]": lambda: IntegerType.instance(nullable=True),

    # signed integer supertype
    "signed": lambda: SignedIntegerType.instance(),
    "nullable[signed]": lambda: SignedIntegerType.instance(nullable=True),

    # int8
    "int8": lambda: Int8Type.instance(),
    "nullable[int8]": lambda: Int8Type.instance(nullable=True),

    # int16
    "int16": lambda: Int16Type.instance(),
    "nullable[int16]": lambda: Int16Type.instance(nullable=True),

    # int32
    "int32": lambda: Int32Type.instance(),
    "nullable[int32]": lambda: Int32Type.instance(nullable=True),

    # int64
    "int64": lambda: Int64Type.instance(),
    "nullable[int64]": lambda: Int64Type.instance(nullable=True),

    # unsigned integer supertype
    "unsigned": lambda: UnsignedIntegerType.instance(),
    "nullable[unsigned]": lambda: UnsignedIntegerType.instance(nullable=True),

    # uint8
    "uint8": lambda: UInt8Type.instance(),
    "nullable[uint8]": lambda: UInt8Type.instance(nullable=True),

    # uint16
    "uint16": lambda: UInt16Type.instance(),
    "nullable[uint16]": lambda: UInt16Type.instance(nullable=True),

    # uint32
    "uint32": lambda: UInt32Type.instance(),
    "nullable[uint32]": lambda: UInt32Type.instance(nullable=True),

    # uint64
    "uint64": lambda: UInt64Type.instance(),
    "nullable[uint64]": lambda: UInt64Type.instance(nullable=True),

    # float supertype
    "float": lambda: FloatType.instance(),

    # float16
    "float16": lambda: Float16Type.instance(),

    # float32
    "float32": lambda: Float32Type.instance(),

    # float64
    "float64": lambda: Float64Type.instance(),

    # longdouble
    "longdouble": lambda: LongDoubleType.instance(),

    # complex supertype
    "complex": lambda: ComplexType.instance(),

    # complex64
    "complex64": lambda: Complex64Type.instance(),

    # complex128
    "complex128": lambda: Complex128Type.instance(),

    # clongdouble
    "clongdouble": lambda: CLongDoubleType.instance(),

    # decimal
    "decimal": lambda: DecimalType.instance(),

    # datetime supertype
    "datetime": lambda: DatetimeType.instance(),

    # datetime[pandas]
    "datetime[pandas]": lambda: PandasTimestampType.instance(),

    # datetime[python]
    "datetime[python]": lambda: PyDatetimeType.instance(),

    # datetime[numpy]
    "datetime[numpy]": lambda: NumpyDatetime64Type.instance(),

    # timedelta supertype
    "timedelta": lambda: TimedeltaType.instance(),

    # timedelta[pandas]
    "timedelta[pandas]": lambda: PandasTimedeltaType.instance(),

    # timedelta[python]
    "timedelta[python]": lambda: PyTimedeltaType.instance(),

    # timedelta[numpy]
    "timedelta[numpy]": lambda: NumpyTimedelta64Type.instance(),

    # string
    "string": lambda: StringType.instance(),
    "string[python]": lambda: StringType.instance(storage="python"),

    # object
    # TODO: account for extendable atomic types
}


data_model = {
    # boolean
    "bool": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": bool,
        "numpy_type": np.dtype(bool),
        "pandas_type": pd.BooleanDtype(),
        "slug": "bool",
        "supertype": None,
        "subtypes": frozenset({
            BooleanType.instance(),
            BooleanType.instance(nullable=True)
        })
    },
    "nullable[bool]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": bool,
        "numpy_type": np.dtype(bool),
        "pandas_type": pd.BooleanDtype(),
        "slug": "nullable[bool]",
        "supertype": None,
        "subtypes": frozenset({
            BooleanType.instance(nullable=True)
        })
    },

    # integer supertype
    "int": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": int,
        "numpy_type": np.dtype(np.int64),
        "pandas_type": pd.Int64Dtype(),
        "slug": "int",
        "supertype": None,
        "subtypes": frozenset({
            IntegerType.instance(),
            IntegerType.instance(nullable=True),
            SignedIntegerType.instance(),
            SignedIntegerType.instance(nullable=True),
            Int8Type.instance(),
            Int8Type.instance(nullable=True),
            Int16Type.instance(),
            Int16Type.instance(nullable=True),
            Int32Type.instance(),
            Int32Type.instance(nullable=True),
            Int64Type.instance(),
            Int64Type.instance(nullable=True),
            UnsignedIntegerType.instance(),
            UnsignedIntegerType.instance(nullable=True),
            UInt8Type.instance(),
            UInt8Type.instance(nullable=True),
            UInt16Type.instance(),
            UInt16Type.instance(nullable=True),
            UInt32Type.instance(),
            UInt32Type.instance(nullable=True),
            UInt64Type.instance(),
            UInt64Type.instance(nullable=True),
        })
    },
    "nullable[int]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": int,
        "numpy_type": np.dtype(np.int64),
        "pandas_type": pd.Int64Dtype(),
        "slug": "nullable[int]",
        "supertype": None,
        "subtypes": frozenset({
            IntegerType.instance(nullable=True),
            SignedIntegerType.instance(nullable=True),
            Int8Type.instance(nullable=True),
            Int16Type.instance(nullable=True),
            Int32Type.instance(nullable=True),
            Int64Type.instance(nullable=True),
            UnsignedIntegerType.instance(nullable=True),
            UInt8Type.instance(nullable=True),
            UInt16Type.instance(nullable=True),
            UInt32Type.instance(nullable=True),
            UInt64Type.instance(nullable=True),
        })
    },

    # signed integer supertype
    "signed": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": None,
        "numpy_type": np.dtype(np.int64),
        "pandas_type": pd.Int64Dtype(),
        "slug": "signed int",
        "supertype": IntegerType.instance(),
        "subtypes": frozenset({
            SignedIntegerType.instance(),
            SignedIntegerType.instance(nullable=True),
            Int8Type.instance(),
            Int8Type.instance(nullable=True),
            Int16Type.instance(),
            Int16Type.instance(nullable=True),
            Int32Type.instance(),
            Int32Type.instance(nullable=True),
            Int64Type.instance(),
            Int64Type.instance(nullable=True),
        })
    },
    "nullable[signed]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": None,
        "numpy_type": np.dtype(np.int64),
        "pandas_type": pd.Int64Dtype(),
        "slug": "nullable[signed int]",
        "supertype": IntegerType.instance(nullable=True),
        "subtypes": frozenset({
            SignedIntegerType.instance(nullable=True),
            Int8Type.instance(nullable=True),
            Int16Type.instance(nullable=True),
            Int32Type.instance(nullable=True),
            Int64Type.instance(nullable=True),
        })
    },

    # int8
    "int8": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": np.int8,
        "numpy_type": np.dtype(np.int8),
        "pandas_type": pd.Int8Dtype(),
        "slug": "int8",
        "supertype": SignedIntegerType.instance(),
        "subtypes": frozenset({
            Int8Type.instance(),
            Int8Type.instance(nullable=True),
        })
    },
    "nullable[int8]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.int8,
        "numpy_type": np.dtype(np.int8),
        "pandas_type": pd.Int8Dtype(),
        "slug": "nullable[int8]",
        "supertype": SignedIntegerType.instance(nullable=True),
        "subtypes": frozenset({
            Int8Type.instance(nullable=True),
        })
    },

    # int16
    "int16": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": np.int16,
        "numpy_type": np.dtype(np.int16),
        "pandas_type": pd.Int16Dtype(),
        "slug": "int16",
        "supertype": SignedIntegerType.instance(),
        "subtypes": frozenset({
            Int16Type.instance(),
            Int16Type.instance(nullable=True),
        })
    },
    "nullable[int16]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.int16,
        "numpy_type": np.dtype(np.int16),
        "pandas_type": pd.Int16Dtype(),
        "slug": "nullable[int16]",
        "supertype": SignedIntegerType.instance(nullable=True),
        "subtypes": frozenset({
            Int16Type.instance(nullable=True),
        })
    },

    # int32
    "int32": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": np.int32,
        "numpy_type": np.dtype(np.int32),
        "pandas_type": pd.Int32Dtype(),
        "slug": "int32",
        "supertype": SignedIntegerType.instance(),
        "subtypes": frozenset({
            Int32Type.instance(),
            Int32Type.instance(nullable=True),
        })
    },
    "nullable[int32]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.int32,
        "numpy_type": np.dtype(np.int32),
        "pandas_type": pd.Int32Dtype(),
        "slug": "nullable[int32]",
        "supertype": SignedIntegerType.instance(nullable=True),
        "subtypes": frozenset({
            Int32Type.instance(nullable=True),
        })
    },

    # int64
    "int64": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": np.int64,
        "numpy_type": np.dtype(np.int64),
        "pandas_type": pd.Int64Dtype(),
        "slug": "int64",
        "supertype": SignedIntegerType.instance(),
        "subtypes": frozenset({
            Int64Type.instance(),
            Int64Type.instance(nullable=True),
        })
    },
    "nullable[int64]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.int64,
        "numpy_type": np.dtype(np.int64),
        "pandas_type": pd.Int64Dtype(),
        "slug": "nullable[int64]",
        "supertype": SignedIntegerType.instance(nullable=True),
        "subtypes": frozenset({
            Int64Type.instance(nullable=True),
        })
    },

    # unsigned integer supertype
    "unsigned": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": None,
        "numpy_type": np.dtype(np.uint64),
        "pandas_type": pd.UInt64Dtype(),
        "slug": "unsigned int",
        "supertype": IntegerType.instance(),
        "subtypes": frozenset({
            UnsignedIntegerType.instance(),
            UnsignedIntegerType.instance(nullable=True),
            UInt8Type.instance(),
            UInt8Type.instance(nullable=True),
            UInt16Type.instance(),
            UInt16Type.instance(nullable=True),
            UInt32Type.instance(),
            UInt32Type.instance(nullable=True),
            UInt64Type.instance(),
            UInt64Type.instance(nullable=True),
        })
    },
    "nullable[unsigned]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": None,
        "numpy_type": np.dtype(np.uint64),
        "pandas_type": pd.UInt64Dtype(),
        "slug": "nullable[unsigned int]",
        "supertype": IntegerType.instance(nullable=True),
        "subtypes": frozenset({
            UnsignedIntegerType.instance(nullable=True),
            UInt8Type.instance(nullable=True),
            UInt16Type.instance(nullable=True),
            UInt32Type.instance(nullable=True),
            UInt64Type.instance(nullable=True),
        })
    },

    # uint8
    "uint8": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": np.uint8,
        "numpy_type": np.dtype(np.uint8),
        "pandas_type": pd.UInt8Dtype(),
        "slug": "uint8",
        "supertype": UnsignedIntegerType.instance(),
        "subtypes": frozenset({
            UInt8Type.instance(),
            UInt8Type.instance(nullable=True),
        })
    },
    "nullable[uint8]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.uint8,
        "numpy_type": np.dtype(np.uint8),
        "pandas_type": pd.UInt8Dtype(),
        "slug": "nullable[uint8]",
        "supertype": UnsignedIntegerType.instance(nullable=True),
        "subtypes": frozenset({
            UInt8Type.instance(nullable=True),
        })
    },

    # uint16
    "uint16": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": np.uint16,
        "numpy_type": np.dtype(np.uint16),
        "pandas_type": pd.UInt16Dtype(),
        "slug": "uint16",
        "supertype": UnsignedIntegerType.instance(),
        "subtypes": frozenset({
            UInt16Type.instance(),
            UInt16Type.instance(nullable=True),
        })
    },
    "nullable[uint16]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.uint16,
        "numpy_type": np.dtype(np.uint16),
        "pandas_type": pd.UInt16Dtype(),
        "slug": "nullable[uint16]",
        "supertype": UnsignedIntegerType.instance(nullable=True),
        "subtypes": frozenset({
            UInt16Type.instance(nullable=True),
        })
    },

    # uint32
    "uint32": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": np.uint32,
        "numpy_type": np.dtype(np.uint32),
        "pandas_type": pd.UInt32Dtype(),
        "slug": "uint32",
        "supertype": UnsignedIntegerType.instance(),
        "subtypes": frozenset({
            UInt32Type.instance(),
            UInt32Type.instance(nullable=True),
        })
    },
    "nullable[uint32]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.uint32,
        "numpy_type": np.dtype(np.uint32),
        "pandas_type": pd.UInt32Dtype(),
        "slug": "nullable[uint32]",
        "supertype": UnsignedIntegerType.instance(nullable=True),
        "subtypes": frozenset({
            UInt32Type.instance(nullable=True),
        })
    },

    # uint64
    "uint64": {
        "sparse": False,
        "categorical": False,
        "nullable": False,
        "atomic_type": np.uint64,
        "numpy_type": np.dtype(np.uint64),
        "pandas_type": pd.UInt64Dtype(),
        "slug": "uint64",
        "supertype": UnsignedIntegerType.instance(),
        "subtypes": frozenset({
            UInt64Type.instance(),
            UInt64Type.instance(nullable=True),
        })
    },
    "nullable[uint64]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.uint64,
        "numpy_type": np.dtype(np.uint64),
        "pandas_type": pd.UInt64Dtype(),
        "slug": "nullable[uint64]",
        "supertype": UnsignedIntegerType.instance(nullable=True),
        "subtypes": frozenset({
            UInt64Type.instance(nullable=True),
        })
    },

    # float supertype
    "float": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": float,
        "numpy_type": np.dtype(np.float64),
        "pandas_type": None,
        "slug": "float",
        "supertype": None,
        "subtypes": frozenset({
            FloatType.instance(),
            Float16Type.instance(),
            Float32Type.instance(),
            Float64Type.instance(),
            LongDoubleType.instance(),
        })
    },

    # float16
    "float16": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.float16,
        "numpy_type": np.dtype(np.float16),
        "pandas_type": None,
        "slug": "float16",
        "supertype": FloatType.instance(),
        "subtypes": frozenset({
            Float16Type.instance(),
        })
    },

    # float32
    "float32": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.float32,
        "numpy_type": np.dtype(np.float32),
        "pandas_type": None,
        "slug": "float32",
        "supertype": FloatType.instance(),
        "subtypes": frozenset({
            Float32Type.instance(),
        })
    },

    # float64
    "float64": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.float64,
        "numpy_type": np.dtype(np.float64),
        "pandas_type": None,
        "slug": "float64",
        "supertype": FloatType.instance(),
        "subtypes": frozenset({
            Float64Type.instance(),
        })
    },

    # longdouble
    "longdouble": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.longdouble,
        "numpy_type": np.dtype(np.longdouble),
        "pandas_type": None,
        "slug": "longdouble",
        "supertype": FloatType.instance(),
        "subtypes": frozenset({
            LongDoubleType.instance(),
        })
    },

    # complex supertype
    "complex": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": complex,
        "numpy_type": np.dtype(np.complex128),
        "pandas_type": None,
        "slug": "complex",
        "supertype": None,
        "subtypes": frozenset({
            ComplexType.instance(),
            Complex64Type.instance(),
            Complex128Type.instance(),
            CLongDoubleType.instance(),
        })
    },

    # complex64
    "complex64": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.complex64,
        "numpy_type": np.dtype(np.complex64),
        "pandas_type": None,
        "slug": "complex64",
        "supertype": ComplexType.instance(),
        "subtypes": frozenset({
            Complex64Type.instance(),
        })
    },

    # complex128
    "complex128": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.complex128,
        "numpy_type": np.dtype(np.complex128),
        "pandas_type": None,
        "slug": "complex128",
        "supertype": ComplexType.instance(),
        "subtypes": frozenset({
            Complex128Type.instance(),
        })
    },

    # clongdouble
    "clongdouble": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.clongdouble,
        "numpy_type": np.dtype(np.clongdouble),
        "pandas_type": None,
        "slug": "clongdouble",
        "supertype": ComplexType.instance(),
        "subtypes": frozenset({
            CLongDoubleType.instance(),
        })
    },

    # decimal
    "decimal": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": decimal.Decimal,
        "numpy_type": None,
        "pandas_type": None,
        "slug": "decimal",
        "supertype": None,
        "subtypes": frozenset({
            DecimalType.instance(),
        })
    },

    # datetime supertype
    "datetime": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": None,
        "numpy_type": None,
        "pandas_type": None,
        "slug": "datetime",
        "supertype": None,
        "subtypes": frozenset({
            DatetimeType.instance(),
            PandasTimestampType.instance(),
            PyDatetimeType.instance(),
            NumpyDatetime64Type.instance(),
        })
    },

    # datetime[pandas]
    "datetime[pandas]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": pd.Timestamp,
        "numpy_type": None,
        "pandas_type": None,
        "slug": "datetime[pandas]",
        "supertype": DatetimeType.instance(),
        "subtypes": frozenset({
            PandasTimestampType.instance(),
        })
    },

    # datetime[python]
    "datetime[python]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": datetime.datetime,
        "numpy_type": None,
        "pandas_type": None,
        "slug": "datetime[python]",
        "supertype": DatetimeType.instance(),
        "subtypes": frozenset({
            PyDatetimeType.instance(),
        })
    },

    # datetime[numpy]
    "datetime[numpy]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.datetime64,
        "numpy_type": np.dtype("M8"),
        "pandas_type": None,
        "slug": "M8",
        "supertype": DatetimeType.instance(),
        "subtypes": frozenset({
            NumpyDatetime64Type.instance(),
        })
    },

    # timedelta supertype
    "timedelta": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": None,
        "numpy_type": None,
        "pandas_type": None,
        "slug": "timedelta",
        "supertype": None,
        "subtypes": frozenset({
            TimedeltaType.instance(),
            PandasTimedeltaType.instance(),
            PyTimedeltaType.instance(),
            NumpyTimedelta64Type.instance(),
        })
    },

    # timedelta[pandas]
    "timedelta[pandas]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": pd.Timedelta,
        "numpy_type": None,
        "pandas_type": None,
        "slug": "timedelta[pandas]",
        "supertype": TimedeltaType.instance(),
        "subtypes": frozenset({
            PandasTimedeltaType.instance(),
        })
    },

    # timedelta[python]
    "timedelta[python]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": datetime.timedelta,
        "numpy_type": None,
        "pandas_type": None,
        "slug": "timedelta[python]",
        "supertype": TimedeltaType.instance(),
        "subtypes": frozenset({
            PyTimedeltaType.instance(),
        })
    },

    # timedelta[numpy]
    "timedelta[numpy]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": np.timedelta64,
        "numpy_type": np.dtype("m8"),
        "pandas_type": None,
        "slug": "m8",
        "supertype": TimedeltaType.instance(),
        "subtypes": frozenset({
            NumpyTimedelta64Type.instance(),
        })
    },

    # string
    "string": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": str,
        "numpy_type": np.dtype(str),
        "pandas_type": DEFAULT_STRING_DTYPE,
        "slug": "string",
        "supertype": None,
        "subtypes": frozenset({
            StringType.instance(),
            StringType.instance(storage="python"),
        })
    },
    "string[python]": {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": str,
        "numpy_type": np.dtype(str),
        "pandas_type": pd.StringDtype(storage="python"),
        "slug": "string[python]",
        "supertype": None,
        "subtypes": frozenset({
            StringType.instance(storage="python")
        }),
    },
    # NOTE: pyarrow string type requires pyarrow dependency (handled below)

    # object
    # TODO: account for extendable atomic types

}


if PYARROW_INSTALLED:
    # append pyarrow string type to element_types registry
    element_types["string[pyarrow]"] = lambda: StringType.instance(
        storage="pyarrow"
    )

    # define expected metadata for pyarrow string type
    data_model["string[pyarrow]"] = {
        "sparse": False,
        "categorical": False,
        "nullable": True,
        "atomic_type": str,
        "numpy_type": np.dtype(str),
        "pandas_type": pd.StringDtype(storage="pyarrow"),
        "slug": "string[pyarrow]",
        "supertype": None,
        "subtypes": frozenset({
            StringType.instance(storage="pyarrow")
        }),
    }

    # add pyarrow subtype to string metadata
    data_model["string"]["subtypes"] |= {
        StringType.instance(storage="pyarrow")
    }


#####################
####    TESTS    ####
#####################


@pytest.mark.parametrize("factory", element_types.values())
def test_element_type_instance_constructor_returns_flyweights(factory):
    instance_1 = factory()
    instance_2 = factory()

    assert instance_1 is instance_2


@pytest.mark.parametrize("name, properties", data_model.items())
def test_element_types_have_expected_values(name, properties):
    instance = element_types[name]()

    assert instance.sparse == properties["sparse"]
    assert instance.categorical == properties["categorical"]
    assert instance.nullable == properties["nullable"]
    assert instance.atomic_type == properties["atomic_type"]
    assert instance.numpy_type == properties["numpy_type"]
    assert instance.pandas_type == properties["pandas_type"]
    assert instance.slug == properties["slug"]
    assert instance.supertype == properties["supertype"]
    assert instance.subtypes == properties["subtypes"]
