import itertools

import numpy as np
import pandas as pd
import pytest

from tests import skip

from tests.cast import Parameters

from pdtypes.cast.boolean import BooleanSeries


# TODO: test_boolean_to_integer_handles_all_type_specifiers
# -> some xfail params in there as well


####################
####    DATA    ####
####################


def valid_input(dtype_no_na, dtype_with_na):
    create_record = lambda test_input, test_output: (
        {},
        test_input,
        test_output
    )

    return [
        # scalar
        create_record(
            True,
            pd.Series([1], dtype=dtype_no_na)
        ),
        create_record(
            False,
            pd.Series([0], dtype=dtype_no_na)
        ),

        # iterable
        create_record(
            [True, False],
            pd.Series([1, 0], dtype=dtype_no_na)
        ),
        create_record(
            (False, True),
            pd.Series([0, 1], dtype=dtype_no_na)
        ),
        create_record(
            (x for x in [True, False, None]),
            pd.Series([1, 0, None], dtype=dtype_with_na)
        ),
        create_record(
            [True, False, pd.NA],
            pd.Series([1, 0, None], dtype=dtype_with_na)
        ),
        create_record(
            [True, False, np.nan],
            pd.Series([1, 0, None], dtype=dtype_with_na)
        ),

        # numpy array
        create_record(
            np.array(True),
            pd.Series([1], dtype=dtype_no_na)
        ),
        create_record(
            np.array(True, dtype="O"),
            pd.Series([1], dtype=dtype_no_na)
        ),
        create_record(
            np.array([True, False]),
            pd.Series([1, 0], dtype=dtype_no_na)
        ),
        create_record(
            np.array([True, False], dtype="O"),
            pd.Series([1, 0], dtype=dtype_no_na)
        ),
        create_record(
            np.array([True, False, None], dtype="O"),
            pd.Series([1, 0, pd.NA], dtype=dtype_with_na)
        ),
        create_record(
            np.array([True, False, pd.NA], dtype="O"),
            pd.Series([1, 0, pd.NA], dtype=dtype_with_na)
        ),
        create_record(
            np.array([True, False, np.nan], dtype="O"),
            pd.Series([1, 0, pd.NA], dtype=dtype_with_na)
        ),

        # pandas Series
        create_record(
            pd.Series([True, False]),
            pd.Series([1, 0], dtype=dtype_no_na)
        ),
        create_record(
            pd.Series([True, False], dtype="O"),
            pd.Series([1, 0], dtype=dtype_no_na)
        ),
        create_record(
            pd.Series([True, False, None], dtype="O"),
            pd.Series([1, 0, pd.NA], dtype=dtype_with_na)
        ),
        create_record(
            pd.Series([True, False, pd.NA], dtype="O"),
            pd.Series([1, 0, pd.NA], dtype=dtype_with_na)
        ),
        create_record(
            pd.Series([True, False, np.nan], dtype="O"),
            pd.Series([1, 0, pd.NA], dtype=dtype_with_na)
        ),
        create_record(
            pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype()),
            pd.Series([1, 0, pd.NA], dtype=dtype_with_na)
        ),
    ]


def downcast_input():
    create_record = lambda target_dtype, series_type: (
        {"dtype": target_dtype, "downcast": True},
        pd.Series([True, False]),
        pd.Series([1, 0], dtype=series_type)
    )

    return [
        # non-nullable
        create_record("int", np.int8),
        create_record("signed", np.int8),
        create_record("unsigned", np.uint8),
        create_record("int8", np.int8),
        create_record("int16", np.int8),
        create_record("int32", np.int8),
        create_record("int64", np.int8),
        create_record("uint8", np.uint8),
        create_record("uint16", np.uint8),
        create_record("uint32", np.uint8),
        create_record("uint64", np.uint8),

        # nullable
        create_record("nullable[int]", pd.Int8Dtype()),
        create_record("nullable[signed]", pd.Int8Dtype()),
        create_record("nullable[unsigned]", pd.UInt8Dtype()),
        create_record("nullable[int8]", pd.Int8Dtype()),
        create_record("nullable[int16]", pd.Int8Dtype()),
        create_record("nullable[int32]", pd.Int8Dtype()),
        create_record("nullable[int64]", pd.Int8Dtype()),
        create_record("nullable[uint8]", pd.UInt8Dtype()),
        create_record("nullable[uint16]", pd.UInt8Dtype()),
        create_record("nullable[uint32]", pd.UInt8Dtype()),
        create_record("nullable[uint64]", pd.UInt8Dtype()),
    ]


