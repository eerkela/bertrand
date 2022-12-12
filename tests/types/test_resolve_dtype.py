from __future__ import annotations
import datetime
import decimal
from itertools import product
from types import MappingProxyType

import numpy as np
import pandas as pd
import pytest

from tests.scheme import (
    _TestCase, all_subclasses, Parameters, parametrize, Raises
)

from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED
from pdtypes.types import (
    resolve_dtype, ElementType, BooleanType, IntegerType, SignedIntegerType,
    UnsignedIntegerType, Int8Type, Int16Type, Int32Type, Int64Type, UInt8Type,
    UInt16Type, UInt32Type, UInt64Type, FloatType, Float16Type, Float32Type,
    Float64Type, LongDoubleType, ComplexType, Complex64Type, Complex128Type,
    CLongDoubleType, DecimalType, DatetimeType, PandasTimestampType,
    PyDatetimeType, NumpyDatetime64Type, TimedeltaType, PandasTimedeltaType,
    PyTimedeltaType, NumpyTimedelta64Type, StringType, ObjectType
)


# final signature:
# resolve_dtype(typespec, sparse, categorical, nullable)
# -> nullable=True turns integer, boolean types into their nullable
# counterparts and does nothing for types that are already nullable.


# TODO: make data_model a named function with sparse, categorical, nullable
# flags
# -> def data_model(sparse, categorical, nullable)
# Flags indicate the call signature of resolve_dtype, and its keys are the base
# inputs to the resolve_dtype function.  They take every possible value, with
# every permutation of sparse, categorical, nullable.
# When flattening to the final dataset, just call it with the cartesian product
# of sparse, categorical, nullable
# -> data_model = itertools.chain([
#     data_model(sparse=sparse, categorical=categorical, nullable=nullable)
#     for sparse in (True, False)
#     for categorical in (True, False)
#     for nullable in (True, False)
# ])


##############################
####    CASE STRUCTURE    ####
##############################


class ResolveDtypeCase(_TestCase):
    """A subclass of `_TestCase` that defines the expected structure of test
    cases related to `pdtypes.types.resolve_dtype()` functionality.

    These cases enforce the following structure:
        -   `kwargs: dict`
            -   Keyword arguments to `resolve_dtype()`.  Must include
                values for `sparse` and `categorical` at minimum.
        -   `test_input: type specifier`
            -   A type specifier (atomic type, ElementType, type string, or
                numpy/pandas dtype object) representing input to the
                `resolve_dtype()` function.
        -   `test_output: ElementType`
            -   An instance of the expected `ElementType` subclass with
                expected properties for the given input, or a `pytest.raises()`
                context manager if the test case describes an error.
    """

    def __init__(
        self,
        kwargs: dict,
        test_input: (
            ElementType | type | str | np.dtype |
            pd.api.extensions.ExtensionDtype
        ),
        test_output: ElementType,
        name: str = None,
        id: str = None,
        marks: tuple = tuple()
    ):
        super().__init__(
            kwargs=kwargs,
            test_input=test_input,
            test_output=test_output,
            input_type=(
                ElementType, type, str, np.dtype,
                pd.api.extensions.ExtensionDtype
            ),
            output_type=ElementType,
            name=name,
            id=id,
            marks=marks
        )
        # kwargs must contain values for sparse, categorical at minimum
        # NOTE: since resolve_dtype is a cython function, we cannot inspect its
        # signature directly.
        if not all(key in self.kwargs for key in ("sparse", "categorical")):
            raise SyntaxError(
                f"`kwargs` must contain 'sparse' and 'categorical' keys"
            )

    def resolve(self) -> ElementType:
        return resolve_dtype(self.input, **self.kwargs)

    def __hash__(self) -> int:
        return hash((
            tuple(sorted(self.kwargs.items())),
            self.input,
            self.output
        ))

    def __eq__(self, other: ResolveDtypeCase) -> bool:
        return hash(self) == hash(other)


####################
####    DATA    ####
####################


def generate_numpy_pandas_dtype(
    base: np.dtype | pd.api.extensions.ExtensionDtype,
    sparse: bool,
    categorical: bool
):
    if categorical:
        base = pd.CategoricalDtype(pd.Index([], dtype=base))
    if sparse:
        base = pd.SparseDtype(base)
    return base


def generate_string_typespec(
    base: str,
    sparse: bool,
    categorical: bool
):
    if categorical:
        base = f"categorical[{base}]"
    if sparse:
        base = f"sparse[{base}]"
    return base


class DummyClass:
    pass



