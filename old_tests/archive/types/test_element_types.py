"""Unit tests for all variants of `pdtypes.types.ElementType`, as returned by
`get_dtype()`/`resolve_dtype()`.
"""
from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytest

from tests.scheme import _TestCase, all_subclasses, Parameters, parametrize

from pdtypes.types import (
    ElementType, BooleanType, IntegerType, SignedIntegerType, Int8Type,
    Int16Type, Int32Type, Int64Type, UnsignedIntegerType, UInt8Type,
    UInt16Type, UInt32Type, UInt64Type, FloatType, Float16Type, Float32Type,
    Float64Type, LongDoubleType, ComplexType, Complex64Type, Complex128Type,
    CLongDoubleType, DecimalType, DatetimeType, PandasTimestampType,
    PyDatetimeType, NumpyDatetime64Type, TimedeltaType, PandasTimedeltaType,
    PyTimedeltaType, NumpyTimedelta64Type, StringType, ObjectType
)

from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED


##############################
####    CASE STRUCTURE    ####
##############################


class ElementTypeCase(_TestCase):
    """A subclass of `_TestCase` that defines the expected structure of test
    cases related to `pdtypes.types.ElementType` functionality.

    These cases enforce the following structure:
        -   `kwargs: dict`
            -   Keyword arguments to `ElementType.instance()`.  Must include
                values for `sparse` and `categorical` at minimum.
        -   `test_input: type`
            -   A subclass of `ElementType`, whose properties will be tested.
                Subclasses are discovered at runtime and automatically include
                every variation of ElementType that is present within the
                production codebase.
        -   `test_output: dict`
            -   Expected values for every property of the given `ElementType`
                subclass. As with `test_input`, property names are discovered
                at runtime and include any non-callable attribute of
                `test_input` that is not prepended by an underscore.
    """

    def __init__(
        self,
        kwargs: dict,
        test_input: type,
        test_output: dict,
        name: str = None,
        id: str = None,
        marks: tuple = tuple()
    ):
        super().__init__(
            kwargs=kwargs,
            test_input=test_input,
            test_output=test_output,
            input_type=type,
            output_type=dict,
            name=name,
            id=id,
            marks=marks
        )
        # kwargs must contain values for sparse, categorical at minimum
        # NOTE: since test_input is a cython object, we cannot inspect its
        # signature directly.
        if not all(key in self.kwargs for key in ("sparse", "categorical")):
            raise SyntaxError(
                f"`kwargs` must contain 'sparse' and 'categorical' keys"
            )

        # test_input must be a valid subclass of ElementType (but not
        # ElementType itself)
        if self.input not in all_subclasses(ElementType):
            raise SyntaxError(
                f"`test_input` must be a recognized subclass of ElementType, "
                f"not {type(self.input)}"
            )

        # test_output must define values for every property of test_input
        required_properties = [x for x in dir(self.input) if (
            not callable(getattr(self.input, x)) and not x.startswith("_")
        )]
        if not all(prop in self.output for prop in required_properties):
            missing_properties = [
                prop for prop in required_properties if prop not in self.output
            ]
            raise SyntaxError(
                f"`test_output` must define expected values for every "
                f"property of {self.input.__name__}({self.signature()}) "
                f"(missing {missing_properties})"
            )

    def instance(self) -> ElementType:
        """Generate an instance of `test_input` using the `kwargs` that are
        attached to this test case.
        """
        return self.input.instance(**self.kwargs)


####################
####    DATA    ####
####################


class DummyClass:
    pass


def generate_slug(base: str, sparse: bool, categorical: bool) -> str:
    if categorical:
        base = f"categorical[{base}]"
    if sparse:
        base = f"sparse[{base}]"
    return base


