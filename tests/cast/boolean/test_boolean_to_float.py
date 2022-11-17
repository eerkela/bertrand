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
        pd.Series([1.0, 0.0], dtype=series_type)
    )

    return CastParameters(
        case("float", np.float16),
        case("float16", np.float16),
        case("float32", np.float16),
        case("float64", np.float16),
        case("longdouble", np.float16),
    ).with_na(pd.NA, np.nan)


#####################
####    VALID    ####
#####################


@parametrize(valid_input_data("float"))
def test_boolean_to_float_accepts_all_valid_inputs(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_float(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_float({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(valid_dtype_data("float"))
def test_boolean_to_float_accepts_all_valid_type_specifiers(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_float(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_float({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(downcast_data())
def test_boolean_to_float_downcasting(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_float(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_float({fmt_kwargs}) failed with input:\n"
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
def test_boolean_to_float_rejects_all_invalid_inputs(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError):
        BooleanSeries(test_input).to_float(**kwargs)

        # custom error message
        fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
        pytest.fail(
            f"BooleanSeries.to_float({fmt_kwargs}) did not reject "
            f"input data:\n"
            f"{test_input}"
        )


@parametrize(invalid_dtype_data("float"))
def test_boolean_to_float_rejects_all_invalid_type_specifiers(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError, match="`dtype` must be float-like"):
        BooleanSeries(test_input).to_float(**kwargs)

        # custom error message
        fmt_kwargs = ", ".join(
            f"{k}={repr(v)}" for k, v in kwargs.items() if k != "dtype"
        )
        pytest.fail(
            f"BooleanSeries.to_float({fmt_kwargs}) did not reject "
            f"dtype={repr(kwargs['dtype'])}"
        )


#####################
####    OTHER    ####
#####################


def test_boolean_to_float_preserves_index():
    # arrange
    case = CastCase(
        {},
        pd.Series(
            [True, False, pd.NA],
            index=[4, 5, 6],
            dtype=pd.BooleanDtype()
        ),
        pd.Series(
            [1.0, 0.0, np.nan],
            index=[4, 5, 6],
            dtype=np.float64
        )
    )

    # act
    result = BooleanSeries(case.test_input).to_float(**case.kwargs)

    # assert
    assert result.equals(case.test_output), (
        "BooleanSeries.to_float() does not preserve index"
    )
