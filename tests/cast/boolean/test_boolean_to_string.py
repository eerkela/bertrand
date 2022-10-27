import numpy as np
import pandas as pd
import pytest

from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED

from pdtypes.cast.boolean import BooleanSeries


#######################################
####    DEFAULT STORAGE BACKEND    ####
#######################################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series("True", dtype=DEFAULT_STRING_DTYPE)),
    (False, pd.Series("False", dtype=DEFAULT_STRING_DTYPE)),
    ([True, False], pd.Series(["True", "False"], dtype=DEFAULT_STRING_DTYPE)),
    ((False, True), pd.Series(["False", "True"], dtype=DEFAULT_STRING_DTYPE)),
    ([True, False, None], pd.Series(["True", "False", pd.NA], dtype=DEFAULT_STRING_DTYPE)),
    ([True, False, pd.NA], pd.Series(["True", "False", pd.NA], dtype=DEFAULT_STRING_DTYPE)),
    ([True, False, np.nan], pd.Series(["True", "False", pd.NA], dtype=DEFAULT_STRING_DTYPE)),

    # array
    (np.array(True), pd.Series("True", dtype=DEFAULT_STRING_DTYPE)),
    (np.array([True, False]), pd.Series(["True", "False"], dtype=DEFAULT_STRING_DTYPE)),
    (np.array([True, False], dtype="O"), pd.Series(["True", "False"], dtype=DEFAULT_STRING_DTYPE)),
    (np.array([True, False, None], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=DEFAULT_STRING_DTYPE)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=DEFAULT_STRING_DTYPE)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=DEFAULT_STRING_DTYPE)),

    # series
    (pd.Series(True), pd.Series("True", dtype=DEFAULT_STRING_DTYPE)),
    (pd.Series([True, False]), pd.Series(["True", "False"], dtype=DEFAULT_STRING_DTYPE)),
    (pd.Series([True, False], dtype="O"), pd.Series(["True", "False"], dtype=DEFAULT_STRING_DTYPE)),
    (pd.Series([True, False, None], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=DEFAULT_STRING_DTYPE)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=DEFAULT_STRING_DTYPE)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=DEFAULT_STRING_DTYPE)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series(["True", "False", pd.NA], dtype=DEFAULT_STRING_DTYPE))
])
def test_boolean_to_default_string_dtype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_string()

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_string() failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


######################################
####    PYTHON STORAGE BACKEND    ####
######################################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series("True", dtype=pd.StringDtype("python"))),
    (False, pd.Series("False", dtype=pd.StringDtype("python"))),
    ([True, False], pd.Series(["True", "False"], dtype=pd.StringDtype("python"))),
    ((False, True), pd.Series(["False", "True"], dtype=pd.StringDtype("python"))),
    ([True, False, None], pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("python"))),
    ([True, False, pd.NA], pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("python"))),
    ([True, False, np.nan], pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("python"))),

    # array
    (np.array(True), pd.Series("True", dtype=pd.StringDtype("python"))),
    (np.array([True, False]), pd.Series(["True", "False"], dtype=pd.StringDtype("python"))),
    (np.array([True, False], dtype="O"), pd.Series(["True", "False"], dtype=pd.StringDtype("python"))),
    (np.array([True, False, None], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("python"))),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("python"))),
    (np.array([True, False, np.nan], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("python"))),

    # series
    (pd.Series(True), pd.Series("True", dtype=pd.StringDtype("python"))),
    (pd.Series([True, False]), pd.Series(["True", "False"], dtype=pd.StringDtype("python"))),
    (pd.Series([True, False], dtype="O"), pd.Series(["True", "False"], dtype=pd.StringDtype("python"))),
    (pd.Series([True, False, None], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("python"))),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("python"))),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("python"))),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("python")))
])
def test_boolean_to_python_string_dtype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_string("str[python]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_string(dtype='str[python]') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )



#######################################
####    PYARROW STORAGE BACKEND    ####
#######################################


if PYARROW_INSTALLED:

    @pytest.mark.parametrize("given", [
        # scalar
        (True, pd.Series("True", dtype=pd.StringDtype("pyarrow"))),
        (False, pd.Series("False", dtype=pd.StringDtype("pyarrow"))),
        ([True, False], pd.Series(["True", "False"], dtype=pd.StringDtype("pyarrow"))),
        ((False, True), pd.Series(["False", "True"], dtype=pd.StringDtype("pyarrow"))),
        ([True, False, None], pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("pyarrow"))),
        ([True, False, pd.NA], pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("pyarrow"))),
        ([True, False, np.nan], pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("pyarrow"))),

        # array
        (np.array(True), pd.Series("True", dtype=pd.StringDtype("pyarrow"))),
        (np.array([True, False]), pd.Series(["True", "False"], dtype=pd.StringDtype("pyarrow"))),
        (np.array([True, False], dtype="O"), pd.Series(["True", "False"], dtype=pd.StringDtype("pyarrow"))),
        (np.array([True, False, None], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("pyarrow"))),
        (np.array([True, False, pd.NA], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("pyarrow"))),
        (np.array([True, False, np.nan], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("pyarrow"))),

        # series
        (pd.Series(True), pd.Series("True", dtype=pd.StringDtype("pyarrow"))),
        (pd.Series([True, False]), pd.Series(["True", "False"], dtype=pd.StringDtype("pyarrow"))),
        (pd.Series([True, False], dtype="O"), pd.Series(["True", "False"], dtype=pd.StringDtype("pyarrow"))),
        (pd.Series([True, False, None], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("pyarrow"))),
        (pd.Series([True, False, pd.NA], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("pyarrow"))),
        (pd.Series([True, False, np.nan], dtype="O"), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("pyarrow"))),
        (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series(["True", "False", pd.NA], dtype=pd.StringDtype("pyarrow")))
    ])
    def test_boolean_to_pyarrow_string_dtype(given):
        # arrange
        val, expected = given

        # act
        result = BooleanSeries(val).to_string("str[pyarrow]")

        # assert
        assert result.equals(expected), (
            f"BooleanSeries.to_string(dtype='str[pyarrow]') failed with input:\n"
            f"{val}\n"
            f"expected:\n"
            f"{expected}\n"
            f"received:\n"
            f"{result}"
        )