# define model factory, ignoring permutations of sparse, categorical flags
data_model = {
    # NOTE: keys aren't used, they're included for documentation purposes.

    # boolean
    "bool": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        BooleanType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": bool,
            "numpy_type": np.dtype(bool),
            "pandas_type": pd.BooleanDtype(),
            "na_value": pd.NA,
            "itemsize": 1,
            "slug": generate_slug("bool", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                BooleanType.instance(sparse=sparse, categorical=categorical),
                BooleanType.instance(sparse=sparse, categorical=categorical, nullable=True)
            })
        }
    ),
    "nullable[bool]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        BooleanType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": bool,
            "numpy_type": np.dtype(bool),
            "pandas_type": pd.BooleanDtype(),
            "na_value": pd.NA,
            "itemsize": 1,
            "slug": generate_slug("nullable[bool]", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                BooleanType.instance(sparse=sparse, categorical=categorical, nullable=True)
            })
        }
    ),

    # integer supertype
    "int": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        IntegerType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": int,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "na_value": pd.NA,
            "itemsize": None,
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
        }
    ),
    "nullable[int]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        IntegerType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": int,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "na_value": pd.NA,
            "itemsize": None,
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
        }
    ),

    # signed integer supertype
    "signed": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        SignedIntegerType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": None,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "na_value": pd.NA,
            "itemsize": None,
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
        }
    ),
    "nullable[signed]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        SignedIntegerType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": None,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "na_value": pd.NA,
            "itemsize": None,
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
        }
    ),

    # int8
    "int8": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        Int8Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.int8,
            "numpy_type": np.dtype(np.int8),
            "pandas_type": pd.Int8Dtype(),
            "na_value": pd.NA,
            "itemsize": 1,
            "slug": generate_slug("int8", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**7,
            "max": 2**7 - 1,
        }
    ),
    "nullable[int8]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        Int8Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.int8,
            "numpy_type": np.dtype(np.int8),
            "pandas_type": pd.Int8Dtype(),
            "na_value": pd.NA,
            "itemsize": 1,
            "slug": generate_slug("nullable[int8]", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                Int8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**7,
            "max": 2**7 - 1,
        }
    ),

    # int16
    "int16": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        Int16Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.int16,
            "numpy_type": np.dtype(np.int16),
            "pandas_type": pd.Int16Dtype(),
            "na_value": pd.NA,
            "itemsize": 2,
            "slug": generate_slug("int16", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**15,
            "max": 2**15 - 1,
        }
    ),
    "nullable[int16]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        Int16Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.int16,
            "numpy_type": np.dtype(np.int16),
            "pandas_type": pd.Int16Dtype(),
            "na_value": pd.NA,
            "itemsize": 2,
            "slug": generate_slug("nullable[int16]", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                Int16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**15,
            "max": 2**15 - 1,
        }
    ),

    # int32
    "int32": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        Int32Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.int32,
            "numpy_type": np.dtype(np.int32),
            "pandas_type": pd.Int32Dtype(),
            "na_value": pd.NA,
            "itemsize": 4,
            "slug": generate_slug("int32", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**31,
            "max": 2**31 - 1,
        }
    ),
    "nullable[int32]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        Int32Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.int32,
            "numpy_type": np.dtype(np.int32),
            "pandas_type": pd.Int32Dtype(),
            "na_value": pd.NA,
            "itemsize": 4,
            "slug": generate_slug("nullable[int32]", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                Int32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**31,
            "max": 2**31 - 1,
        }
    ),

    # int64
    "int64": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        Int64Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.int64,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "na_value": pd.NA,
            "itemsize": 8,
            "slug": generate_slug("int64", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**63,
            "max": 2**63 - 1,
        }
    ),
    "nullable[int64]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        Int64Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.int64,
            "numpy_type": np.dtype(np.int64),
            "pandas_type": pd.Int64Dtype(),
            "na_value": pd.NA,
            "itemsize": 8,
            "slug": generate_slug("nullable[int64]", sparse=sparse, categorical=categorical),
            "supertype": SignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                Int64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": -2**63,
            "max": 2**63 - 1,
        }
    ),

    # unsigned integer supertype
    "unsigned": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        UnsignedIntegerType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": None,
            "numpy_type": np.dtype(np.uint64),
            "pandas_type": pd.UInt64Dtype(),
            "na_value": pd.NA,
            "itemsize": None,
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
        }
    ),
    "nullable[unsigned]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        UnsignedIntegerType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": None,
            "numpy_type": np.dtype(np.uint64),
            "pandas_type": pd.UInt64Dtype(),
            "na_value": pd.NA,
            "itemsize": None,
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
        }
    ),

    # uint8
    "uint8": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        UInt8Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.uint8,
            "numpy_type": np.dtype(np.uint8),
            "pandas_type": pd.UInt8Dtype(),
            "na_value": pd.NA,
            "itemsize": 1,
            "slug": generate_slug("uint8", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**8 - 1,
        }
    ),
    "nullable[uint8]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        UInt8Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.uint8,
            "numpy_type": np.dtype(np.uint8),
            "pandas_type": pd.UInt8Dtype(),
            "na_value": pd.NA,
            "itemsize": 1,
            "slug": generate_slug("nullable[uint8]", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                UInt8Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**8 - 1,
        }
    ),

    # uint16
    "uint16": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        UInt16Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.uint16,
            "numpy_type": np.dtype(np.uint16),
            "pandas_type": pd.UInt16Dtype(),
            "na_value": pd.NA,
            "itemsize": 2,
            "slug": generate_slug("uint16", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**16 - 1,
        }
    ),
    "nullable[uint16]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        UInt16Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.uint16,
            "numpy_type": np.dtype(np.uint16),
            "pandas_type": pd.UInt16Dtype(),
            "na_value": pd.NA,
            "itemsize": 2,
            "slug": generate_slug("nullable[uint16]", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                UInt16Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**16 - 1,
        }
    ),

    # uint32
    "uint32": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        UInt32Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.uint32,
            "numpy_type": np.dtype(np.uint32),
            "pandas_type": pd.UInt32Dtype(),
            "na_value": pd.NA,
            "itemsize": 4,
            "slug": generate_slug("uint32", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**32 - 1,
        }
    ),
    "nullable[uint32]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        UInt32Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.uint32,
            "numpy_type": np.dtype(np.uint32),
            "pandas_type": pd.UInt32Dtype(),
            "na_value": pd.NA,
            "itemsize": 4,
            "slug": generate_slug("nullable[uint32]", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                UInt32Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**32 - 1,
        }
    ),

    # uint64
    "uint64": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": False},
        UInt64Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": False,
            "atomic_type": np.uint64,
            "numpy_type": np.dtype(np.uint64),
            "pandas_type": pd.UInt64Dtype(),
            "na_value": pd.NA,
            "itemsize": 8,
            "slug": generate_slug("uint64", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=False),
            "subtypes": frozenset({
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=False),
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**64 - 1,
        }
    ),
    "nullable[uint64]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical, "nullable": True},
        UInt64Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.uint64,
            "numpy_type": np.dtype(np.uint64),
            "pandas_type": pd.UInt64Dtype(),
            "na_value": pd.NA,
            "itemsize": 8,
            "slug": generate_slug("nullable[uint64]", sparse=sparse, categorical=categorical),
            "supertype": UnsignedIntegerType.instance(sparse=sparse, categorical=categorical, nullable=True),
            "subtypes": frozenset({
                UInt64Type.instance(sparse=sparse, categorical=categorical, nullable=True),
            }),
            "min": 0,
            "max": 2**64 - 1,
        }
    ),

    # float supertype
    "float": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        FloatType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": float,
            "numpy_type": np.dtype(np.float64),
            "pandas_type": None,
            "na_value": np.nan,
            "itemsize": 8,
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
        }
    ),

    # float16
    "float16": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        Float16Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.float16,
            "numpy_type": np.dtype(np.float16),
            "pandas_type": None,
            "na_value": np.nan,
            "itemsize": 2,
            "slug": generate_slug("float16", sparse=sparse, categorical=categorical),
            "supertype": FloatType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                Float16Type.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_complex": Complex64Type.instance(sparse=sparse, categorical=categorical),
            "min": -2**11,
            "max": 2**11,
        }
    ),

    # float32
    "float32": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        Float32Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.float32,
            "numpy_type": np.dtype(np.float32),
            "pandas_type": None,
            "na_value": np.nan,
            "itemsize": 4,
            "slug": generate_slug("float32", sparse=sparse, categorical=categorical),
            "supertype": FloatType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                Float32Type.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_complex": Complex64Type.instance(sparse=sparse, categorical=categorical),
            "min": -2**24,
            "max": 2**24,
        }
    ),

    # float64
    "float64": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        Float64Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.float64,
            "numpy_type": np.dtype(np.float64),
            "pandas_type": None,
            "na_value": np.nan,
            "itemsize": 8,
            "slug": generate_slug("float64", sparse=sparse, categorical=categorical),
            "supertype": FloatType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                Float64Type.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_complex": Complex128Type.instance(sparse=sparse, categorical=categorical),
            "min": -2**53,
            "max": 2**53,
        }
    ),

    # longdouble
    "longdouble": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        LongDoubleType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.longdouble,
            "numpy_type": np.dtype(np.longdouble),
            "pandas_type": None,
            "na_value": np.nan,
            "itemsize": np.dtype(np.longdouble).itemsize,  # platform-specific
            "slug": generate_slug("longdouble", sparse=sparse, categorical=categorical),
            "supertype": FloatType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                LongDoubleType.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_complex": CLongDoubleType.instance(sparse=sparse, categorical=categorical),
            "min": -2**64,
            "max": 2**64,
        }
    ),

    # complex supertype
    "complex": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        ComplexType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": complex,
            "numpy_type": np.dtype(np.complex128),
            "pandas_type": None,
            "na_value": complex("nan+nanj"),
            "itemsize": 16,
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
        }
    ),

    # complex64
    "complex64": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        Complex64Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.complex64,
            "numpy_type": np.dtype(np.complex64),
            "pandas_type": None,
            "na_value": complex("nan+nanj"),
            "itemsize": 8,
            "slug": generate_slug("complex64", sparse=sparse, categorical=categorical),
            "supertype": ComplexType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                Complex64Type.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_float": Float32Type.instance(sparse=sparse, categorical=categorical),
            "min": -2**24,
            "max": 2**24,
        }
    ),

    # complex128
    "complex128": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        Complex128Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.complex128,
            "numpy_type": np.dtype(np.complex128),
            "pandas_type": None,
            "na_value": complex("nan+nanj"),
            "itemsize": 16,
            "slug": generate_slug("complex128", sparse=sparse, categorical=categorical),
            "supertype": ComplexType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                Complex128Type.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_float": Float64Type.instance(sparse=sparse, categorical=categorical),
            "min": -2**53,
            "max": 2**53,
        }
    ),

    # clongdouble
    "clongdouble": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        CLongDoubleType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.clongdouble,
            "numpy_type": np.dtype(np.clongdouble),
            "pandas_type": None,
            "na_value": complex("nan+nanj"),
            "itemsize": np.dtype(np.clongdouble).itemsize,  # platform-specific
            "slug": generate_slug("clongdouble", sparse=sparse, categorical=categorical),
            "supertype": ComplexType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                CLongDoubleType.instance(sparse=sparse, categorical=categorical),
            }),
            "equiv_float": LongDoubleType.instance(sparse=sparse, categorical=categorical),
            "min": -2**64,
            "max": 2**64,
        }
    ),

    # decimal
    "decimal": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        DecimalType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": decimal.Decimal,
            "numpy_type": None,
            "pandas_type": None,
            "na_value": pd.NA,
            "itemsize": None,
            "slug": generate_slug("decimal", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                DecimalType.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -np.inf,
            "max": np.inf,
        }
    ),

    # datetime supertype
    "datetime": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        DatetimeType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": None,
            "numpy_type": None,
            "pandas_type": None,
            "na_value": pd.NaT,
            "itemsize": None,
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
        }
    ),

    # datetime[pandas]
    "datetime[pandas]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        PandasTimestampType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": pd.Timestamp,
            "numpy_type": None,
            "pandas_type": None,
            "na_value": pd.NaT,
            "itemsize": None,
            "slug": generate_slug("datetime[pandas]", sparse=sparse, categorical=categorical),
            "supertype": DatetimeType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                PandasTimestampType.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -2**63 + 1,
            "max": 2**63 - 1,
        }
    ),

    # datetime[python]
    "datetime[python]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        PyDatetimeType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": datetime.datetime,
            "numpy_type": None,
            "pandas_type": None,
            "na_value": pd.NaT,
            "itemsize": None,
            "slug": generate_slug("datetime[python]", sparse=sparse, categorical=categorical),
            "supertype": DatetimeType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                PyDatetimeType.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -62135596800000000000,
            "max": 253402300799999999000,
        }
    ),

    # datetime[numpy]
    # TODO: change na_value to np.datetime64("nat")?
    "datetime[numpy]": lambda sparse, categorical: ElementTypeCase(
        {"unit": None, "step_size": 1, "sparse": sparse, "categorical": categorical},
        NumpyDatetime64Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.datetime64,
            "numpy_type": np.dtype("M8"),
            "pandas_type": None,
            "na_value": pd.NaT,
            "itemsize": 8,
            "slug": generate_slug("M8", sparse=sparse, categorical=categorical),
            "supertype": DatetimeType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                NumpyDatetime64Type.instance(sparse=sparse, categorical=categorical),
            }),
            "unit": None,
            "step_size": 1,
            "min": -291061508645168391112243200000000000,
            "max": 291061508645168328945024000000000000,
        }
    ),
    "M8[5m]": lambda sparse, categorical: ElementTypeCase(
        {"unit": "m", "step_size": 5, "sparse": sparse, "categorical": categorical},
        NumpyDatetime64Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.datetime64,
            "numpy_type": np.dtype("M8[5m]"),
            "pandas_type": None,
            "na_value": pd.NaT,
            "itemsize": 8,
            "slug": generate_slug("M8[5m]", sparse=sparse, categorical=categorical),
            "supertype": DatetimeType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                NumpyDatetime64Type.instance(unit="m", step_size=5, sparse=sparse, categorical=categorical),
            }),
            "unit": "m",
            "step_size": 5,
            "min": -291061508645168391112243200000000000,
            "max": 291061508645168328945024000000000000,
        }
    ),

    # timedelta supertype
    "timedelta": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        TimedeltaType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": None,
            "numpy_type": None,
            "pandas_type": None,
            "na_value": pd.NaT,
            "itemsize": None,
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
        }
    ),

    # timedelta[pandas]
    "timedelta[pandas]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        PandasTimedeltaType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": pd.Timedelta,
            "numpy_type": None,
            "pandas_type": None,
            "na_value": pd.NaT,
            "itemsize": None,
            "slug": generate_slug("timedelta[pandas]", sparse=sparse, categorical=categorical),
            "supertype": TimedeltaType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                PandasTimedeltaType.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -2**63 + 1,
            "max": 2**63 - 1,
        }
    ),

    # timedelta[python]
    "timedelta[python]": lambda sparse, categorical: ElementTypeCase(
        {"sparse": sparse, "categorical": categorical},
        PyTimedeltaType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": datetime.timedelta,
            "numpy_type": None,
            "pandas_type": None,
            "na_value": pd.NaT,
            "itemsize": None,
            "slug": generate_slug("timedelta[python]", sparse=sparse, categorical=categorical),
            "supertype": TimedeltaType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                PyTimedeltaType.instance(sparse=sparse, categorical=categorical),
            }),
            "min": -86399999913600000000000,
            "max": 86399999999999999999000,
        }
    ),

    # timedelta[numpy]
    # TODO: change na_value to np.timedelta64("nat")?
    "timedelta[numpy]": lambda sparse, categorical: ElementTypeCase(
        {"unit": None, "step_size": 1, "sparse": sparse, "categorical": categorical},
        NumpyTimedelta64Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.timedelta64,
            "numpy_type": np.dtype("m8"),
            "pandas_type": None,
            "na_value": pd.NaT,
            "itemsize": 8,
            "slug": generate_slug("m8", sparse=sparse, categorical=categorical),
            "supertype": TimedeltaType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                NumpyTimedelta64Type.instance(sparse=sparse, categorical=categorical),
            }),
            "unit": None,
            "step_size": 1,
            "min": -291061508645168391112243200000000000,
            "max": 291061508645168328945024000000000000,
        }
    ),
    "m8[25us]": lambda sparse, categorical: ElementTypeCase(
        {"unit": "us", "step_size": 25, "sparse": sparse, "categorical": categorical},
        NumpyTimedelta64Type,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": np.timedelta64,
            "numpy_type": np.dtype("m8[25us]"),
            "pandas_type": None,
            "na_value": pd.NaT,
            "itemsize": 8,
            "slug": generate_slug("m8[25us]", sparse=sparse, categorical=categorical),
            "supertype": TimedeltaType.instance(sparse=sparse, categorical=categorical),
            "subtypes": frozenset({
                NumpyTimedelta64Type.instance(unit="us", step_size=25, sparse=sparse, categorical=categorical),
            }),
            "unit": "us",
            "step_size": 25,
            "min": -291061508645168391112243200000000000,
            "max": 291061508645168328945024000000000000,
        }
    ),

    # string
    "string": lambda sparse, categorical: ElementTypeCase(
        {"storage": None, "sparse": sparse, "categorical": categorical},
        StringType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": str,
            "numpy_type": np.dtype(str),
            "pandas_type": DEFAULT_STRING_DTYPE,
            "na_value": pd.NA,
            "itemsize": None,
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
        }
    ),
    "string[python]": lambda sparse, categorical: ElementTypeCase(
        {"storage": "python", "sparse": sparse, "categorical": categorical},
        StringType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": str,
            "numpy_type": np.dtype(str),
            "pandas_type": pd.StringDtype(storage="python"),
            "na_value": pd.NA,
            "itemsize": None,
            "slug": generate_slug("string[python]", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                StringType.instance(storage="python", sparse=sparse, categorical=categorical)
            }),
            "is_default": False,
            "storage": "python",
        }
    ),
    # NOTE: pyarrow string type requires pyarrow dependency (handled below)

    # object
    "object": lambda sparse, categorical: ElementTypeCase(
        {"atomic_type": object, "sparse": sparse, "categorical": categorical},
        ObjectType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": object,
            "numpy_type": np.dtype("O"),
            "pandas_type": None,
            "na_value": pd.NA,
            "itemsize": None,
            "slug": generate_slug("object", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                ObjectType.instance(sparse=sparse, categorical=categorical)
            }),
        }
    ),
    "DummyClass": lambda sparse, categorical: ElementTypeCase(
        {"atomic_type": DummyClass, "sparse": sparse, "categorical": categorical},
        ObjectType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": DummyClass,
            "numpy_type": np.dtype("O"),
            "pandas_type": None,
            "na_value": pd.NA,
            "itemsize": None,
            "slug": generate_slug("DummyClass", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                ObjectType.instance(atomic_type=DummyClass, sparse=sparse, categorical=categorical)
            }),
        }
    ),
}


