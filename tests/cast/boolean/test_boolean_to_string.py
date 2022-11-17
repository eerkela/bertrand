import pandas as pd
import pytest

from tests.cast.scheme import CastCase, parametrize
from tests.cast.boolean import (
    valid_input_data, valid_dtype_data, invalid_input_data, invalid_dtype_data
)

from pdtypes.cast.boolean import BooleanSeries


#####################
####    VALID    ####
#####################


@parametrize(valid_input_data("string"))
def test_boolean_to_string_accepts_all_valid_inputs(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_string(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_string({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(valid_dtype_data("string"))
def test_boolean_to_string_accepts_all_valid_type_specifiers(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_string(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_string({fmt_kwargs}) failed with input:\n"
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
def test_boolean_to_string_rejects_all_invalid_inputs(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError):
        BooleanSeries(test_input).to_string(**kwargs)

        # custom error message
        fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
        pytest.fail(
            f"BooleanSeries.to_string({fmt_kwargs}) did not reject "
            f"input data:\n"
            f"{test_input}"
        )


@parametrize(invalid_dtype_data("string"))
def test_boolean_to_string_rejects_all_invalid_type_specifiers(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError, match="`dtype` must be string-like"):
        BooleanSeries(test_input).to_string(**kwargs)

        # custom error message
        fmt_kwargs = ", ".join(
            f"{k}={repr(v)}" for k, v in kwargs.items() if k != "dtype"
        )
        pytest.fail(
            f"BooleanSeries.to_string({fmt_kwargs}) did not reject "
            f"dtype={repr(kwargs['dtype'])}"
        )


#####################
####    OTHER    ####
#####################


def test_boolean_to_string_preserves_index():
    # arrange
    case = CastCase(
        {"dtype": "str[python]"},
        pd.Series(
            [True, False, pd.NA],
            index=[4, 5, 6],
            dtype=pd.BooleanDtype()
        ),
        pd.Series(
            ["True", "False", pd.NA],
            index=[4, 5, 6],
            dtype=pd.StringDtype("python")
        )
    )

    # act
    result = BooleanSeries(case.test_input).to_string(**case.kwargs)

    # assert
    assert result.equals(case.test_output), (
        "BooleanSeries.to_string() does not preserve index"
    )
