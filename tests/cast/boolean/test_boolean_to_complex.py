import itertools

import numpy as np
import pandas as pd
import pytest

from tests import make_parameters

from pdtypes.cast.boolean import BooleanSeries


####################
####    DATA    ####
####################


def valid_input(expected_dtype):
    return [
        # scalar
        (True, pd.Series(1+0j, dtype=expected_dtype)),
        (False, pd.Series(0+0j, dtype=expected_dtype)),

        # iterable
        ([True, False], pd.Series([1+0j, 0+0j], dtype=expected_dtype)),
        ((False, True), pd.Series([0+0j, 1+0j], dtype=expected_dtype)),
        ((x for x in [True, False, None]), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=expected_dtype)),
        ([True, False, pd.NA], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=expected_dtype)),
        ([True, False, np.nan], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=expected_dtype)),

        # scalar array
        (np.array(True), pd.Series(1+0j, dtype=expected_dtype)),
        (np.array(True, dtype="O"), pd.Series(1+0j, dtype=expected_dtype)),

        # 1D array (numpy dtype)
        (np.array([True, False]), pd.Series([1+0j, 0+0j], dtype=expected_dtype)),

        # 1D array (object dtype)
        (np.array([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=expected_dtype)),
        (np.array([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=expected_dtype)),
        (np.array([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=expected_dtype)),
        (np.array([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=expected_dtype)),

        # series (numpy dtype)
        (pd.Series([True, False]), pd.Series([1+0j, 0+0j], dtype=expected_dtype)),

        # series (object dtype)
        (pd.Series([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=expected_dtype)),
        (pd.Series([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=expected_dtype)),
        (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=expected_dtype)),
        (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=expected_dtype)),

        # series (pandas dtype)
        (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=expected_dtype))
    ]


def downcast_input(expected_dtype):
    return [
        ([True, False], pd.Series([1+0j, 0+0j], dtype=expected_dtype)),
        ([True, False, None], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=expected_dtype)),
    ]


#######################
####    GENERAL    ####
#######################


@pytest.mark.parametrize(
    "target_dtype, test_input, expected_result",
    list(itertools.chain(*[
        make_parameters("complex", valid_input(np.complex128)),
        make_parameters("complex64", valid_input(np.complex64)),
        make_parameters("complex128", valid_input(np.complex128)),
        make_parameters("clongdouble", valid_input(np.clongdouble)),
    ]))
)
def test_boolean_to_complex_accepts_all_valid_inputs(
    target_dtype, test_input, expected_result
):
    result = BooleanSeries(test_input).to_complex(target_dtype)
    assert result.equals(expected_result), (
        f"BooleanSeries.to_complex(dtype={repr(target_dtype)}) failed with "
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
        make_parameters("complex", downcast_input(np.complex64)),
        make_parameters("complex64", downcast_input(np.complex64)),
        make_parameters("complex128", downcast_input(np.complex64)),
        make_parameters("clongdouble", downcast_input(np.complex64)),
    ]))
)
def test_boolean_to_complex_downcasting(
    target_dtype, test_input, expected_result
):
    series = BooleanSeries(test_input)
    result = series.to_complex(dtype=target_dtype, downcast=True)
    assert result.equals(expected_result), (
        f"BooleanSeries.to_complex(dtype={repr(target_dtype)}, downcast=True) "
        f"failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{expected_result}\n"
        f"received:\n"
        f"{result}"
    )


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