# add models for optional dependencies
if PYARROW_INSTALLED:  # pyarrow-backed StringType
    data_model["string[pyarrow]"] = lambda sparse, categorical: ElementTypeCase(
        {"storage": "pyarrow", "sparse": sparse, "categorical": categorical},
        StringType,
        {
            "sparse": sparse,
            "categorical": categorical,
            "nullable": True,
            "atomic_type": str,
            "numpy_type": np.dtype(str),
            "pandas_type": pd.StringDtype(storage="pyarrow"),
            "na_value": pd.NA,
            "itemsize": None,
            "slug": generate_slug("string[pyarrow]", sparse=sparse, categorical=categorical),
            "supertype": None,
            "subtypes": frozenset({
                StringType.instance(storage="pyarrow", sparse=sparse, categorical=categorical)
            }),
            "is_default": False,
            "storage": "pyarrow",
        }
    )


# flatten data_model, including every permutation of sparse, categorical
data_model = Parameters(*[
    model(sparse, categorical)
    for model in data_model.values()
    for sparse in (True, False)  # cartesian product of sparse, categorical
    for categorical in (True, False)
])


# ensure model contains entries for every subclass of ElementType
missing_subclasses = frozenset(
    s for s in all_subclasses(ElementType)
    if s not in {case.input for case in data_model}
)
if missing_subclasses:
    raise SyntaxError(
        f"`tests.types.test_element_types.data_model` is missing test cases "
        f"for ElementTypes: {set(s.__name__ for s in missing_subclasses)}"
    )


