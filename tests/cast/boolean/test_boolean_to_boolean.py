import numpy as np
import pandas as pd
import pytest

from pdtypes.cast.boolean import BooleanSeries


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(True)),
    (False, pd.Series(False)),
    ([True, False], pd.Series([True, False])),
    ((False, True), pd.Series([False, True])),
    ([True, False, None], pd.Series([True, False, None], dtype=pd.BooleanDtype())),
    ([True, False, pd.NA], pd.Series([True, False, None], dtype=pd.BooleanDtype())),
    ([True, False, np.nan], pd.Series([True, False, None], dtype=pd.BooleanDtype())),

    # array
    (np.array(True), pd.Series(True)),
    (np.array([True, False]), pd.Series([True, False])),
    (np.array([True, False], dtype="O"), pd.Series([True, False])),
    (np.array([True, False, None], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),

    # series
    (pd.Series(True), pd.Series(True)),
    (pd.Series([True, False]), pd.Series([True, False])),
    (pd.Series([True, False], dtype="O"), pd.Series([True, False])),
    (pd.Series([True, False, None], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype()))
])
def test_boolean_to_boolean_with_boolean_supertype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_boolean()

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_boolean() failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(True, dtype=pd.BooleanDtype())),
    (False, pd.Series(False, dtype=pd.BooleanDtype())),
    ([True, False], pd.Series([True, False], dtype=pd.BooleanDtype())),
    ([False, True], pd.Series([False, True], dtype=pd.BooleanDtype())),
    ([True, False, None], pd.Series([True, False, None], dtype=pd.BooleanDtype())),
    ([True, False, pd.NA], pd.Series([True, False, None], dtype=pd.BooleanDtype())),
    ([True, False, np.nan], pd.Series([True, False, None], dtype=pd.BooleanDtype())),

    # array
    (np.array(True), pd.Series(True, dtype=pd.BooleanDtype())),
    (np.array([True, False]), pd.Series([True, False], dtype=pd.BooleanDtype())),
    (np.array([True, False], dtype="O"), pd.Series([True, False], dtype=pd.BooleanDtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),

    # series
    (pd.Series(True), pd.Series(True, dtype=pd.BooleanDtype())),
    (pd.Series([True, False]), pd.Series([True, False], dtype=pd.BooleanDtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([True, False], dtype=pd.BooleanDtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([True, False, pd.NA], dtype=pd.BooleanDtype()))
])
def test_boolean_to_boolean_with_nullable_boolean_supertype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_boolean("nullable[bool]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_boolean(dtype='nullable[bool]') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
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
