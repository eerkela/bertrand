import itertools

import numpy as np
import pandas as pd
import pytest

from tests import make_parameters

from pdtypes.cast.boolean import BooleanSeries


def valid_input(non_nullable_dtype, nullable_dtype):
    return [
        # scalar
        (True, pd.Series(True, dtype=non_nullable_dtype)),
        (False, pd.Series(False, dtype=non_nullable_dtype)),

        # iterable
        ([True, False], pd.Series([True, False], dtype=non_nullable_dtype)),
        ((False, True), pd.Series([False, True], dtype=non_nullable_dtype)),
        ((x for x in [True, False, None]), pd.Series([True, False, pd.NA], dtype=nullable_dtype)),
        ([True, False, pd.NA], pd.Series([True, False, pd.NA], dtype=nullable_dtype)),
        ([True, False, np.nan], pd.Series([True, False, pd.NA], dtype=nullable_dtype)),

        # scalar array
        (np.array(True), pd.Series(True, dtype=non_nullable_dtype)),
        (np.array(True, dtype="O"), pd.Series(True, dtype=non_nullable_dtype)),

        # 1D array (numpy dtype)
        (np.array([True, False]), pd.Series([True, False], dtype=non_nullable_dtype)),

        # 1D array (object dtype)
        (np.array([True, False], dtype="O"), pd.Series([True, False], dtype=non_nullable_dtype)),
        (np.array([True, False, None], dtype="O"), pd.Series([True, False, pd.NA], dtype=nullable_dtype)),
        (np.array([True, False, pd.NA], dtype="O"), pd.Series([True, False, pd.NA], dtype=nullable_dtype)),
        (np.array([True, False, np.nan], dtype="O"), pd.Series([True, False, pd.NA], dtype=nullable_dtype)),

        # series (numpy dtype)
        (pd.Series([True, False]), pd.Series([True, False], dtype=non_nullable_dtype)),

        # series (object dtype)
        (pd.Series([True, False], dtype="O"), pd.Series([True, False], dtype=non_nullable_dtype)),
        (pd.Series([True, False, None], dtype="O"), pd.Series([True, False, pd.NA], dtype=nullable_dtype)),
        (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([True, False, pd.NA], dtype=nullable_dtype)),
        (pd.Series([True, False, np.nan], dtype="O"), pd.Series([True, False, pd.NA], dtype=nullable_dtype)),

        # series (pandas dtype)
        (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([True, False, pd.NA], dtype=nullable_dtype))
    ]


#####################
####    TESTS    ####
#####################


@pytest.mark.parametrize(
    "target_dtype, test_input, expected_result",
    list(itertools.chain(*[
        make_parameters("bool", valid_input(bool, pd.BooleanDtype())),
        make_parameters("nullable[bool]", valid_input(pd.BooleanDtype(), pd.BooleanDtype())),
    ]))
)
def test_boolean_to_boolean_accepts_all_valid_inputs(
    target_dtype, test_input, expected_result
):
    result = BooleanSeries(test_input).to_boolean(target_dtype)
    assert result.equals(expected_result), (
        f"BooleanSeries.to_boolean(dtype={repr(target_dtype)}) failed with "
        f"input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{expected_result}\n"
        f"received:\n"
        f"{result}"
    )


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