#####################
####    TESTS    ####
#####################


@parametrize(data_model)
def test_element_type_instance_constructor_returns_flyweights(case: ElementTypeCase):
    assert case.instance() is case.instance()


@parametrize(data_model)
def test_element_type_attributes_fit_data_model(case: ElementTypeCase):
    instance = case.instance()

    # check .attribute accessors
    for k, v in case.output.items():
        result = getattr(instance, k)

        # na_value special case - `==` is ambiguous for most NA objects
        if k == "na_value":
            # assert type match
            assert pd.isna(result) and pd.isna(v)
            assert type(result) == type(v)

            # complex special case - check real, imag separately
            if isinstance(v, (complex, np.complexfloating)):
                assert (
                    (pd.isna(result.real) and pd.isna(v.real)) or
                    result.real == v.real
                )
                assert (
                    (pd.isna(result.imag) and pd.isna(v.imag)) or
                    result.imag == v.imag
                )

        # generic case
        else:
            assert result == v

    # check str, repr, hash, len, iter
    assert str(instance) == case.output["slug"]
    assert repr(instance) == f"{case.input.__name__}({case.signature()})"
    assert hash(instance) == hash(case.output["slug"])
    assert len(instance) == len(case.output["subtypes"])
    assert frozenset(x for x in instance) == case.output["subtypes"]


