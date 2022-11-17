import pandas as pd
import pytest

from tests.cast.scheme import CastCase, parametrize
from tests.cast.boolean import (
    valid_input_data, valid_dtype_data, invalid_input_data, invalid_dtype_data
)

from pdtypes.cast.boolean import BooleanSeries


# with parametrized failures, use
# test_boolean_to_boolean_filters_input_data_formats
# test_boolean_to_boolean_filters_type_specifiers



###################
####   VALID   ####
###################


@parametrize(valid_input_data("boolean"))
def test_boolean_to_boolean_accepts_valid_input_formats(
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
def test_boolean_to_boolean_accepts_valid_type_specifiers(
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
def test_boolean_to_boolean_rejects_invalid_input_formats(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError):
        BooleanSeries(test_input).to_boolean(**kwargs)

        # custom error message
        fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
        pytest.fail(
            f"BooleanSeries.to_boolean({fmt_kwargs}) did not reject "
            f"input data:\n"
            f"{test_input}"
        )


@parametrize(invalid_dtype_data("boolean"))
def test_boolean_to_boolean_rejects_invalid_type_specifiers(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError, match="`dtype` must be bool-like"):
        BooleanSeries(test_input).to_boolean(**kwargs)

        # custom error message
        fmt_kwargs = ", ".join(
            f"{k}={repr(v)}" for k, v in kwargs.items() if k != "dtype"
        )
        pytest.fail(
            f"BooleanSeries.to_boolean({fmt_kwargs}) did not reject "
            f"dtype={repr(kwargs['dtype'])}"
        )


#####################
####    OTHER    ####
#####################


def test_boolean_to_boolean_preserves_index():
    # arrange
    case = CastCase(
        {},
        pd.Series(
            [True, False, pd.NA],
            index=[4, 5, 6],
            dtype=pd.BooleanDtype()
        ),
        pd.Series(
            [True, False, pd.NA],
            index=[4, 5, 6],
            dtype=pd.BooleanDtype()
        )
    )

    # act
    result = BooleanSeries(case.test_input).to_boolean(**case.kwargs)

    # assert
    assert result.equals(case.test_output), (
        "BooleanSeries.to_boolean() does not preserve index"
    )
