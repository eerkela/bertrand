import pandas as pd
import pytest

from tests.cast.scheme import parametrize
from tests.cast.boolean import (
    valid_input_data, valid_dtype_data, invalid_input_data, invalid_dtype_data
)

from pdtypes.cast.boolean import BooleanSeries


###################
####   VALID   ####
###################


@parametrize(valid_input_data("boolean"))
def test_boolean_to_boolean_accepts_all_valid_inputs(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_boolean(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_boolean({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(valid_dtype_data("boolean"))
def test_boolean_to_boolean_accepts_all_valid_type_specifiers(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_boolean(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_boolean({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


#######################
####    INVALID    ####
#######################


@parametrize(invalid_input_data())
def test_boolean_to_boolean_rejects_all_invalid_inputs(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError):
        BooleanSeries(test_input).to_boolean(**kwargs)


@parametrize(invalid_dtype_data("boolean"))
def test_boolean_to_boolean_rejects_all_invalid_type_specifiers(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError, match="`dtype` must be bool-like"):
        BooleanSeries(test_input).to_boolean(**kwargs)


#####################
####    OTHER    ####
#####################


def test_boolean_to_boolean_preserves_index():
    # arrange
    val = pd.Series(
        [True, False, pd.NA],
        index=[4, 5, 6],
        dtype=pd.BooleanDtype()
    )
    expected = val.copy()

    # act
    result = BooleanSeries(val).to_boolean()

    # assert
    assert result.equals(expected), (
        "BooleanSeries.to_boolean() does not preserve index"
    )