@parametrize(data_model)
def test_element_type_attributes_are_immutable(case: ElementTypeCase):
    instance = case.instance()
    for k in case.output:
        with pytest.raises(AttributeError):
            setattr(instance, k, False)


@parametrize(data_model)
def test_element_type_contains_subtypes(case: ElementTypeCase):
    instance = case.instance()
    test_matrix = [x.instance() for x in data_model]

    # scalar test - iterate through flat_types and apply subtype logic
    for other in test_matrix:
        result = other in instance
        expected = other in instance.subtypes

        # datetime special case - if element_type is an M8 type and instance
        # has no units (either datetime supertype or NumpyDatetime64Type with
        # generic units), disregard unit/step_size during comparison
        if (
            isinstance(instance, DatetimeType) and
            isinstance(other, NumpyDatetime64Type)
        ):
            is_generic_M8 = (
                isinstance(instance, NumpyDatetime64Type) and
                instance.unit is None
            )
            is_datetime_supertype = (
                not isinstance(instance, tuple(all_subclasses(DatetimeType)))
            )
            if is_generic_M8 or is_datetime_supertype:
                expected = (
                    other.sparse == instance.sparse and
                    other.categorical == instance.categorical
                )

        # timedelta special case - if element_type is an m8 type and instance
        # has no units (either timedelta supertype or NumpyTimedelta64Type with
        # generic units), disregard unit/step_size during comparison
        if (
            isinstance(instance, TimedeltaType) and
            isinstance(other, NumpyTimedelta64Type)
        ):
            is_generic_m8 = (
                isinstance(instance, NumpyTimedelta64Type) and
                instance.unit is None
            )
            is_timedelta_supertype = (
                not isinstance(instance, tuple(all_subclasses(TimedeltaType)))
            )
            if is_generic_m8 or is_timedelta_supertype:
                expected = (
                    other.sparse == instance.sparse and
                    other.categorical == instance.categorical
                )

        # object special case - account for atomic_type inheritance
        if (
            isinstance(instance, ObjectType) and
            isinstance(other, ObjectType)
        ):
            expected = issubclass(
                other.atomic_type,
                instance.atomic_type
            )

        assert result == expected

    # collective test - use iterables directly
    assert instance.subtypes in instance  # set
    assert not test_matrix in instance  # list


