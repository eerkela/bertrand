from decimal import Decimal

import numpy as np
import pandas as pd
import pytest

from pdtypes.cast.boolean import BooleanSeries


def valid_input():
    return np.array([
        # scalar
        (True, pd.Series(Decimal(1), dtype="O")),
        (False, pd.Series(Decimal(0), dtype="O")),

        # iterable
        ([True, False], pd.Series([Decimal(1), Decimal(0)], dtype="O")),
        ((False, True), pd.Series([Decimal(0), Decimal(1)], dtype="O")),
        ((x for x in [True, False, None]), pd.Series([Decimal(1), Decimal(0), pd.NA], dtype="O")),
        ([True, False, pd.NA], pd.Series([Decimal(1), Decimal(0), pd.NA], dtype="O")),
        ([True, False, np.nan], pd.Series([Decimal(1), Decimal(0), pd.NA], dtype="O")),

        # scalar array
        (np.array(True), pd.Series(Decimal(1), dtype="O")),
        (np.array(False), pd.Series(Decimal(0), dtype="O")),
    
        # 1D array (numpy dtype)
        (np.array([True, False]), pd.Series([Decimal(1), Decimal(0)], dtype="O")),

        # 1D array (object dtype)
        (np.array([True, False], dtype="O"), pd.Series([Decimal(1), Decimal(0)], dtype="O")),
        (np.array([True, False, None], dtype="O"), pd.Series([Decimal(1), Decimal(0), pd.NA], dtype="O")),
        (np.array([True, False, pd.NA], dtype="O"), pd.Series([Decimal(1), Decimal(0), pd.NA], dtype="O")),
        (np.array([True, False, np.nan], dtype="O"), pd.Series([Decimal(1), Decimal(0), pd.NA], dtype="O")),

        # series (numpy dtype)
        (pd.Series([True, False]), pd.Series([Decimal(1), Decimal(0)], dtype="O")),

        # series (object dtype)
        (pd.Series([True, False], dtype="O"), pd.Series([Decimal(1), Decimal(0)], dtype="O")),
        (pd.Series([True, False, None], dtype="O"), pd.Series([Decimal(1), Decimal(0), pd.NA], dtype="O")),
        (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([Decimal(1), Decimal(0), pd.NA], dtype="O")),
        (pd.Series([True, False, np.nan], dtype="O"), pd.Series([Decimal(1), Decimal(0), pd.NA], dtype="O")),

        # series (pandas dtype)
        (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([Decimal(1), Decimal(0), pd.NA], dtype="O"))
    ],
    dtype=[("test_input", "O"), ("expected_result", "O")]
    )


#######################
####    GENERAL    ####
#######################


@pytest.mark.parametrize("test_input, expected_result", valid_input())
def test_boolean_to_decimal_accepts_valid_inputs(test_input, expected_result):
    result = BooleanSeries(test_input).to_decimal()
    assert result.equals(expected_result), (
        f"BooleanSeries.to_decimal() failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{expected_result}\n"
        f"received:\n"
        f"{result}"
    )


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