#####################
####    TESTS    ####
#####################


@pytest.mark.parametrize(
    "kwargs, test_input, test_output",
    list(itertools.chain(*[
        # non-nullable
        Parameters.nocheck(
            valid_input(np.int64, pd.Int64Dtype()),
            dtype="int"
        ).apply(lambda rec: skip(*rec)),
        Parameters.nocheck(
            valid_input(np.int64, pd.Int64Dtype()),
            dtype="signed"
        ),
        Parameters.nocheck(
            valid_input(np.uint64, pd.UInt64Dtype()),
            dtype="unsigned"
        ),
        Parameters.nocheck(
            valid_input(np.int8, pd.Int8Dtype()),
            dtype="int8"
        ),
        Parameters.nocheck(
            valid_input(np.int16, pd.Int16Dtype()),
            dtype="int16"
        ),
        Parameters.nocheck(
            valid_input(np.int32, pd.Int32Dtype()),
            dtype="int32"
        ),
        Parameters.nocheck(
            valid_input(np.int64, pd.Int64Dtype()),
            dtype="int64",
        ),
        Parameters.nocheck(
            valid_input(np.uint8, pd.UInt8Dtype()),
            dtype="uint8",
        ),
        Parameters.nocheck(
            valid_input(np.uint16, pd.UInt16Dtype()),
            dtype="uint16",
        ),
        Parameters.nocheck(
            valid_input(np.uint32, pd.UInt32Dtype()),
            dtype="uint32",
        ),
        Parameters.nocheck(
            valid_input(np.uint64, pd.UInt64Dtype()),
            dtype="uint64",
        ),

        # nullable
        Parameters.nocheck(
            valid_input(pd.Int64Dtype(), pd.Int64Dtype()),
            dtype="nullable[int]",
        ),
        Parameters.nocheck(
            valid_input(pd.Int64Dtype(), pd.Int64Dtype()),
            dtype="nullable[signed]",
        ),
        Parameters.nocheck(
            valid_input(pd.UInt64Dtype(), pd.UInt64Dtype()),
            dtype="nullable[unsigned]",
        ),
        Parameters.nocheck(
            valid_input(pd.Int8Dtype(), pd.Int8Dtype()),
            dtype="nullable[int8]",
        ),
        Parameters.nocheck(
            valid_input(pd.Int16Dtype(), pd.Int16Dtype()),
            dtype="nullable[int16]",
        ),
        Parameters.nocheck(
            valid_input(pd.Int32Dtype(), pd.Int32Dtype()),
            dtype="nullable[int32]",
        ),
        Parameters.nocheck(
            valid_input(pd.Int64Dtype(), pd.Int64Dtype()),
            dtype="nullable[int64]",
        ),
        Parameters.nocheck(
            valid_input(pd.UInt8Dtype(), pd.UInt8Dtype()),
            dtype="nullable[uint8]",
        ),
        Parameters.nocheck(
            valid_input(pd.UInt16Dtype(), pd.UInt16Dtype()),
            dtype="nullable[uint16]",
        ),
        Parameters.nocheck(
            valid_input(pd.UInt32Dtype(), pd.UInt32Dtype()),
            dtype="nullable[uint32]",
        ),
        Parameters.nocheck(
            valid_input(pd.UInt64Dtype(), pd.UInt64Dtype()),
            dtype="nullable[uint64]",
        ),
    ]))
)
def test_boolean_to_integer_accepts_all_valid_inputs(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_integer(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_integer({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize(
    "kwargs, test_input, test_output",
    Parameters(downcast_input()).repeat_with_na(pd.NA, pd.NA)
)
def test_boolean_to_integer_downcasting(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_integer(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_integer({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
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
