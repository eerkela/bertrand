import numpy as np
import pandas as pd
import pytest

from pdtypes.cast.boolean import BooleanSeries


###############################
####    FLOAT SUPERTYPE    ####
###############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1.0)),
    (False, pd.Series(0.0)),
    ([True, False], pd.Series([1.0, 0.0])),
    ((False, True), pd.Series([0.0, 1.0])),
    ([True, False, None], pd.Series([1.0, 0.0, np.nan])),
    ([True, False, pd.NA], pd.Series([1.0, 0.0, np.nan])),
    ([True, False, np.nan], pd.Series([1.0, 0.0, np.nan])),

    # array
    (np.array(True), pd.Series(1.0)),
    (np.array([True, False]), pd.Series([1.0, 0.0])),
    (np.array([True, False], dtype="O"), pd.Series([1.0, 0.0])),
    (np.array([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan])),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan])),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan])),

    # series
    (pd.Series(True), pd.Series(1.0)),
    (pd.Series([True, False]), pd.Series([1.0, 0.0])),
    (pd.Series([True, False], dtype="O"), pd.Series([1.0, 0.0])),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan])),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan])),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan])),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1.0, 0.0, np.nan]))
])
def test_boolean_to_float_with_float_supertype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_float("float")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_float(dtype='float') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1.0, dtype=np.float16)),
    (False, pd.Series(0.0, dtype=np.float16)),
    ([True, False], pd.Series([1.0, 0.0], dtype=np.float16)),
    ((False, True), pd.Series([0.0, 1.0], dtype=np.float16)),
    ([True, False, None], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, pd.NA], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, np.nan], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # array
    (np.array(True), pd.Series(1.0, dtype=np.float16)),
    (np.array([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # series
    (pd.Series(True), pd.Series(1.0, dtype=np.float16)),
    (pd.Series([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1.0, 0.0, np.nan], dtype=np.float16))
])
def test_boolean_to_float_with_float_supertype_downcasted(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_float("float", downcast=True)

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_float(dtype='float', downcast=True) failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


###############################
####    FLOAT16 SUBTYPE    ####
###############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1.0, dtype=np.float16)),
    (False, pd.Series(0.0, dtype=np.float16)),
    ([True, False], pd.Series([1.0, 0.0], dtype=np.float16)),
    ((False, True), pd.Series([0.0, 1.0], dtype=np.float16)),
    ([True, False, None], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, pd.NA], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, np.nan], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # array
    (np.array(True), pd.Series(1.0, dtype=np.float16)),
    (np.array([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # series
    (pd.Series(True), pd.Series(1.0, dtype=np.float16)),
    (pd.Series([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1.0, 0.0, np.nan], dtype=np.float16))
])
def test_boolean_to_float_with_float16_dtype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_float("float16")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_float(dtype='float16') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1.0, dtype=np.float16)),
    (False, pd.Series(0.0, dtype=np.float16)),
    ([True, False], pd.Series([1.0, 0.0], dtype=np.float16)),
    ((False, True), pd.Series([0.0, 1.0], dtype=np.float16)),
    ([True, False, None], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, pd.NA], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, np.nan], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # array
    (np.array(True), pd.Series(1.0, dtype=np.float16)),
    (np.array([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # series
    (pd.Series(True), pd.Series(1.0, dtype=np.float16)),
    (pd.Series([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1.0, 0.0, np.nan], dtype=np.float16))
])
def test_boolean_to_float_with_float16_dtype_downcasted(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_float("float16", downcast=True)

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_float(dtype='float16', downcast=True) failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


###############################
####    FLOAT32 SUBTYPE    ####
###############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1.0, dtype=np.float32)),
    (False, pd.Series(0.0, dtype=np.float32)),
    ([True, False], pd.Series([1.0, 0.0], dtype=np.float32)),
    ((False, True), pd.Series([0.0, 1.0], dtype=np.float32)),
    ([True, False, None], pd.Series([1.0, 0.0, np.nan], dtype=np.float32)),
    ([True, False, pd.NA], pd.Series([1.0, 0.0, np.nan], dtype=np.float32)),
    ([True, False, np.nan], pd.Series([1.0, 0.0, np.nan], dtype=np.float32)),

    # array
    (np.array(True), pd.Series(1.0, dtype=np.float32)),
    (np.array([True, False]), pd.Series([1.0, 0.0], dtype=np.float32)),
    (np.array([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float32)),
    (np.array([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float32)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float32)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float32)),

    # series
    (pd.Series(True), pd.Series(1.0, dtype=np.float32)),
    (pd.Series([True, False]), pd.Series([1.0, 0.0], dtype=np.float32)),
    (pd.Series([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float32)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float32)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float32)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float32)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1.0, 0.0, np.nan], dtype=np.float32))
])
def test_boolean_to_float_with_float32_dtype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_float("float32")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_float(dtype='float32') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1.0, dtype=np.float16)),
    (False, pd.Series(0.0, dtype=np.float16)),
    ([True, False], pd.Series([1.0, 0.0], dtype=np.float16)),
    ((False, True), pd.Series([0.0, 1.0], dtype=np.float16)),
    ([True, False, None], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, pd.NA], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, np.nan], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # array
    (np.array(True), pd.Series(1.0, dtype=np.float16)),
    (np.array([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # series
    (pd.Series(True), pd.Series(1.0, dtype=np.float16)),
    (pd.Series([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1.0, 0.0, np.nan], dtype=np.float16))
])
def test_boolean_to_float_with_float32_dtype_downcasted(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_float("float32", downcast=True)

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_float(dtype='float32', downcast=True) failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


###############################
####    FLOAT64 SUBTYPE    ####
###############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1.0, dtype=np.float64)),
    (False, pd.Series(0.0, dtype=np.float64)),
    ([True, False], pd.Series([1.0, 0.0], dtype=np.float64)),
    ((False, True), pd.Series([0.0, 1.0], dtype=np.float64)),
    ([True, False, None], pd.Series([1.0, 0.0, np.nan], dtype=np.float64)),
    ([True, False, pd.NA], pd.Series([1.0, 0.0, np.nan], dtype=np.float64)),
    ([True, False, np.nan], pd.Series([1.0, 0.0, np.nan], dtype=np.float64)),

    # array
    (np.array(True), pd.Series(1.0, dtype=np.float64)),
    (np.array([True, False]), pd.Series([1.0, 0.0], dtype=np.float64)),
    (np.array([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float64)),
    (np.array([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float64)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float64)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float64)),

    # series
    (pd.Series(True), pd.Series(1.0, dtype=np.float64)),
    (pd.Series([True, False]), pd.Series([1.0, 0.0], dtype=np.float64)),
    (pd.Series([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float64)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float64)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float64)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float64)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1.0, 0.0, np.nan], dtype=np.float64))
])
def test_boolean_to_float_with_float64_dtype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_float("float64")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_float(dtype='float64') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1.0, dtype=np.float16)),
    (False, pd.Series(0.0, dtype=np.float16)),
    ([True, False], pd.Series([1.0, 0.0], dtype=np.float16)),
    ((False, True), pd.Series([0.0, 1.0], dtype=np.float16)),
    ([True, False, None], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, pd.NA], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, np.nan], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # array
    (np.array(True), pd.Series(1.0, dtype=np.float16)),
    (np.array([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # series
    (pd.Series(True), pd.Series(1.0, dtype=np.float16)),
    (pd.Series([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1.0, 0.0, np.nan], dtype=np.float16))
])
def test_boolean_to_float_with_float64_dtype_downcasted(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_float("float64", downcast=True)

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_float(dtype='float64', downcast=True) failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


##################################
####    LONGDOUBLE SUBTYPE    ####
##################################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1.0, dtype=np.longdouble)),
    (False, pd.Series(0.0, dtype=np.longdouble)),
    ([True, False], pd.Series([1.0, 0.0], dtype=np.longdouble)),
    ((False, True), pd.Series([0.0, 1.0], dtype=np.longdouble)),
    ([True, False, None], pd.Series([1.0, 0.0, np.nan], dtype=np.longdouble)),
    ([True, False, pd.NA], pd.Series([1.0, 0.0, np.nan], dtype=np.longdouble)),
    ([True, False, np.nan], pd.Series([1.0, 0.0, np.nan], dtype=np.longdouble)),

    # array
    (np.array(True), pd.Series(1.0, dtype=np.longdouble)),
    (np.array([True, False]), pd.Series([1.0, 0.0], dtype=np.longdouble)),
    (np.array([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.longdouble)),
    (np.array([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.longdouble)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.longdouble)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.longdouble)),

    # series
    (pd.Series(True), pd.Series(1.0, dtype=np.longdouble)),
    (pd.Series([True, False]), pd.Series([1.0, 0.0], dtype=np.longdouble)),
    (pd.Series([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.longdouble)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.longdouble)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.longdouble)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.longdouble)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1.0, 0.0, np.nan], dtype=np.longdouble))
])
def test_boolean_to_float_with_longdouble_dtype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_float("longdouble")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_float(dtype='longdouble') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1.0, dtype=np.float16)),
    (False, pd.Series(0.0, dtype=np.float16)),
    ([True, False], pd.Series([1.0, 0.0], dtype=np.float16)),
    ((False, True), pd.Series([0.0, 1.0], dtype=np.float16)),
    ([True, False, None], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, pd.NA], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    ([True, False, np.nan], pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # array
    (np.array(True), pd.Series(1.0, dtype=np.float16)),
    (np.array([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (np.array([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),

    # series
    (pd.Series(True), pd.Series(1.0, dtype=np.float16)),
    (pd.Series([True, False]), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False], dtype="O"), pd.Series([1.0, 0.0], dtype=np.float16)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1.0, 0.0, np.nan], dtype=np.float16)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1.0, 0.0, np.nan], dtype=np.float16))
])
def test_boolean_to_float_with_longdouble_dtype_downcasted(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_float("longdouble", downcast=True)

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_float(dtype='longdouble', downcast=True) failed "
        f"with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )
