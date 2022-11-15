from decimal import Decimal

import pandas as pd
import pytest

from tests.cast import parametrize
from tests.cast.boolean import (
    valid_input_data, valid_dtype_data, invalid_input_data, invalid_dtype_data
)

from pdtypes.cast.boolean import BooleanSeries


#####################
####    VALID    ####
#####################


@parametrize(valid_input_data("decimal"))
def test_boolean_to_decimal_accepts_all_valid_inputs(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_decimal(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_decimal({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


# @parametrize(valid_dtype_data("decimal").with_na(pd.NA, pd.NA))
# def test_boolean_to_decimal_accepts_all_valid_type_specifiers(
#     kwargs, test_input, test_output
# ):
#     fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
#     result = BooleanSeries(test_input).to_decimal(**kwargs)
#     assert result.equals(test_output), (
#         f"BooleanSeries.to_decimal({fmt_kwargs}) failed with input:\n"
#         f"{test_input}\n"
#         f"expected:\n"
#         f"{test_output}\n"
#         f"received:\n"
#         f"{result}"
#     )


#######################
####    INVALID    ####
#######################


@parametrize(invalid_input_data())
def test_boolean_to_decimal_rejects_all_invalid_inputs(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError):
        BooleanSeries(test_input).to_decimal(**kwargs)


@parametrize(invalid_dtype_data("decimal"))
def test_boolean_to_decimal_rejects_all_invalid_type_specifiers(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError, match="`dtype` must be decimal-like"):
        BooleanSeries(test_input).to_decimal(**kwargs)


#####################
####    OTHER    ####
#####################


def test_boolean_to_decimal_preserves_index():
    # arrange
    val = pd.Series(
        [True, False, pd.NA],
        index=[4, 5, 6],
        dtype=pd.BooleanDtype()
    )

    # act
    result = BooleanSeries(val).to_decimal()

    # assert
    expected = pd.Series(
        [Decimal(1), Decimal(0), pd.NA],
        index=[4, 5, 6],
        dtype="O"
    )
    assert result.equals(expected), (
        "BooleanSeries.to_decimal() does not preserve index"
    )
