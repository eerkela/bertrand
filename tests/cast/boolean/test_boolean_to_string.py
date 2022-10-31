import itertools

import numpy as np
import pandas as pd
import pytest

from tests import make_parameters

from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED
from pdtypes.cast.boolean import BooleanSeries


####################
####    DATA    ####
####################


def valid_input(expected_dtype):
    return [
        # scalar
        (True, pd.Series("True", dtype=expected_dtype)),
        (False, pd.Series("False", dtype=expected_dtype)),

        # iterable
        ([True, False], pd.Series(["True", "False"], dtype=expected_dtype)),
        ((False, True), pd.Series(["False", "True"], dtype=expected_dtype)),
        ((x for x in [True, False, None]), pd.Series(["True", "False", pd.NA], dtype=expected_dtype)),
        ([True, False, pd.NA], pd.Series(["True", "False", pd.NA], dtype=expected_dtype)),
        ([True, False, np.nan], pd.Series(["True", "False", pd.NA], dtype=expected_dtype)),

        # scalar array
        (np.array(True), pd.Series("True", dtype=expected_dtype)),
        (np.array(True, dtype="O"), pd.Series("True", dtype=expected_dtype)),

        # 1D array (numpy dtype)
        (np.array([True, False]), pd.Series(["True", "False"], dtype=expected_dtype)),

        # 1D array (object dtype)
        (np.array([True, False], dtype="O"), pd.Series(["True", "False"], dtype=expected_dtype)),
        (np.array([True, False, None], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=expected_dtype)),
        (np.array([True, False, pd.NA], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=expected_dtype)),
        (np.array([True, False, np.nan], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=expected_dtype)),

        # series (numpy dtype)
        (pd.Series([True, False]), pd.Series(["True", "False"], dtype=expected_dtype)),

        # series (object dtype)
        (pd.Series([True, False], dtype="O"), pd.Series(["True", "False"], dtype=expected_dtype)),
        (pd.Series([True, False, None], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=expected_dtype)),
        (pd.Series([True, False, pd.NA], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=expected_dtype)),
        (pd.Series([True, False, np.nan], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=expected_dtype)),

        # series (pandas dtype)
        (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series(["True", "False", pd.NA], dtype=expected_dtype))
    ]


string_valid_input = [
    make_parameters("str", valid_input(DEFAULT_STRING_DTYPE)),
    make_parameters("str[python]", valid_input(pd.StringDtype("python")))
]
if PYARROW_INSTALLED:
    string_valid_input.append(
        make_parameters("str[pyarrow]", valid_input(pd.StringDtype("pyarrow")))
    )
string_valid_input = list(itertools.chain(*string_valid_input))


#####################
####    TESTS    ####
#####################


@pytest.mark.parametrize(
    "target_dtype, test_input, expected_result",
    string_valid_input
)
def test_boolean_to_string_accepts_all_valid_inputs(
    target_dtype, test_input, expected_result
):
    result = BooleanSeries(test_input).to_string(dtype=target_dtype)
    assert result.equals(expected_result), (
        f"BooleanSeries.to_string(dtype={repr(target_dtype)}) failed with "
        f"input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{expected_result}\n"
        f"received:\n"
        f"{result}"
    )


def test_boolean_to_string_preserves_index():
    # arrange
    val = pd.Series(
        [True, False, pd.NA],
        index=[4, 5, 6],
        dtype=pd.BooleanDtype()
    )

    # act
    result = BooleanSeries(val).to_string("str[python]")

    # assert
    expected = pd.Series(
        ["True", "False", pd.NA],
        index=[4, 5, 6],
        dtype=pd.StringDtype("python")
    )
    assert result.equals(expected), (
        "BooleanSeries.to_string() does not preserve index"
    )
