import itertools

import numpy as np
import pandas as pd
import pytest

from tests import make_parameters

from pdtypes.cast.boolean import BooleanSeries


####################
####    DATA    ####
####################


def valid_input(non_nullable_dtype, nullable_dtype):
    return [
        # scalar
        (True, pd.Series(1, dtype=non_nullable_dtype)),
        (False, pd.Series(0, dtype=non_nullable_dtype)),

        # iterable
        ([True, False], pd.Series([1, 0], dtype=non_nullable_dtype)),
        ((False, True), pd.Series([0, 1], dtype=non_nullable_dtype)),
        ((x for x in [True, False, None]), pd.Series([1, 0, None], dtype=nullable_dtype)),
        ([True, False, pd.NA], pd.Series([1, 0, None], dtype=nullable_dtype)),
        ([True, False, np.nan], pd.Series([1, 0, None], dtype=nullable_dtype)),

        # scalar array
        (np.array(True), pd.Series(1, dtype=non_nullable_dtype)),
        (np.array(True, dtype="O"), pd.Series(1, dtype=non_nullable_dtype)),

        # 1D array (numpy dtype)
        (np.array([True, False]), pd.Series([1, 0], dtype=non_nullable_dtype)),

        # 1D array (object dtype)
        (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=non_nullable_dtype)),
        (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=nullable_dtype)),
        (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=nullable_dtype)),
        (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=nullable_dtype)),

        # series (numpy dtype)
        (pd.Series([True, False]), pd.Series([1, 0], dtype=non_nullable_dtype)),

        # series (object dtype)
        (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=non_nullable_dtype)),
        (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=nullable_dtype)),
        (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=nullable_dtype)),
        (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=nullable_dtype)),

        # series (pandas dtype)
        (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=nullable_dtype))
    ]


def downcast_input(non_nullable_dtype, nullable_dtype):
    return [
        ([True, False], pd.Series([1, 0], dtype=non_nullable_dtype)),
        ([True, False, None], pd.Series([1, 0, pd.NA], dtype=nullable_dtype))
    ]


#####################
####    TESTS    ####
#####################