def data_model(sparse: bool, categorical: bool, nullable: bool):
    """Generate an exhaustive test matrix that describes the expected output
    for every input that is recognized by `resolve_dtype()` when called with
    the given arguments.
    """
    # dtypes must come before atomic and string types, since they need to
    # backreference to resolve platform specific types
    # -> maybe start with ElementTypes, numpy/pandas dtypes, atomic types,
    # string types and then finish up with atomic types?
    # -> allows a single stacked loop through sparse_input, categorical_input

    # TODO: sparse, categorical, nullable all default to None

    # build model step-by-step, using a dictionary to hold input/output values
    model = {}

    # iterate over a cartesion product of sparse, categorical inputs
    for sparse_input, categorical_input in product(
        [True, False],
        [True, False]
    ):
        sparse_output = sparse if sparse is not None else sparse_input
        categorical_output = (
            categorical if categorical is not None else categorical_input
        )

        # add direct ElementType instances
        model.update({
            # bool
            BooleanType.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): BooleanType.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            BooleanType.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): BooleanType.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # int
            IntegerType.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): IntegerType.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            IntegerType.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): IntegerType.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # signed int
            SignedIntegerType.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): SignedIntegerType.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            SignedIntegerType.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): SignedIntegerType.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # unsigned int
            UnsignedIntegerType.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): UnsignedIntegerType.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            UnsignedIntegerType.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): UnsignedIntegerType.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # int8
            Int8Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): Int8Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            Int8Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): Int8Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # int16
            Int16Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): Int16Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            Int16Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): Int16Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # int32
            Int32Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): Int32Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            Int32Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): Int32Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # int64
            Int64Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): Int64Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            Int64Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): Int64Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # uint8
            UInt8Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): UInt8Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            UInt8Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): UInt8Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # uint16
            UInt16Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): UInt16Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            UInt16Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): UInt16Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # uint32
            UInt32Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): UInt32Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            UInt32Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): UInt32Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # uint64
            UInt64Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=False
            ): UInt64Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else False
            ),
            UInt64Type.instance(
                sparse=sparse_input,
                categorical=categorical_input,
                nullable=True
            ): UInt64Type.instance(
                sparse=sparse_output,
                categorical=categorical_output, 
                nullable=nullable if nullable is not None else True
            ),

            # float
            FloatType.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): FloatType.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # float16
            Float16Type.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): Float16Type.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # float32
            Float32Type.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): Float32Type.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # float64
            Float64Type.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): Float64Type.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # longdouble
            LongDoubleType.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): LongDoubleType.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # complex
            ComplexType.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): ComplexType.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # complex64
            Complex64Type.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): Complex64Type.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # complex128
            Complex128Type.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): Complex128Type.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # clongdouble
            CLongDoubleType.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): CLongDoubleType.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # decimal
            DecimalType.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): DecimalType.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # datetime
            DatetimeType.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): DatetimeType.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # datetime[pandas]
            PandasTimestampType.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): PandasTimestampType.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # datetime[python]
            PyDatetimeType.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): PyDatetimeType.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # datetime[numpy]
            NumpyDatetime64Type.instance(
                unit=None,
                step_size=1,
                sparse=sparse_input,
                categorical=categorical_input
            ): NumpyDatetime64Type.instance(
                unit=None,
                step_size=1,
                sparse=sparse_output,
                categorical=categorical_output
            ),
            NumpyDatetime64Type.instance(
                unit="ns",
                step_size=5,
                sparse=sparse_input,
                categorical=categorical_input
            ): NumpyDatetime64Type.instance(
                unit="ns",
                step_size=5,
                sparse=sparse_output,
                categorical=categorical_output
            ),
            # TODO: add more unit/step_size combinations?

            # timedelta
            TimedeltaType.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): TimedeltaType.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # timedelta[pandas]
            PandasTimedeltaType.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): PandasTimedeltaType.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # timedelta[python]
            PyTimedeltaType.instance(
                sparse=sparse_input,
                categorical=categorical_input
            ): PyTimedeltaType.instance(
                sparse=sparse_output,
                categorical=categorical_output
            ),

            # timedelta[numpy]
            NumpyTimedelta64Type.instance(
                unit=None,
                step_size=1,
                sparse=sparse_input,
                categorical=categorical_input
            ): NumpyTimedelta64Type.instance(
                unit=None,
                step_size=1,
                sparse=sparse_output,
                categorical=categorical_output
            ),
            NumpyTimedelta64Type.instance(
                unit="ns",
                step_size=5,
                sparse=sparse_input,
                categorical=categorical_input
            ): NumpyTimedelta64Type.instance(
                unit="ns",
                step_size=5,
                sparse=sparse_output,
                categorical=categorical_output
            ),
            # TODO: add more unit/step_size combinations?

            # string
            StringType.instance(
                storage=None,
                sparse=sparse_input,
                categorical=categorical_input
            ): StringType.instance(
                storage=None,
                sparse=sparse_output,
                categorical=categorical_output
            ),
            StringType.instance(
                storage="python",
                sparse=sparse_input,
                categorical=categorical_input
            ): StringType.instance(
                storage="python",
                sparse=sparse_output,
                categorical=categorical_output
            ),
            # NOTE: pyarrow StringType covered in dependencies below

            # object
            ObjectType.instance(
                atomic_type=object,
                sparse=sparse_input,
                categorical=categorical_input
            ): ObjectType.instance(
                atomic_type=object,
                sparse=sparse_output,
                categorical=categorical_output
            ),
            ObjectType.instance(
                atomic_type=DummyClass,
                sparse=sparse_input,
                categorical=categorical_input
            ): ObjectType.instance(
                atomic_type=DummyClass,
                sparse=sparse_output,
                categorical=categorical_output
            ),

        })

        # add numpy/pandas dtype objects
        model.update({
            # bool
            generate_numpy_pandas_dtype(
                np.dtype(np.bool_),
                sparse=sparse_input,
                categorical=categorical_input
            ): BooleanType.instance(
                sparse=sparse_output,
                categorical=categorical_output,
                nullable=nullable if nullable is not None else False
            ),
            generate_numpy_pandas_dtype(
                pd.BooleanDtype(),
                sparse=sparse_input,
                categorical=categorical_input
            ): BooleanType.instance(
                sparse=sparse_output,
                categorical=categorical_output,
                nullable=nullable if nullable is not None else True
            ),

            # int8
            generate_numpy_pandas_dtype(
                np.dtype(np.int8),
                sparse=sparse_input,
                categorical=categorical_input
            ): Int8Type.instance(
                sparse=sparse_output,
                categorical=categorical_output,
                nullable=nullable if nullable is not None else False
            ),
            generate_numpy_pandas_dtype(
                pd.Int8Dtype(),
                sparse=sparse_input,
                categorical=categorical_input
            ): Int8Type.instance(
                sparse=sparse_output,
                categorical=categorical_output,
                nullable=nullable if nullable is not None else True
            ),

            # int16
            np.dtype(np.int16): (
                Int16Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else False
                )
            ),
            pd.Int16Dtype(): (
                Int16Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else True
                )
            ),

            # int32
            np.dtype(np.int32): (
                Int32Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else False
                )
            ),
            pd.Int32Dtype(): (
                Int32Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else True
                )
            ),

            # int64
            np.dtype(np.int64): (
                Int64Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else False
                )
            ),
            pd.Int64Dtype(): (
                Int64Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else True
                )
            ),

            # uint8
            np.dtype(np.uint8): (
                UInt8Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else False
                )
            ),
            pd.UInt8Dtype(): (
                UInt8Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else True
                )
            ),

            # uint16
            np.dtype(np.uint16): (
                UInt16Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else False
                )
            ),
            pd.UInt16Dtype(): (
                UInt16Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else True
                )
            ),

            # uint32
            np.dtype(np.uint32): (
                UInt32Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else False
                )
            ),
            pd.UInt32Dtype(): (
                UInt32Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else True
                )
            ),

            # uint64
            np.dtype(np.uint64): (
                UInt64Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else False
                )
            ),
            pd.UInt64Dtype(): (
                UInt64Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                    nullable=nullable if nullable is not None else True
                )
            ),

            # float16
            np.dtype(np.float16): (
                Raises(
                    TypeError,
                    "Float16Type has no non-nullable equivalent",
                )
                if nullable is False else
                Float16Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),

            # float32
            np.dtype(np.float32): (
                Raises(
                    TypeError,
                    "Float32Type has no non-nullable equivalent",
                )
                if nullable is False else
                Float32Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),

            # float64
            np.dtype(np.float64): (
                Raises(
                    TypeError,
                    "Float64Type has no non-nullable equivalent",
                )
                if nullable is False else
                Float64Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),

            # longdouble = C long double
            np.dtype(np.longdouble): (
                Raises(
                    TypeError,
                    "LongDoubleType has no non-nullable equivalent",
                )
                if nullable is False else
                LongDoubleType.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),

            # complex64
            np.dtype(np.complex64): (
                Raises(
                    TypeError,
                    "Complex64Type has no non-nullable equivalent",
                )
                if nullable is False else
                Complex64Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),

            # complex128
            np.dtype(np.complex128): (
                Raises(
                    TypeError,
                    "Complex128Type has no non-nullable equivalent",
                )
                if nullable is False else
                Complex128Type.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),

            # clongdouble = (complex) C long double
            np.dtype(np.clongdouble): (
                Raises(
                    TypeError,
                    "CLongDoubleType has no non-nullable equivalent",
                )
                if nullable is False else
                CLongDoubleType.instance(
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),

            # datetime[numpy]
            # NOTE: using any units other than M8[ns] causes CategoricalDtype
            # construction to fail.
            np.dtype("M8"): (
                Raises(
                    TypeError,
                    "NumpyDatetime64Type has no non-nullable equivalent",
                )
                if nullable is False else
                NumpyDatetime64Type.instance(
                    unit=None,
                    step_size=1,
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),
            np.dtype("M8[ns]"): (
                Raises(
                    TypeError,
                    "NumpyDatetime64Type has no non-nullable equivalent",
                )
                if nullable is False else
                NumpyDatetime64Type.instance(
                    unit="ns",
                    step_size=1,
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),
            np.dtype("M8[5us]"): (
                Raises(
                    TypeError,
                    "NumpyDatetime64Type has no non-nullable equivalent",
                )
                if nullable is False else
                NumpyDatetime64Type.instance(
                    unit="us",
                    step_size=5,
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),
            # NOTE: "M8" with specific units/step size handled in special case
            # TODO: these should be added directly

            # timedelta[numpy]
            # NOTE: using any units other than m8[ns] causes CategoricalDtype
            # construction to fail.
            np.dtype("m8"): (
                Raises(
                    TypeError,
                    "NumpyTimedelta64Type has no non-nullable equivalent",
                )
                if nullable is False else
                NumpyTimedelta64Type.instance(
                    unit=None,
                    step_size=1,
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),
            # NOTE: "m8" with specific units/step size handled in special case
            # TODO: these should be added directly

            # string
            np.dtype(str): (
                Raises(
                    TypeError,
                    "StringType has no non-nullable equivalent",
                )
                if nullable is False else
                StringType.instance(
                    storage=None,
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),
            # TODO: "U" with specific length handled in special case?
            pd.StringDtype("python"): (
                Raises(
                    TypeError,
                    "StringType has no non-nullable equivalent",
                )
                if nullable is False else
                StringType.instance(
                    storage="python",
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),

            # object
            np.dtype("O"): (
                Raises(
                    TypeError,
                    "ObjectType has no non-nullable equivalent",
                )
                if nullable is False else
                ObjectType.instance(
                    atomic_type=object,
                    sparse=sparse if sparse is not None else sparse_input,
                    categorical=categorical if categorical is not None else categorical_input,
                )
            ),

            # bytes (raise errors)
            # TODO: use an exception wrapper
            np.dtype(np.bytes_): Raises(
                TypeError,
                "dtype not recognized: dtype('S')",
            ),

            # void (raise errors)
            # TODO: use an exception wrapper
            np.dtype(np.void): Raises(
                TypeError,
                "dtype not recognized: dtype('V')",
            ),
        })

    # add atomic types
    model.update({

    })

    # add string type specifiers
    model.update({

    })

    # # add optional dependencies
    # TODO: these have to account for sparse_input, categorical_input
    # if PYARROW_INSTALLED:  # pyarrow-backed StringType
    #     model.update({
    #         St
    #     })

    # flatten and return as a 1D sequence of cases
    kwargs = {
        "sparse": sparse,
        "categorical": categorical,
        "nullable": nullable,
    }
    return Parameters(*[
        ResolveDtypeCase(kwargs, k, v) for k, v in model.items()
    ])




# # add numpy/pandas dtype objects to data_model
# data_model.update({
#     generate_numpy_pandas_dtype(
#         k,
#         sparse=dtype_is_sparse,
#         categorical=dtype_is_categorical
#     ): v
#     for k, v in {
#         # bool
#         np.dtype(bool): lambda sparse, categorical, nullable: (
#             BooleanType.instance(
#                 sparse=dtype_is_sparse or sparse,
#                 categorical=dtype_is_categorical or categorical,
#                 nullable=nullable
#             )
#         ),
#         pd.BooleanDtype(): lambda sparse, categorical, nullable: (
#             BooleanType.instance(
#                 sparse=dtype_is_sparse or sparse,
#                 categorical=dtype_is_categorical or categorical,
#                 nullable=True
#             )
#         ),


#     }.items()
#     for dtype_is_sparse in (True, False)
#     for dtype_is_categorical in (True, False)
#     if not (dtype_is_categorical and k in {np.dtype("M8"), np.dtype("m8")})
# })


# # # add atomic types to data_model
# # data_model.update({
# #     # bool
# #     BooleanType: lambda sparse, categorical, nullable: (
# #         BooleanType.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     bool: lambda sparse, categorical, nullable: (
# #         BooleanType.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.bool_: lambda sparse, categorical, nullable: (
# #         BooleanType.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # int
# #     IntegerType: lambda sparse, categorical, nullable: (
# #         IntegerType.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     int: lambda sparse, categorical, nullable: (
# #         IntegerType.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.integer: lambda sparse, categorical, nullable: (
# #         IntegerType.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # signed int
# #     SignedIntegerType: lambda sparse, categorical, nullable: (
# #         SignedIntegerType.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.signedinteger: lambda sparse, categorical, nullable: (
# #         SignedIntegerType.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # unsigned int
# #     UnsignedIntegerType: lambda sparse, categorical, nullable: (
# #         UnsignedIntegerType.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.unsignedinteger: lambda sparse, categorical, nullable: (
# #         UnsignedIntegerType.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # int8
# #     Int8Type: lambda sparse, categorical, nullable: (
# #         Int8Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.int8: lambda sparse, categorical, nullable: (
# #         Int8Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # int16
# #     Int16Type: lambda sparse, categorical, nullable: (
# #         Int16Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.int16: lambda sparse, categorical, nullable: (
# #         Int16Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # int32
# #     Int32Type: lambda sparse, categorical, nullable: (
# #         Int32Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.int32: lambda sparse, categorical, nullable: (
# #         Int32Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # int64
# #     Int64Type: lambda sparse, categorical, nullable: (
# #         Int64Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.int64: lambda sparse, categorical, nullable: (
# #         Int64Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # uint8
# #     UInt8Type: lambda sparse, categorical, nullable: (
# #         UInt8Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.uint8: lambda sparse, categorical, nullable: (
# #         UInt8Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # uint16
# #     UInt16Type: lambda sparse, categorical, nullable: (
# #         UInt16Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.uint8: lambda sparse, categorical, nullable: (
# #         UInt16Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # uint32
# #     UInt32Type: lambda sparse, categorical, nullable: (
# #         UInt32Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.uint32: lambda sparse, categorical, nullable: (
# #         UInt32Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # uint64
# #     UInt64Type: lambda sparse, categorical, nullable: (
# #         UInt64Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),
# #     np.uint64: lambda sparse, categorical, nullable: (
# #         UInt64Type.instance(
# #             sparse=sparse,
# #             categorical=categorical,
# #             nullable=nullable
# #         )
# #     ),

# #     # float
# #     FloatType: lambda sparse, categorical, nullable: (
# #         FloatType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     float: lambda sparse, categorical, nullable: (
# #         FloatType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.floating: lambda sparse, categorical, nullable: (
# #         FloatType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # float16
# #     Float16Type: lambda sparse, categorical, nullable: (
# #         Float16Type.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.float16: lambda sparse, categorical, nullable: (
# #         Float16Type.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # float32
# #     Float32Type: lambda sparse, categorical, nullable: (
# #         Float32Type.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.float32: lambda sparse, categorical, nullable: (
# #         Float32Type.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # float64
# #     Float64Type: lambda sparse, categorical, nullable: (
# #         Float64Type.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.float64: lambda sparse, categorical, nullable: (
# #         Float64Type.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # longdouble
# #     LongDoubleType: lambda sparse, categorical, nullable: (
# #         LongDoubleType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.longdouble: lambda sparse, categorical, nullable: (
# #         LongDoubleType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # complex
# #     ComplexType: lambda sparse, categorical, nullable: (
# #         ComplexType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     complex: lambda sparse, categorical, nullable: (
# #         ComplexType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.complexfloating: lambda sparse, categorical, nullable: (
# #         ComplexType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # complex64
# #     Complex64Type: lambda sparse, categorical, nullable: (
# #         Complex64Type.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.complex64: lambda sparse, categorical, nullable: (
# #         Complex64Type.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # complex128
# #     Complex128Type: lambda sparse, categorical, nullable: (
# #         Complex128Type.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.complex128: lambda sparse, categorical, nullable: (
# #         Complex128Type.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # clongdouble
# #     CLongDoubleType: lambda sparse, categorical, nullable: (
# #         CLongDoubleType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.clongdouble: lambda sparse, categorical, nullable: (
# #         CLongDoubleType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # decimal
# #     DecimalType: lambda sparse, categorical, nullable: (
# #         DecimalType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     decimal.Decimal: lambda sparse, categorical, nullable: (
# #         DecimalType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # datetime
# #     DatetimeType: lambda sparse, categorical, nullable: (
# #         DatetimeType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # datetime[pandas]
# #     PandasTimestampType: lambda sparse, categorical, nullable: (
# #         PandasTimestampType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     pd.Timestamp: lambda sparse, categorical, nullable: (
# #         PandasTimestampType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # datetime[python]
# #     PyDatetimeType: lambda sparse, categorical, nullable: (
# #         PyDatetimeType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     datetime.datetime: lambda sparse, categorical, nullable: (
# #         PyDatetimeType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # datetime[numpy]
# #     NumpyDatetime64Type: lambda sparse, categorical, nullable: (
# #         NumpyDatetime64Type.instance(
# #             unit=None,
# #             step_size=1,
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.datetime64: lambda sparse, categorical, nullable: (
# #         NumpyDatetime64Type.instance(
# #             unit=None,
# #             step_size=1,
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # timedelta
# #     TimedeltaType: lambda sparse, categorical, nullable: (
# #         TimedeltaType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # timedelta[pandas]
# #     PandasTimedeltaType: lambda sparse, categorical, nullable: (
# #         PandasTimedeltaType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     pd.Timedelta: lambda sparse, categorical, nullable: (
# #         PandasTimedeltaType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # timedelta[python]
# #     PyTimedeltaType: lambda sparse, categorical, nullable: (
# #         PyTimedeltaType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     datetime.timedelta: lambda sparse, categorical, nullable: (
# #         PyTimedeltaType.instance(
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # timedelta[numpy]
# #     NumpyTimedelta64Type: lambda sparse, categorical, nullable: (
# #         NumpyTimedelta64Type.instance(
# #             unit=None,
# #             step_size=1,
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.timedelta64: lambda sparse, categorical, nullable: (
# #         NumpyTimedelta64Type.instance(
# #             unit=None,
# #             step_size=1,
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # string
# #     StringType: lambda sparse, categorical, nullable: (
# #         StringType.instance(
# #             storage=None,
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     str: lambda sparse, categorical, nullable: (
# #         StringType.instance(
# #             storage=None,
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     np.str_: lambda sparse, categorical, nullable: (
# #         StringType.instance(
# #             storage=None,
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),

# #     # object
# #     ObjectType: lambda sparse, categorical, nullable: (
# #         ObjectType.instance(
# #             atomic_type=object,
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     object: lambda sparse, categorical, nullable: (
# #         ObjectType.instance(
# #             atomic_type=object,
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# #     DummyClass: lambda sparse, categorical, nullable: (
# #         ObjectType.instance(
# #             atomic_type=DummyClass,
# #             sparse=sparse,
# #             categorical=categorical
# #         )
# #     ),
# # })


# # # add string types to data_model
# # data_model.update({
    
# # })


# # # add optional dependencies to data_model
# # if PYARROW_INSTALLED:  # pyarrow-backed StringType
# #     pass


# # flatten data_model by sparse, categorical, nullable flags and convert to
# # Parameters
# data_model = Parameters(*[
#     ResolveDtypeCase(
#         {"sparse": sparse, "categorical": categorical, "nullable": nullable},
#         k,
#         v(sparse=sparse, categorical=categorical, nullable=nullable)
#     )
#     for k, v in data_model.items()
#     for sparse in (True, False)
#     for categorical in (True, False)
#     for nullable in (True, False)
# ])



# dtype_element_types = {
#     # bool
#     np.dtype(bool): BooleanType.instance(),

#     # nullable[bool]
#     pd.BooleanDtype(): BooleanType.instance(nullable=True),

#     # int8
#     np.dtype(np.int8): Int8Type.instance(),

#     # nullable[int8]
#     pd.Int8Dtype(): Int8Type.instance(nullable=True),

#     # int16
#     np.dtype(np.int16): Int16Type.instance(),

#     # nullable[int16]
#     pd.Int16Dtype(): Int16Type.instance(nullable=True),

#     # int32
#     np.dtype(np.int32): Int32Type.instance(),

#     # nullable[int32]
#     pd.Int32Dtype(): Int32Type.instance(nullable=True),

#     # int64
#     np.dtype(np.int64): Int64Type.instance(),

#     # nullable[int64]
#     pd.Int64Dtype(): Int64Type.instance(nullable=True),

#     # uint8
#     np.dtype(np.uint8): UInt8Type.instance(),

#     # nullable[uint8]
#     pd.UInt8Dtype(): UInt8Type.instance(nullable=True),

#     # uint16
#     np.dtype(np.uint16): UInt16Type.instance(),

#     # nullable[uint16]
#     pd.UInt16Dtype(): UInt16Type.instance(nullable=True),

#     # uint32
#     np.dtype(np.uint32): UInt32Type.instance(),

#     # nullable[uint32]
#     pd.UInt32Dtype(): UInt32Type.instance(nullable=True),

#     # uint64
#     np.dtype(np.uint64): UInt64Type.instance(),

#     # nullable[uint64]
#     pd.UInt64Dtype(): UInt64Type.instance(nullable=True),

#     # float16
#     np.dtype(np.float16): Float16Type.instance(),

#     # float32
#     np.dtype(np.float32): Float32Type.instance(),

#     # float64
#     np.dtype(np.float64): Float64Type.instance(),

#     # longdouble = C long double
#     np.dtype(np.longdouble): LongDoubleType.instance(),

#     # complex64
#     np.dtype(np.complex64): Complex64Type.instance(),

#     # complex128
#     np.dtype(np.complex128): Complex128Type.instance(),

#     # clongdouble = (complex) C long double
#     np.dtype(np.clongdouble): CLongDoubleType.instance(),

#     # datetime[numpy]
#     np.dtype("M8"): NumpyDatetime64Type.instance(),
#     # NOTE: "M8" with specific units/step size handled in special case

#     # timedelta[numpy]
#     np.dtype("m8"): NumpyTimedelta64Type.instance(),
#     # NOTE: "m8" with specific units/step size handled in special case

#     # string
#     np.dtype(str): StringType.instance(),
#     # NOTE: "U" with specific length handled in special case

#     # string[python]
#     pd.StringDtype("python"): StringType.instance(storage="python"),

#     # object
#     np.dtype("O"): ObjectType.instance(),
# }


# atomic_element_types = {
#     # some types are platform-dependent.  For reference, these are indicated
#     # with a comment showing the equivalent type as seen by compilers.

#     # bool
#     bool: BooleanType.instance(),
#     np.bool_: BooleanType.instance(),

#     # int
#     int: IntegerType.instance(),
#     np.integer: IntegerType.instance(),

#     # signed
#     np.signedinteger: SignedIntegerType.instance(),

#     # unsigned
#     np.unsignedinteger: UnsignedIntegerType.instance(),

#     # int8
#     np.int8: Int8Type.instance(),

#     # int16
#     np.int16: Int16Type.instance(),

#     # int32
#     np.int32: Int32Type.instance(),

#     # int64
#     np.int64: Int64Type.instance(),

#     # uint8
#     np.uint8: UInt8Type.instance(),

#     # uint16
#     np.uint16: UInt16Type.instance(),

#     # uint32
#     np.uint32: UInt32Type.instance(),

#     # uint64
#     np.uint64: UInt64Type.instance(),

#     # char = C char
#     np.byte: dtype_element_types[np.dtype(np.byte)],

#     # short = C short
#     np.short: dtype_element_types[np.dtype(np.short)],

#     # intc = C int
#     np.intc: dtype_element_types[np.dtype(np.intc)],

#     # long = C long
#     np.int_: dtype_element_types[np.dtype(np.int_)],

#     # long long = C long long
#     np.longlong: dtype_element_types[np.dtype(np.longlong)],

#     # ssize_t = C ssize_t
#     np.intp: dtype_element_types[np.dtype(np.intp)],

#     # unsigned char = C unsigned char
#     np.ubyte: dtype_element_types[np.dtype(np.ubyte)],

#     # unsigned short = C unsigned short
#     np.ushort: dtype_element_types[np.dtype(np.ushort)],

#     # unsigned intc = C unsigned int
#     np.uintc: dtype_element_types[np.dtype(np.uintc)],

#     # unsigned long = C unsigned long
#     np.uint: dtype_element_types[np.dtype(np.uint)],

#     # unsigned long long = C unsigned long long
#     np.ulonglong: dtype_element_types[np.dtype(np.ulonglong)],

#     # size_t = C size_t
#     np.uintp: dtype_element_types[np.dtype(np.uintp)],

#     # float
#     float: FloatType.instance(),
#     np.floating: FloatType.instance(),

#     # float16
#     np.float16: Float16Type.instance(),
#     np.half: Float16Type.instance(),

#     # float32
#     np.float32: Float32Type.instance(),
#     np.single: Float32Type.instance(),

#     # float64
#     np.float64: Float64Type.instance(),
#     np.double: Float64Type.instance(),

#     # longdouble = C long double
#     # TODO: np.float96/np.float128
#     np.longdouble: LongDoubleType.instance(),

#     # complex
#     complex: ComplexType.instance(),
#     np.complexfloating: ComplexType.instance(),

#     # complex64
#     np.complex64: Complex64Type.instance(),
#     np.csingle: Complex64Type.instance(),

#     # complex128
#     np.complex128: Complex128Type.instance(),
#     np.cdouble: Complex128Type.instance(),

#     # clongdouble = (complex) C long double
#     # TODO: np.complex192/np.complex256?
#     np.clongdouble: CLongDoubleType.instance(),

#     # decimal
#     decimal.Decimal: DecimalType.instance(),

#     # datetime[pandas]
#     pd.Timestamp: PandasTimestampType.instance(),

#     # datetime[python]
#     datetime.datetime: PyDatetimeType.instance(),

#     # datetime[numpy]
#     np.datetime64: NumpyDatetime64Type.instance(),

#     # timedelta[pandas]
#     pd.Timedelta: PandasTimedeltaType.instance(),

#     # timedelta[python]
#     datetime.timedelta: PyTimedeltaType.instance(),

#     # timedelta[numpy]
#     np.timedelta64: NumpyTimedelta64Type.instance(),

#     # string
#     str: StringType.instance(),
#     np.str_: StringType.instance(),

#     # object
#     object: ObjectType.instance(),
# }


# # TODO: these need to account for sparse[]/categorical[]

# string_element_types = {
#     # some types are platform-dependent.  For reference, these are indicated
#     # with a comment showing the equivalent type as seen by compilers.

#     # bool
#     "bool": BooleanType.instance(),
#     "boolean": BooleanType.instance(),
#     "bool_": BooleanType.instance(),
#     "bool8": BooleanType.instance(),
#     "b1": BooleanType.instance(),
#     "?": BooleanType.instance(),

#     # nullable[bool]
#     "nullable[bool]": BooleanType.instance(nullable=True),
#     "nullable[boolean]": BooleanType.instance(nullable=True),
#     "nullable[bool_]": BooleanType.instance(nullable=True),
#     "nullable[bool8]": BooleanType.instance(nullable=True),
#     "nullable[b1]": BooleanType.instance(nullable=True),
#     "nullable[?]": BooleanType.instance(nullable=True),
#     "Boolean": BooleanType.instance(nullable=True),

#     # int
#     "int": IntegerType.instance(),
#     "integer": IntegerType.instance(),

#     # nullable[int]
#     "nullable[int]": IntegerType.instance(nullable=True),
#     "nullable[integer]": IntegerType.instance(nullable=True),

#     # signed
#     "signed": SignedIntegerType.instance(),
#     "signed integer": SignedIntegerType.instance(),
#     "signed int": SignedIntegerType.instance(),
#     "i": SignedIntegerType.instance(),

#     # nullable[signed]
#     "nullable[signed]": SignedIntegerType.instance(nullable=True),
#     "nullable[signed integer]": SignedIntegerType.instance(nullable=True),
#     "nullable[signed int]": SignedIntegerType.instance(nullable=True),
#     "nullable[i]": SignedIntegerType.instance(nullable=True),

#     # unsigned
#     "unsigned": UnsignedIntegerType.instance(),
#     "unsigned integer": UnsignedIntegerType.instance(),
#     "unsigned int": UnsignedIntegerType.instance(),
#     "uint": UnsignedIntegerType.instance(),
#     "u": UnsignedIntegerType.instance(),

#     # nullable[unsigned]
#     "nullable[unsigned]": UnsignedIntegerType.instance(nullable=True),
#     "nullable[unsigned integer]": UnsignedIntegerType.instance(nullable=True),
#     "nullable[unsigned int]": UnsignedIntegerType.instance(nullable=True),
#     "nullable[uint]": UnsignedIntegerType.instance(nullable=True),
#     "nullable[u]": UnsignedIntegerType.instance(nullable=True),

#     # int8
#     "int8": Int8Type.instance(),
#     "i1": Int8Type.instance(),

#     # nullable[int8]
#     "nullable[int8]": Int8Type.instance(nullable=True),
#     "nullable[i1]": Int8Type.instance(nullable=True),
#     "Int8": Int8Type.instance(nullable=True),

#     # int16
#     "int16": Int16Type.instance(),
#     "i2": Int16Type.instance(),

#     # nullable[int16]
#     "nullable[int16]": Int16Type.instance(nullable=True),
#     "nullable[i2]": Int16Type.instance(nullable=True),
#     "Int16": Int16Type.instance(nullable=True),

#     # int32
#     "int32": Int32Type.instance(),
#     "i4": Int32Type.instance(),

#     # nullable[int32]
#     "nullable[int32]": Int32Type.instance(nullable=True),
#     "nullable[i4]": Int32Type.instance(nullable=True),
#     "Int32": Int32Type.instance(nullable=True),

#     # int64
#     "int64": Int64Type.instance(),
#     "i8": Int64Type.instance(),

#     # nullable[int64]
#     "nullable[int64]": Int64Type.instance(nullable=True),
#     "nullable[i8]": Int64Type.instance(nullable=True),
#     "Int64": Int64Type.instance(nullable=True),

#     # uint8
#     "uint8": UInt8Type.instance(),
#     "u1": UInt8Type.instance(),

#     # nullable[uint8]
#     "nullable[uint8]": UInt8Type.instance(nullable=True),
#     "nullable[u1]": UInt8Type.instance(nullable=True),
#     "UInt8": UInt8Type.instance(nullable=True),

#     # uint16
#     "uint16": UInt16Type.instance(),
#     "u2": UInt16Type.instance(),

#     # nullable[uint16]
#     "nullable[uint16]": UInt16Type.instance(nullable=True),
#     "nullable[u2]": UInt16Type.instance(nullable=True),
#     "UInt16": UInt16Type.instance(nullable=True),

#     # uint32
#     "uint32": UInt32Type.instance(),
#     "u4": UInt32Type.instance(),

#     # nullable[uint32]
#     "nullable[uint32]": UInt32Type.instance(nullable=True),
#     "nullable[u4]": UInt32Type.instance(nullable=True),
#     "UInt32": UInt32Type.instance(nullable=True),

#     # uint64
#     "uint64": UInt64Type.instance(),
#     "u8": UInt64Type.instance(),

#     # nullable[uint64]
#     "nullable[uint64]": UInt64Type.instance(nullable=True),
#     "nullable[u8]": UInt64Type.instance(nullable=True),
#     "UInt64": UInt64Type.instance(nullable=True),

#     # char = C char
#     "char": dtype_element_types[np.dtype(np.byte)],
#     "signed char": dtype_element_types[np.dtype(np.byte)],
#     "byte": dtype_element_types[np.dtype(np.byte)],
#     "b": dtype_element_types[np.dtype(np.byte)],

#     # nullable[char] = nullable extensions to C char
#     "nullable[char]": type(dtype_element_types[np.dtype(np.byte)]).instance(nullable=True),
#     "nullable[signed char]": type(dtype_element_types[np.dtype(np.byte)]).instance(nullable=True),
#     "nullable[byte]": type(dtype_element_types[np.dtype(np.byte)]).instance(nullable=True),
#     "nullable[b]": type(dtype_element_types[np.dtype(np.byte)]).instance(nullable=True),

#     # short = C short
#     "short": dtype_element_types[np.dtype(np.short)],
#     "short int": dtype_element_types[np.dtype(np.short)],
#     "signed short": dtype_element_types[np.dtype(np.short)],
#     "signed short int": dtype_element_types[np.dtype(np.short)],
#     "h": dtype_element_types[np.dtype(np.short)],

#     # nullable[short] = nullable extensions to C short
#     "nullable[short]": type(dtype_element_types[np.dtype(np.short)]).instance(nullable=True),
#     "nullable[short int]": type(dtype_element_types[np.dtype(np.short)]).instance(nullable=True),
#     "nullable[signed short]": type(dtype_element_types[np.dtype(np.short)]).instance(nullable=True),
#     "nullable[signed short int]": type(dtype_element_types[np.dtype(np.short)]).instance(nullable=True),
#     "nullable[h]": type(dtype_element_types[np.dtype(np.short)]).instance(nullable=True),

#     # intc = C int
#     "intc": dtype_element_types[np.dtype(np.intc)],
#     "signed intc": dtype_element_types[np.dtype(np.intc)],

#     # nullable[intc] = nullable extensions to C int
#     "nullable[intc]": type(dtype_element_types[np.dtype(np.intc)]).instance(nullable=True),
#     "nullable[signed intc]": type(dtype_element_types[np.dtype(np.intc)]).instance(nullable=True),

#     # long = C long
#     "long": dtype_element_types[np.dtype(np.int_)],
#     "long int": dtype_element_types[np.dtype(np.int_)],
#     "signed long": dtype_element_types[np.dtype(np.int_)],
#     "signed long int": dtype_element_types[np.dtype(np.int_)],
#     "l": dtype_element_types[np.dtype(np.int_)],

#     # nullable[long] = nullable extensions to C long
#     "nullable[long]": type(dtype_element_types[np.dtype(np.int_)]).instance(nullable=True),
#     "nullable[long int]": type(dtype_element_types[np.dtype(np.int_)]).instance(nullable=True),
#     "nullable[signed long]": type(dtype_element_types[np.dtype(np.int_)]).instance(nullable=True),
#     "nullable[signed long int]": type(dtype_element_types[np.dtype(np.int_)]).instance(nullable=True),
#     "nullable[l]": type(dtype_element_types[np.dtype(np.int_)]).instance(nullable=True),

#     # long long = C long long
#     "long long": dtype_element_types[np.dtype(np.longlong)],
#     "longlong": dtype_element_types[np.dtype(np.longlong)],
#     "long long int": dtype_element_types[np.dtype(np.longlong)],
#     "signed long long": dtype_element_types[np.dtype(np.longlong)],
#     "signed longlong": dtype_element_types[np.dtype(np.longlong)],
#     "signed long long int": dtype_element_types[np.dtype(np.longlong)],
#     "q": dtype_element_types[np.dtype(np.longlong)],

#     # nullable[long long] = nullable extensions to C long long
#     "nullable[long long]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
#     "nullable[longlong]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
#     "nullable[long long int]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
#     "nullable[signed long long]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
#     "nullable[signed longlong]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
#     "nullable[signed long long int]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),
#     "nullable[q]": type(dtype_element_types[np.dtype(np.longlong)]).instance(nullable=True),

#     # ssize_t = C ssize_t
#     "ssize_t": dtype_element_types[np.dtype(np.intp)],
#     "intp": dtype_element_types[np.dtype(np.intp)],
#     "int0": dtype_element_types[np.dtype(np.intp)],
#     "p": dtype_element_types[np.dtype(np.intp)],

#     # nullable[ssize_t] = nullable extensions to C ssize_t
#     "nullable[ssize_t]": type(dtype_element_types[np.dtype(np.intp)]).instance(nullable=True),
#     "nullable[intp]": type(dtype_element_types[np.dtype(np.intp)]).instance(nullable=True),
#     "nullable[int0]": type(dtype_element_types[np.dtype(np.intp)]).instance(nullable=True),
#     "nullable[p]": type(dtype_element_types[np.dtype(np.intp)]).instance(nullable=True),

#     # unsigned char = C unsigned char
#     "unsigned char": dtype_element_types[np.dtype(np.ubyte)],
#     "unsigned byte": dtype_element_types[np.dtype(np.ubyte)],
#     "ubyte": dtype_element_types[np.dtype(np.ubyte)],
#     "B": dtype_element_types[np.dtype(np.ubyte)],

#     # nullable[unsigned char] = nullable extensions to C unsigned char
#     "nullable[unsigned char]": type(dtype_element_types[np.dtype(np.ubyte)]).instance(nullable=True),
#     "nullable[unsigned byte]": type(dtype_element_types[np.dtype(np.ubyte)]).instance(nullable=True),
#     "nullable[ubyte]": type(dtype_element_types[np.dtype(np.ubyte)]).instance(nullable=True),
#     "nullable[B]": type(dtype_element_types[np.dtype(np.ubyte)]).instance(nullable=True),

#     # unsigned short = C unsigned short
#     "unsigned short": dtype_element_types[np.dtype(np.ushort)],
#     "unsigned short int": dtype_element_types[np.dtype(np.ushort)],
#     "ushort": dtype_element_types[np.dtype(np.ushort)],
#     "H": dtype_element_types[np.dtype(np.ushort)],

#     # nullable[unsigned short] = nullable extensions to C unsigned short
#     "nullable[unsigned short]": type(dtype_element_types[np.dtype(np.ushort)]).instance(nullable=True),
#     "nullable[unsigned short int]": type(dtype_element_types[np.dtype(np.ushort)]).instance(nullable=True),
#     "nullable[ushort]": type(dtype_element_types[np.dtype(np.ushort)]).instance(nullable=True),
#     "nullable[H]": type(dtype_element_types[np.dtype(np.ushort)]).instance(nullable=True),

#     # unsigned intc = C unsigned int
#     "unsigned intc": dtype_element_types[np.dtype(np.uintc)],
#     "uintc": dtype_element_types[np.dtype(np.uintc)],
#     "I": dtype_element_types[np.dtype(np.uintc)],

#     # nullable[unsigned intc] = nullable extensions to C unsigned int
#     "nullable[unsigned intc]": type(dtype_element_types[np.dtype(np.uintc)]).instance(nullable=True),
#     "nullable[uintc]": type(dtype_element_types[np.dtype(np.uintc)]).instance(nullable=True),
#     "nullable[I]": type(dtype_element_types[np.dtype(np.uintc)]).instance(nullable=True),

#     # unsigned long = C unsigned long
#     "unsigned long": dtype_element_types[np.dtype(np.uint)],
#     "unsigned long int": dtype_element_types[np.dtype(np.uint)],
#     "ulong": dtype_element_types[np.dtype(np.uint)],
#     "L": dtype_element_types[np.dtype(np.uint)],

#     # nullable[unsigned long] = nullable extensions to C unsigned long
#     "nullable[unsigned long]": type(dtype_element_types[np.dtype(np.uint)]).instance(nullable=True),
#     "nullable[unsigned long int]": type(dtype_element_types[np.dtype(np.uint)]).instance(nullable=True),
#     "nullable[ulong]": type(dtype_element_types[np.dtype(np.uint)]).instance(nullable=True),
#     "nullable[L]": type(dtype_element_types[np.dtype(np.uint)]).instance(nullable=True),

#     # unsigned long long = C unsigned long long
#     "unsigned long long": dtype_element_types[np.dtype(np.ulonglong)],
#     "unsigned longlong": dtype_element_types[np.dtype(np.ulonglong)],
#     "unsigned long long int": dtype_element_types[np.dtype(np.ulonglong)],
#     "ulonglong": dtype_element_types[np.dtype(np.ulonglong)],
#     "Q": dtype_element_types[np.dtype(np.ulonglong)],

#     # nullable[unsigned long long] = nullable extensions to C unsigned long long
#     "nullable[unsigned long long]": type(dtype_element_types[np.dtype(np.ulonglong)]).instance(nullable=True),
#     "nullable[unsigned longlong]": type(dtype_element_types[np.dtype(np.ulonglong)]).instance(nullable=True),
#     "nullable[unsigned long long int]": type(dtype_element_types[np.dtype(np.ulonglong)]).instance(nullable=True),
#     "nullable[ulonglong]": type(dtype_element_types[np.dtype(np.ulonglong)]).instance(nullable=True),
#     "nullable[Q]": type(dtype_element_types[np.dtype(np.ulonglong)]).instance(nullable=True),

#     # size_t = C size_t
#     "size_t": dtype_element_types[np.dtype(np.uintp)],
#     "uintp": dtype_element_types[np.dtype(np.uintp)],
#     "uint0": dtype_element_types[np.dtype(np.uintp)],
#     "P": dtype_element_types[np.dtype(np.uintp)],

#     # nullable[size_t] = nullable extensions to C size_t
#     "nullable[size_t]": type(dtype_element_types[np.dtype(np.uintp)]).instance(nullable=True),
#     "nullable[uintp]": type(dtype_element_types[np.dtype(np.uintp)]).instance(nullable=True),
#     "nullable[uint0]": type(dtype_element_types[np.dtype(np.uintp)]).instance(nullable=True),
#     "nullable[P]": type(dtype_element_types[np.dtype(np.uintp)]).instance(nullable=True),

#     # float
#     "float": FloatType.instance(),
#     "floating": FloatType.instance(),
#     "f": FloatType.instance(),

#     # float16
#     "float16": Float16Type.instance(),
#     "f2": Float16Type.instance(),
#     "half": Float16Type.instance(),
#     "e": Float16Type.instance(),

#     # float32
#     "float32": Float32Type.instance(),
#     "f4": Float32Type.instance(),
#     "single": Float32Type.instance(),

#     # float64
#     "float64": Float64Type.instance(),
#     "f8": Float64Type.instance(),
#     "float_": Float64Type.instance(),
#     "double": Float64Type.instance(),
#     "d": Float64Type.instance(),

#     # longdouble = C long double
#     "longdouble": LongDoubleType.instance(),
#     "longfloat": LongDoubleType.instance(),
#     "long double": LongDoubleType.instance(),
#     "long float": LongDoubleType.instance(),
#     "float96": LongDoubleType.instance(),
#     "float128": LongDoubleType.instance(),
#     "f12": LongDoubleType.instance(),
#     "f16": LongDoubleType.instance(),
#     "g": LongDoubleType.instance(),

#     # complex
#     "complex": ComplexType.instance(),
#     "complex floating": ComplexType.instance(),
#     "complex float": ComplexType.instance(),
#     "cfloat": ComplexType.instance(),
#     "c": ComplexType.instance(),

#     # complex64
#     "complex64": Complex64Type.instance(),
#     "c8": Complex64Type.instance(),
#     "complex single": Complex64Type.instance(),
#     "csingle": Complex64Type.instance(),
#     "singlecomplex": Complex64Type.instance(),
#     "F": Complex64Type.instance(),

#     # complex128
#     "complex128": Complex128Type.instance(),
#     "c16": Complex128Type.instance(),
#     "complex double": Complex128Type.instance(),
#     "cdouble": Complex128Type.instance(),
#     "complex_": Complex128Type.instance(),
#     "D": Complex128Type.instance(),

#     # clongdouble = (complex) C long double
#     "clongdouble": CLongDoubleType.instance(),
#     "clongfloat": CLongDoubleType.instance(),
#     "complex longdouble": CLongDoubleType.instance(),
#     "complex longfloat": CLongDoubleType.instance(),
#     "complex long double": CLongDoubleType.instance(),
#     "complex long float": CLongDoubleType.instance(),
#     "complex192": CLongDoubleType.instance(),
#     "complex256": CLongDoubleType.instance(),
#     "c24": CLongDoubleType.instance(),
#     "c32": CLongDoubleType.instance(),
#     "G": CLongDoubleType.instance(),

#     # decimal
#     "decimal": DecimalType.instance(),

#     # datetime
#     "datetime": DatetimeType.instance(),

#     # datetime[pandas]
#     "datetime[pandas]": PandasTimestampType.instance(),
#     "pandas.Timestamp": PandasTimestampType.instance(),
#     "pandas Timestamp": PandasTimestampType.instance(),
#     "pd.Timestamp": PandasTimestampType.instance(),

#     # datetime[python]
#     "datetime[python]": PyDatetimeType.instance(),
#     "pydatetime": PyDatetimeType.instance(),
#     "datetime.datetime": PyDatetimeType.instance(),

#     # datetime[numpy]
#     "datetime[numpy]": NumpyDatetime64Type.instance(),
#     "numpy.datetime64": NumpyDatetime64Type.instance(),
#     "numpy datetime64": NumpyDatetime64Type.instance(),
#     "np.datetime64": NumpyDatetime64Type.instance(),
#     "M8": NumpyDatetime64Type.instance(),
#     # TODO: these go in M8 special case
#     # "M8[ns]", "datetime64[5us]", "M8[50ms]",
#     # "M8[2s]", "datetime64[30m]", "datetime64[h]", "M8[3D]",
#     # "datetime64[2W]", "M8[3M]", "datetime64[10Y]"

#     # timedelta
#     "timedelta": TimedeltaType.instance(),

#     # timedelta[pandas]
#     "timedelta[pandas]": PandasTimedeltaType.instance(),
#     "pandas.Timedelta": PandasTimedeltaType.instance(),
#     "pandas Timedelta": PandasTimedeltaType.instance(),
#     "pd.Timedelta": PandasTimedeltaType.instance(),

#     # timedelta[python]
#     "timedelta[python]": PyTimedeltaType.instance(),
#     "pytimedelta": PyTimedeltaType.instance(),
#     "datetime.timedelta": PyTimedeltaType.instance(),

#     # timedelta[numpy]
#     "timedelta[numpy]": NumpyTimedelta64Type.instance(),
#     "numpy.timedelta64": NumpyTimedelta64Type.instance(),
#     "numpy timedelta64": NumpyTimedelta64Type.instance(),
#     "np.timedelta64": NumpyTimedelta64Type.instance(),
#     "m8": NumpyTimedelta64Type.instance(),
#     # TODO: these go in m8 special case
#     # "m8[ns]", "timedelta64[5us]",
#     # "m8[50ms]", "m8[2s]", "timedelta64[30m]", "timedelta64[h]",
#     # "m8[3D]", "timedelta64[2W]", "m8[3M]", "timedelta64[10Y]"

#     # string
#     "str": StringType.instance(),
#     "string": StringType.instance(),
#     "unicode": StringType.instance(),
#     "U": StringType.instance(),
#     "str0": StringType.instance(),
#     "str_": StringType.instance(),
#     "unicode_": StringType.instance(),

#     # string[python]
#     "str[python]": StringType.instance(storage="python"),
#     "string[python]": StringType.instance(storage="python"),
#     "unicode[python]": StringType.instance(storage="python"),
#     "pystr": StringType.instance(storage="python"),
#     "pystring": StringType.instance(storage="python"),
#     "python string": StringType.instance(storage="python"),

#     # object
#     "object": ObjectType.instance(),
#     "obj": ObjectType.instance(),
#     "O": ObjectType.instance(),
#     "pyobject": ObjectType.instance(),
#     "object_": ObjectType.instance(),
#     "object0": ObjectType.instance(),
# }


# if PYARROW_INSTALLED:  # requires PYARROW dependency
#     dtype_element_types[pd.StringDtype("pyarrow")] = StringType.instance(storage="pyarrow")
#     string_element_types["str[pyarrow]"] = StringType.instance(storage="pyarrow")
#     string_element_types["unicode[pyarrow]"] = StringType.instance(storage="pyarrow")
#     string_element_types["pyarrow str"] = StringType.instance(storage="pyarrow")
#     string_element_types["pyarrow string"] = StringType.instance(storage="pyarrow")


# # MappingProxyType is immutable - prevents test-related side effects
# dtype_element_types = MappingProxyType(dtype_element_types)
# atomic_element_types = MappingProxyType(atomic_element_types)
# string_element_types = MappingProxyType(string_element_types)


# # TODO: ensure every numpy dtype is represented (np.sctypeDict.keys())


# #####################
# ####    TESTS    ####
# #####################


# # TODO: returns flyweights
# # TODO: accepts numpy/pandas dtype objects
# # TODO: accepts atomic types
# # TODO: accepts string type specifiers
# # TODO: accepts other ElementType objects
# # TODO: handles_errors (raise a ValueError)


# # @parametrize(data_model)
# # def test_resolve_dtype_returns_flyweights(case: ResolveDtypeCase):
# #     assert case.resolve() is case.resolve()


# # @parametrize(data_model)
# # def test_resolve_dtype_matches_data_model(case: ResolveDtypeCase):
# #     assert case.resolve() == case.output


# @pytest.mark.parametrize(
#     "test_input, test_output",
#     dtype_element_types.items()
# )
# def test_resolve_dtype_accepts_numpy_pandas_dtype_specifiers(
#     test_input, test_output
# ):
#     assert resolve_dtype(test_input) is test_output


# @pytest.mark.parametrize(
#     "test_input, test_output",
#     atomic_element_types.items()
# )
# def test_resolve_dtype_accepts_atomic_type_specifiers(
#     test_input, test_output
# ):
#     assert resolve_dtype(test_input) is test_output


# @pytest.mark.parametrize(
#     "test_input, test_output",
#     string_element_types.items()
# )
# def test_resolve_dtype_accepts_string_type_specifiers(
#     test_input, test_output
# ):
#     assert resolve_dtype(test_input) is test_output