@parametrize(data_model)
def test_element_type_is_subtype(case: ElementTypeCase):
    instance = case.instance()
    test_matrix = [x.instance() for x in data_model]

    # scalar test - iterate through flat_types and apply subtype logic
    for other in test_matrix:
        result = instance.is_subtype(other)
        expected = instance in other.subtypes

        # datetime special case - if element_type is an M8 type and instance
        # has no units (either datetime supertype or NumpyDatetime64Type with
        # generic units), disregard unit/step_size during comparison
        if (
            isinstance(other, DatetimeType) and
            isinstance(instance, NumpyDatetime64Type)
        ):
            is_generic_M8 = (
                isinstance(other, NumpyDatetime64Type) and
                other.unit is None
            )
            is_datetime_supertype = not isinstance(
                other,
                tuple(all_subclasses(DatetimeType))
            )
            if is_generic_M8 or is_datetime_supertype:
                expected = (
                    other.sparse == instance.sparse and
                    other.categorical == instance.categorical
                )

        # timedelta special case - if element_type is an m8 type and instance
        # has no units (either timedelta supertype or NumpyTimedelta64Type with
        # generic units), disregard unit/step_size during comparison
        if (
            isinstance(other, TimedeltaType) and
            isinstance(instance, NumpyTimedelta64Type)
        ):
            is_generic_m8 = (
                isinstance(other, NumpyTimedelta64Type) and
                other.unit is None
            )
            is_timedelta_supertype = not isinstance(
                other,
                tuple(all_subclasses(TimedeltaType))
            )
            if is_generic_m8 or is_timedelta_supertype:
                expected = (
                    other.sparse == instance.sparse and
                    other.categorical == instance.categorical
                )

        # object special case - account for atomic_type inheritance
        if (
            isinstance(other, ObjectType) and
            isinstance(instance, ObjectType)
        ):
            expected = issubclass(
                instance.atomic_type,
                other.atomic_type
            )

        assert result == expected

    # collective test - use iterables directly
    assert not instance.is_subtype(instance.subtypes - {instance})  # set
    assert instance.is_subtype(test_matrix)  # list


@parametrize(data_model)
def test_element_types_compare_equal(case: ElementTypeCase):
    instance = case.instance()
    for other in [x.instance() for x in data_model]:
        result = (instance == other)
        assert result == (hash(instance) == hash(other))
        assert result == (instance.slug == other.slug)
        assert result == (str(instance) == str(other))
        assert result == (repr(instance) == repr(other))
