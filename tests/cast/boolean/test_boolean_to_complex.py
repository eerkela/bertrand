import numpy as np
import pandas as pd
import pytest

from pdtypes.cast.boolean import BooleanSeries


#################################
####    COMPLEX SUPERTYPE    ####
#################################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1+0j)),
    (False, pd.Series(0+0j)),
    ([True, False], pd.Series([1+0j, 0+0j])),
    ((False, True), pd.Series([0+0j, 1+0j])),
    ([True, False, None], pd.Series([1+0j, 0+0j, complex("nan+nanj")])),
    ([True, False, pd.NA], pd.Series([1+0j, 0+0j, complex("nan+nanj")])),
    ([True, False, np.nan], pd.Series([1+0j, 0+0j, complex("nan+nanj")])),

    # array
    (np.array(True), pd.Series(1+0j)),
    (np.array([True, False]), pd.Series([1+0j, 0+0j])),
    (np.array([True, False], dtype="O"), pd.Series([1+0j, 0+0j])),
    (np.array([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")])),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")])),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")])),

    # series
    (pd.Series(True), pd.Series(1+0j)),
    (pd.Series([True, False]), pd.Series([1+0j, 0+0j])),
    (pd.Series([True, False], dtype="O"), pd.Series([1+0j, 0+0j])),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")])),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")])),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")])),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1+0j, 0+0j, complex("nan+nanj")]))
])
def test_boolean_to_float_with_complex_supertype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_complex("complex")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_complex(dtype='complex') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1+0j, dtype=np.complex64)),
    (False, pd.Series(0+0j, dtype=np.complex64)),
    ([True, False], pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    ((False, True), pd.Series([0+0j, 1+0j], dtype=np.complex64)),
    ([True, False, None], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    ([True, False, pd.NA], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    ([True, False, np.nan], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),

    # array
    (np.array(True), pd.Series(1+0j, dtype=np.complex64)),
    (np.array([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (np.array([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (np.array([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),

    # series
    (pd.Series(True), pd.Series(1+0j, dtype=np.complex64)),
    (pd.Series([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (pd.Series([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64))
])
def test_boolean_to_float_with_complex_supertype_downcasted(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_complex("complex", downcast=True)

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_complex(dtype='complex', downcast=True) failed "
        f"with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


#################################
####    COMPLEX64 SUBTYPE    ####
#################################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1+0j, dtype=np.complex64)),
    (False, pd.Series(0+0j, dtype=np.complex64)),
    ([True, False], pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    ((False, True), pd.Series([0+0j, 1+0j], dtype=np.complex64)),
    ([True, False, None], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    ([True, False, pd.NA], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    ([True, False, np.nan], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),

    # array
    (np.array(True), pd.Series(1+0j, dtype=np.complex64)),
    (np.array([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (np.array([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (np.array([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),

    # series
    (pd.Series(True), pd.Series(1+0j, dtype=np.complex64)),
    (pd.Series([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (pd.Series([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64))
])
def test_boolean_to_float_with_complex64_dtype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_complex("complex64")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_complex(dtype='complex64') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1+0j, dtype=np.complex64)),
    (False, pd.Series(0+0j, dtype=np.complex64)),
    ([True, False], pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    ((False, True), pd.Series([0+0j, 1+0j], dtype=np.complex64)),
    ([True, False, None], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    ([True, False, pd.NA], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    ([True, False, np.nan], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),

    # array
    (np.array(True), pd.Series(1+0j, dtype=np.complex64)),
    (np.array([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (np.array([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (np.array([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),

    # series
    (pd.Series(True), pd.Series(1+0j, dtype=np.complex64)),
    (pd.Series([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (pd.Series([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64))
])
def test_boolean_to_float_with_complex64_dtype_downcasted(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_complex("complex64", downcast=True)

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_complex(dtype='complex64', downcast=True) failed "
        f"with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


##################################
####    COMPLEX128 SUBTYPE    ####
##################################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1+0j, dtype=np.complex128)),
    (False, pd.Series(0+0j, dtype=np.complex128)),
    ([True, False], pd.Series([1+0j, 0+0j], dtype=np.complex128)),
    ((False, True), pd.Series([0+0j, 1+0j], dtype=np.complex128)),
    ([True, False, None], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex128)),
    ([True, False, pd.NA], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex128)),
    ([True, False, np.nan], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex128)),

    # array
    (np.array(True), pd.Series(1+0j, dtype=np.complex128)),
    (np.array([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex128)),
    (np.array([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex128)),
    (np.array([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex128)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex128)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex128)),

    # series
    (pd.Series(True), pd.Series(1+0j, dtype=np.complex128)),
    (pd.Series([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex128)),
    (pd.Series([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex128)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex128)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex128)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex128)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex128))
])
def test_boolean_to_float_with_complex128_dtype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_complex("complex128")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_complex(dtype='complex128') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1+0j, dtype=np.complex64)),
    (False, pd.Series(0+0j, dtype=np.complex64)),
    ([True, False], pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    ((False, True), pd.Series([0+0j, 1+0j], dtype=np.complex64)),
    ([True, False, None], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    ([True, False, pd.NA], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    ([True, False, np.nan], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),

    # array
    (np.array(True), pd.Series(1+0j, dtype=np.complex64)),
    (np.array([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (np.array([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (np.array([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),

    # series
    (pd.Series(True), pd.Series(1+0j, dtype=np.complex64)),
    (pd.Series([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (pd.Series([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64))
])
def test_boolean_to_float_with_complex128_dtype_downcasted(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_complex("complex128", downcast=True)

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_complex(dtype='complex128', downcast=True) failed "
        f"with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


###################################
####    CLONGDOUBLE SUBTYPE    ####
###################################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1+0j, dtype=np.clongdouble)),
    (False, pd.Series(0+0j, dtype=np.clongdouble)),
    ([True, False], pd.Series([1+0j, 0+0j], dtype=np.clongdouble)),
    ((False, True), pd.Series([0+0j, 1+0j], dtype=np.clongdouble)),
    ([True, False, None], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.clongdouble)),
    ([True, False, pd.NA], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.clongdouble)),
    ([True, False, np.nan], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.clongdouble)),

    # array
    (np.array(True), pd.Series(1+0j, dtype=np.clongdouble)),
    (np.array([True, False]), pd.Series([1+0j, 0+0j], dtype=np.clongdouble)),
    (np.array([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.clongdouble)),
    (np.array([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.clongdouble)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.clongdouble)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.clongdouble)),

    # series
    (pd.Series(True), pd.Series(1+0j, dtype=np.clongdouble)),
    (pd.Series([True, False]), pd.Series([1+0j, 0+0j], dtype=np.clongdouble)),
    (pd.Series([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.clongdouble)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.clongdouble)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.clongdouble)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.clongdouble)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.clongdouble))
])
def test_boolean_to_float_with_clongdouble_dtype(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_complex("clongdouble")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_complex(dtype='clongdouble') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1+0j, dtype=np.complex64)),
    (False, pd.Series(0+0j, dtype=np.complex64)),
    ([True, False], pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    ((False, True), pd.Series([0+0j, 1+0j], dtype=np.complex64)),
    ([True, False, None], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    ([True, False, pd.NA], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    ([True, False, np.nan], pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),

    # array
    (np.array(True), pd.Series(1+0j, dtype=np.complex64)),
    (np.array([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (np.array([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (np.array([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),

    # series
    (pd.Series(True), pd.Series(1+0j, dtype=np.complex64)),
    (pd.Series([True, False]), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (pd.Series([True, False], dtype="O"), pd.Series([1+0j, 0+0j], dtype=np.complex64)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64)),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1+0j, 0+0j, complex("nan+nanj")], dtype=np.complex64))
])
def test_boolean_to_float_with_clongdouble_dtype_downcasted(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_complex("clongdouble", downcast=True)

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_complex(dtype='clongdouble', downcast=True) failed "
        f"with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )
