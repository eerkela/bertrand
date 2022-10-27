import decimal

import numpy as np
import pandas as pd
import pytest

from pdtypes.cast.boolean import BooleanSeries


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(decimal.Decimal(1), dtype="O")),
    (False, pd.Series(decimal.Decimal(0), dtype="O")),
    ([True, False], pd.Series([decimal.Decimal(1), decimal.Decimal(0)], dtype="O")),
    ((False, True), pd.Series([decimal.Decimal(0), decimal.Decimal(1)], dtype="O")),
    ([True, False, None], pd.Series([decimal.Decimal(1), decimal.Decimal(0), pd.NA], dtype="O")),
    ([True, False, pd.NA], pd.Series([decimal.Decimal(1), decimal.Decimal(0), pd.NA], dtype="O")),
    ([True, False, np.nan], pd.Series([decimal.Decimal(1), decimal.Decimal(0), pd.NA], dtype="O")),

    # array
    (np.array(True), pd.Series(decimal.Decimal(1), dtype="O")),
    (np.array([True, False]), pd.Series([decimal.Decimal(1), decimal.Decimal(0)], dtype="O")),
    (np.array([True, False], dtype="O"), pd.Series([decimal.Decimal(1), decimal.Decimal(0)], dtype="O")),
    (np.array([True, False, None], dtype="O"), pd.Series([decimal.Decimal(1), decimal.Decimal(0), pd.NA], dtype="O")),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([decimal.Decimal(1), decimal.Decimal(0), pd.NA], dtype="O")),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([decimal.Decimal(1), decimal.Decimal(0), pd.NA], dtype="O")),

    # series
    (pd.Series(True), pd.Series(decimal.Decimal(1), dtype="O")),
    (pd.Series([True, False]), pd.Series([decimal.Decimal(1), decimal.Decimal(0)], dtype="O")),
    (pd.Series([True, False], dtype="O"), pd.Series([decimal.Decimal(1), decimal.Decimal(0)], dtype="O")),
    (pd.Series([True, False, None], dtype="O"), pd.Series([decimal.Decimal(1), decimal.Decimal(0), pd.NA], dtype="O")),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([decimal.Decimal(1), decimal.Decimal(0), pd.NA], dtype="O")),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([decimal.Decimal(1), decimal.Decimal(0), pd.NA], dtype="O")),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([decimal.Decimal(1), decimal.Decimal(0), pd.NA], dtype="O"))
])
def test_boolean_to_decimal(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_decimal()

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_decimal() failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )
