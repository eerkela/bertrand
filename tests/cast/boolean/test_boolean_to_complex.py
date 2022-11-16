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
        pd.Series([1+0j, 0+0j], dtype=series_type)
    )

    return CastParameters(
        case("complex", np.complex64),
        case("complex64", np.complex64),
        case("complex128", np.complex64),
        case("clongdouble", np.complex64),
    ).with_na(pd.NA, complex("nan+nanj"))


#####################
####    VALID    ####
#####################


@parametrize(valid_input_data("complex"))
def test_boolean_to_complex_accepts_all_valid_inputs(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_complex(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_complex({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(valid_dtype_data("complex"))
def test_boolean_to_complex_accepts_all_valid_type_specifiers(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_complex(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_complex({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(downcast_data())
def test_boolean_to_complex_downcasting(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_complex(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_complex({fmt_kwargs}) failed with input:\n"
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
def test_boolean_to_complex_rejects_all_invalid_inputs(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError):
        BooleanSeries(test_input).to_complex(**kwargs)


@parametrize(invalid_dtype_data("complex"))
def test_boolean_to_complex_rejects_all_invalid_type_specifiers(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError, match="`dtype` must be complex-like"):
        BooleanSeries(test_input).to_complex(**kwargs)


#####################
####    OTHER    ####
#####################


def test_boolean_to_complex_preserves_index():
    # arrange
    val = pd.Series(
        [True, False, pd.NA],
        index=[4, 5, 6],
        dtype=pd.BooleanDtype()
    )

    # act
    result = BooleanSeries(val).to_complex()

    # assert
    expected = pd.Series(
        [1+0j, 0+0j, complex("nan+nanj")],
        index=[4, 5, 6],
        dtype=np.complex128
    )
    assert result.equals(expected), (
        "BooleanSeries.to_complex() does not preserve index"
    )