@pytest.mark.parametrize(
    "target_dtype, test_input, expected_result",
    list(itertools.chain(*[
        # non-nullable
        make_parameters("int", valid_input(np.int64, pd.Int64Dtype())),
        make_parameters("signed", valid_input(np.int64, pd.Int64Dtype())),
        make_parameters("unsigned", valid_input(np.uint64, pd.UInt64Dtype())),
        make_parameters("int8", valid_input(np.int8, pd.Int8Dtype())),
        make_parameters("int16", valid_input(np.int16, pd.Int16Dtype())),
        make_parameters("int32", valid_input(np.int32, pd.Int32Dtype())),
        make_parameters("int64", valid_input(np.int64, pd.Int64Dtype())),
        make_parameters("uint8", valid_input(np.uint8, pd.UInt8Dtype())),
        make_parameters("uint16", valid_input(np.uint16, pd.UInt16Dtype())),
        make_parameters("uint32", valid_input(np.uint32, pd.UInt32Dtype())),
        make_parameters("uint64", valid_input(np.uint64, pd.UInt64Dtype())),

        # nullable
        make_parameters("nullable[int]", valid_input(pd.Int64Dtype(), pd.Int64Dtype())),
        make_parameters("nullable[signed]", valid_input(pd.Int64Dtype(), pd.Int64Dtype())),
        make_parameters("nullable[unsigned]", valid_input(pd.UInt64Dtype(), pd.UInt64Dtype())),
        make_parameters("nullable[int8]", valid_input(pd.Int8Dtype(), pd.Int8Dtype())),
        make_parameters("nullable[int16]", valid_input(pd.Int16Dtype(), pd.Int16Dtype())),
        make_parameters("nullable[int32]", valid_input(pd.Int32Dtype(), pd.Int32Dtype())),
        make_parameters("nullable[int64]", valid_input(pd.Int64Dtype(), pd.Int64Dtype())),
        make_parameters("nullable[uint8]", valid_input(pd.UInt8Dtype(), pd.UInt8Dtype())),
        make_parameters("nullable[uint16]", valid_input(pd.UInt16Dtype(), pd.UInt16Dtype())),
        make_parameters("nullable[uint32]", valid_input(pd.UInt32Dtype(), pd.UInt32Dtype())),
        make_parameters("nullable[uint64]", valid_input(pd.UInt64Dtype(), pd.UInt64Dtype())),
    ]))
)
def test_boolean_to_integer_accepts_all_valid_inputs(
    target_dtype, test_input, expected_result
):
    result = BooleanSeries(test_input).to_integer(target_dtype)
    assert result.equals(expected_result), (
        f"BooleanSeries.to_integer(dtype={repr(target_dtype)}) failed with "
        f"input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{expected_result}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize(
    "target_dtype, test_input, expected_result",
    list(itertools.chain(*[
        # non-nullable
        make_parameters("int", downcast_input(np.int8, pd.Int8Dtype())),
        make_parameters("signed", downcast_input(np.int8, pd.Int8Dtype())),
        make_parameters("unsigned", downcast_input(np.uint8, pd.UInt8Dtype())),
        make_parameters("int8", downcast_input(np.int8, pd.Int8Dtype())),
        make_parameters("int16", downcast_input(np.int8, pd.Int8Dtype())),
        make_parameters("int32", downcast_input(np.int8, pd.Int8Dtype())),
        make_parameters("int64", downcast_input(np.int8, pd.Int8Dtype())),
        make_parameters("uint8", downcast_input(np.uint8, pd.UInt8Dtype())),
        make_parameters("uint16", downcast_input(np.uint8, pd.UInt8Dtype())),
        make_parameters("uint32", downcast_input(np.uint8, pd.UInt8Dtype())),
        make_parameters("uint64", downcast_input(np.uint8, pd.UInt8Dtype())),

        # nullable
        make_parameters("nullable[int]", downcast_input(pd.Int8Dtype(), pd.Int8Dtype())),
        make_parameters("nullable[signed]", downcast_input(pd.Int8Dtype(), pd.Int8Dtype())),
        make_parameters("nullable[unsigned]", downcast_input(pd.UInt8Dtype(), pd.UInt8Dtype())),
        make_parameters("nullable[int8]", downcast_input(pd.Int8Dtype(), pd.Int8Dtype())),
        make_parameters("nullable[int16]", downcast_input(pd.Int8Dtype(), pd.Int8Dtype())),
        make_parameters("nullable[int32]", downcast_input(pd.Int8Dtype(), pd.Int8Dtype())),
        make_parameters("nullable[int64]", downcast_input(pd.Int8Dtype(), pd.Int8Dtype())),
        make_parameters("nullable[uint8]", downcast_input(pd.UInt8Dtype(), pd.UInt8Dtype())),
        make_parameters("nullable[uint16]", downcast_input(pd.UInt8Dtype(), pd.UInt8Dtype())),
        make_parameters("nullable[uint32]", downcast_input(pd.UInt8Dtype(), pd.UInt8Dtype())),
        make_parameters("nullable[uint64]", downcast_input(pd.UInt8Dtype(), pd.UInt8Dtype())),
    ]))
)
def test_boolean_to_integer_downcasting(
    target_dtype, test_input, expected_result
):
    series = BooleanSeries(test_input)
    result = series.to_integer(dtype=target_dtype, downcast=True)
    assert result.equals(expected_result), (
        f"BooleanSeries.to_integer(dtype={repr(target_dtype)}, downcast=True) "
        f"failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{expected_result}\n"
        f"received:\n"
        f"{result}"
    )


def test_boolean_to_integer_preserves_index():
    # arrange
    val = pd.Series(
        [True, False, pd.NA],
        index=[4, 5, 6],
        dtype=pd.BooleanDtype()
    )
    expected = pd.Series(
        [1, 0, pd.NA],
        index=[4, 5, 6],
        dtype=pd.Int64Dtype()
    )

    # act
    result = BooleanSeries(val).to_integer()

    # assert
    assert result.equals(expected), (
        "BooleanSeries.to_integer() does not preserve index"
    )
