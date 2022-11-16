import numpy as np
import pandas as pd
import pytest

from tests.cast.scheme import CastCase, CastParameters, parametrize
from tests.cast.boolean import (
    valid_input_data, valid_dtype_data, invalid_input_data, invalid_dtype_data
)

from pdtypes.cast.boolean import BooleanSeries


####################
####    DATA    ####
####################


def downcast_data():
    case = lambda target_dtype, series_type: CastCase(
        {"dtype": target_dtype, "downcast": True},
        pd.Series([True, False]),
        pd.Series([1, 0], dtype=series_type)
    )

    return CastParameters(
        # non-nullable
        case("int", np.int8),
        case("signed", np.int8),
        case("unsigned", np.uint8),
        case("int8", np.int8),
        case("int16", np.int8),
        case("int32", np.int8),
        case("int64", np.int8),
        case("uint8", np.uint8),
        case("uint16", np.uint8),
        case("uint32", np.uint8),
        case("uint64", np.uint8),

        # nullable
        case("nullable[int]", pd.Int8Dtype()),
        case("nullable[signed]", pd.Int8Dtype()),
        case("nullable[unsigned]", pd.UInt8Dtype()),
        case("nullable[int8]", pd.Int8Dtype()),
        case("nullable[int16]", pd.Int8Dtype()),
        case("nullable[int32]", pd.Int8Dtype()),
        case("nullable[int64]", pd.Int8Dtype()),
        case("nullable[uint8]", pd.UInt8Dtype()),
        case("nullable[uint16]", pd.UInt8Dtype()),
        case("nullable[uint32]", pd.UInt8Dtype()),
        case("nullable[uint64]", pd.UInt8Dtype()),
    ).with_na(pd.NA, pd.NA)


#####################
####    VALID    ####
#####################


@parametrize(valid_input_data("integer"))
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


@parametrize(valid_dtype_data("integer"))
def test_boolean_to_integer_accepts_all_valid_type_specifiers(
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


@parametrize(downcast_data())
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


# #######################
# ####    INVALID    ####
# #######################


@parametrize(invalid_input_data())
def test_boolean_to_integer_rejects_all_invalid_inputs(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError):
        BooleanSeries(test_input).to_integer(**kwargs)


@parametrize(invalid_dtype_data("integer"))
def test_boolean_to_integer_rejects_all_invalid_type_specifiers(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError, match="`dtype` must be int-like"):
        BooleanSeries(test_input).to_integer(**kwargs)


#####################
####    OTHER    ####
#####################


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
