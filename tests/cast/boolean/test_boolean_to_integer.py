import numpy as np
import pandas as pd
import pytest

from pdtypes.cast.boolean import BooleanSeries


# TODO: it should be possible to radically simplify this with parametrized
# fixtures and such.
# https://docs.pytest.org/en/6.2.x/fixture.html#fixture-parametrize


# TODO: test_valid_input depends on:
# - target dtype
# -


#######################
####    GENERAL    ####
#######################


@pytest.mark.parametrize("dtype, expected", [
    # non-nullable
    ("int", pd.Series([1, 0], dtype=np.int8)),
    ("signed", pd.Series([1, 0], dtype=np.int8)),
    ("unsigned", pd.Series([1, 0], dtype=np.uint8)),
    ("int8", pd.Series([1, 0], dtype=np.int8)),
    ("int16", pd.Series([1, 0], dtype=np.int8)),
    ("int32", pd.Series([1, 0], dtype=np.int8)),
    ("int64", pd.Series([1, 0], dtype=np.int8)),
    ("uint8", pd.Series([1, 0], dtype=np.uint8)),
    ("uint16", pd.Series([1, 0], dtype=np.uint8)),
    ("uint32", pd.Series([1, 0], dtype=np.uint8)),
    ("uint64", pd.Series([1, 0], dtype=np.uint8)),

    # nullable
    ("nullable[int]", pd.Series([1, 0], dtype=pd.Int8Dtype())),
    ("nullable[signed]", pd.Series([1, 0], dtype=pd.Int8Dtype())),
    ("nullable[unsigned]", pd.Series([1, 0], dtype=pd.UInt8Dtype())),
    ("nullable[int8]", pd.Series([1, 0], dtype=pd.Int8Dtype())),
    ("nullable[int16]", pd.Series([1, 0], dtype=pd.Int8Dtype())),
    ("nullable[int32]", pd.Series([1, 0], dtype=pd.Int8Dtype())),
    ("nullable[int64]", pd.Series([1, 0], dtype=pd.Int8Dtype())),
    ("nullable[uint8]", pd.Series([1, 0], dtype=pd.UInt8Dtype())),
    ("nullable[uint16]", pd.Series([1, 0], dtype=pd.UInt8Dtype())),
    ("nullable[uint32]", pd.Series([1, 0], dtype=pd.UInt8Dtype())),
    ("nullable[uint64]", pd.Series([1, 0], dtype=pd.UInt8Dtype())),
])
def test_boolean_to_integer_downcasting(dtype, expected):
    result = BooleanSeries([True, False]).to_integer(dtype=dtype, downcast=True)
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype={repr(dtype)}, downcast=True) "
        f"failed.  expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


def test_boolean_to_integer_preserves_index():
    # arrange
    val = pd.Series(
        [True, False, pd.NA],
        index=[4, 5, 6],
        dtype=pd.BooleanDtype()
    )
    expected = pd.Series(
        [1, 0, pd.NA],
        index=[4, 5, 6],
        dtype=pd.Int64Dtype()
    )

    # act
    result = BooleanSeries(val).to_integer()

    # assert
    assert result.equals(expected), (
        "BooleanSeries.to_integer() does not preserve index"
    )


#################################
####    INTEGER SUPERTYPE    ####
#################################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1)),
    (False, pd.Series(0)),
    ([True, False], pd.Series([1, 0])),
    ((False, True), pd.Series([0, 1])),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),

    # array
    (np.array(True), pd.Series(1)),
    (np.array([True, False]), pd.Series([1, 0])),
    (np.array([True, False], dtype="O"), pd.Series([1, 0])),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),

    # series
    (pd.Series(True), pd.Series(1)),
    (pd.Series([True, False]), pd.Series([1, 0])),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0])),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype()))
])
def test_boolean_to_integer_supertype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("int")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='int') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=pd.Int64Dtype())),
    (False, pd.Series(0, dtype=pd.Int64Dtype())),
    ([True, False], pd.Series([1, 0], dtype=pd.Int64Dtype())),
    ((False, True), pd.Series([0, 1], dtype=pd.Int64Dtype())),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=pd.Int64Dtype())),
    (np.array([True, False]), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=pd.Int64Dtype())),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype()))
])
def test_boolean_to_nullable_integer_supertype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("nullable[int]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='nullable[int]') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


########################################
####    SIGNED INTEGER SUPERTYPE    ####
########################################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1)),
    (False, pd.Series(0)),
    ([True, False], pd.Series([1, 0])),
    ((False, True), pd.Series([0, 1])),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),

    # array
    (np.array(True), pd.Series(1)),
    (np.array([True, False]), pd.Series([1, 0])),
    (np.array([True, False], dtype="O"), pd.Series([1, 0])),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),

    # series
    (pd.Series(True), pd.Series(1)),
    (pd.Series([True, False]), pd.Series([1, 0])),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0])),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype()))
])
def test_boolean_to_signed_integer_supertype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("signed")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='signed') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=pd.Int64Dtype())),
    (False, pd.Series(0, dtype=pd.Int64Dtype())),
    ([True, False], pd.Series([1, 0], dtype=pd.Int64Dtype())),
    ((False, True), pd.Series([0, 1], dtype=pd.Int64Dtype())),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=pd.Int64Dtype())),
    (np.array([True, False]), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=pd.Int64Dtype())),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype()))
])
def test_boolean_to_nullable_signed_integer_supertype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("nullable[signed]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='nullable[signed]') failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


##########################################
####    UNSIGNED INTEGER SUPERTYPE    ####
##########################################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=np.uint64)),
    (False, pd.Series(0, dtype=np.uint64)),
    ([True, False], pd.Series([1, 0], dtype=np.uint64)),
    ((False, True), pd.Series([0, 1], dtype=np.uint64)),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=np.uint64)),
    (np.array([True, False]), pd.Series([1, 0], dtype=np.uint64)),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=np.uint64)),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=np.uint64)),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=np.uint64)),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=np.uint64)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype()))
])
def test_boolean_to_unsigned_integer_supertype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("unsigned")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='unsigned') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=pd.UInt64Dtype())),
    (False, pd.Series(0, dtype=pd.UInt64Dtype())),
    ([True, False], pd.Series([1, 0], dtype=pd.UInt64Dtype())),
    ((False, True), pd.Series([0, 1], dtype=pd.UInt64Dtype())),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=pd.UInt64Dtype())),
    (np.array([True, False]), pd.Series([1, 0], dtype=pd.UInt64Dtype())),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.UInt64Dtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=pd.UInt64Dtype())),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype()))
])
def test_boolean_to_nullable_unsigned_integer_supertype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("nullable[unsigned]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='nullable[unsigned]') failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


############################
####    INT8 SUBTYPE    ####
############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=np.int8)),
    (False, pd.Series(0, dtype=np.int8)),
    ([True, False], pd.Series([1, 0], dtype=np.int8)),
    ([False, True], pd.Series([0, 1], dtype=np.int8)),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int8Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int8Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int8Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=np.int8)),
    (np.array([True, False]), pd.Series([1, 0], dtype=np.int8)),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=np.int8)),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=np.int8)),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=np.int8)),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=np.int8)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype()))
])
def test_boolean_to_int8_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("int8")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='int8') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=pd.Int8Dtype())),
    (False, pd.Series(0, dtype=pd.Int8Dtype())),
    ([True, False], pd.Series([1, 0], dtype=pd.Int8Dtype())),
    ([False, True], pd.Series([0, 1], dtype=pd.Int8Dtype())),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int8Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int8Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int8Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=pd.Int8Dtype())),
    (np.array([True, False]), pd.Series([1, 0], dtype=pd.Int8Dtype())),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int8Dtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=pd.Int8Dtype())),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=pd.Int8Dtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int8Dtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int8Dtype()))
])
def test_boolean_to_nullable_int8_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("nullable[int8]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='nullable[int8]') failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


#############################
####    INT16 SUBTYPE    ####
#############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=np.int16)),
    (False, pd.Series(0, dtype=np.int16)),
    ([True, False], pd.Series([1, 0], dtype=np.int16)),
    ([False, True], pd.Series([0, 1], dtype=np.int16)),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int16Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int16Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int16Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=np.int16)),
    (np.array([True, False]), pd.Series([1, 0], dtype=np.int16)),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=np.int16)),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=np.int16)),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=np.int16)),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=np.int16)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype()))
])
def test_boolean_to_int16_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("int16")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='int16') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=pd.Int16Dtype())),
    (False, pd.Series(0, dtype=pd.Int16Dtype())),
    ([True, False], pd.Series([1, 0], dtype=pd.Int16Dtype())),
    ([False, True], pd.Series([0, 1], dtype=pd.Int16Dtype())),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int16Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int16Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int16Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=pd.Int16Dtype())),
    (np.array([True, False]), pd.Series([1, 0], dtype=pd.Int16Dtype())),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int16Dtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=pd.Int16Dtype())),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=pd.Int16Dtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int16Dtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int16Dtype()))
])
def test_boolean_to_nullable_int16_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("nullable[int16]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='nullable[int16]') failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


#############################
####    INT32 SUBTYPE    ####
#############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=np.int32)),
    (False, pd.Series(0, dtype=np.int32)),
    ([True, False], pd.Series([1, 0], dtype=np.int32)),
    ([False, True], pd.Series([0, 1], dtype=np.int32)),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int32Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int32Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int32Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=np.int32)),
    (np.array([True, False]), pd.Series([1, 0], dtype=np.int32)),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=np.int32)),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=np.int32)),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=np.int32)),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=np.int32)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype()))
])
def test_boolean_to_int32_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("int32")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='int32') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=pd.Int32Dtype())),
    (False, pd.Series(0, dtype=pd.Int32Dtype())),
    ([True, False], pd.Series([1, 0], dtype=pd.Int32Dtype())),
    ([False, True], pd.Series([0, 1], dtype=pd.Int32Dtype())),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int32Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int32Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int32Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=pd.Int32Dtype())),
    (np.array([True, False]), pd.Series([1, 0], dtype=pd.Int32Dtype())),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int32Dtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=pd.Int32Dtype())),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=pd.Int32Dtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int32Dtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int32Dtype()))
])
def test_boolean_to_nullable_int32_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("nullable[int32]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='nullable[int32]') failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


#############################
####    INT64 SUBTYPE    ####
#############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=np.int64)),
    (False, pd.Series(0, dtype=np.int64)),
    ([True, False], pd.Series([1, 0], dtype=np.int64)),
    ([False, True], pd.Series([0, 1], dtype=np.int64)),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=np.int64)),
    (np.array([True, False]), pd.Series([1, 0], dtype=np.int64)),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=np.int64)),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=np.int64)),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=np.int64)),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=np.int64)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype()))
])
def test_boolean_to_int64_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("int64")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='int64') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=pd.Int64Dtype())),
    (False, pd.Series(0, dtype=pd.Int64Dtype())),
    ([True, False], pd.Series([1, 0], dtype=pd.Int64Dtype())),
    ([False, True], pd.Series([0, 1], dtype=pd.Int64Dtype())),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.Int64Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=pd.Int64Dtype())),
    (np.array([True, False]), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=pd.Int64Dtype())),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.Int64Dtype()))
])
def test_boolean_to_nullable_int64_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("nullable[int64]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='nullable[int64]') failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


#############################
####    UINT8 SUBTYPE    ####
#############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=np.uint8)),
    (False, pd.Series(0, dtype=np.uint8)),
    ([True, False], pd.Series([1, 0], dtype=np.uint8)),
    ([False, True], pd.Series([0, 1], dtype=np.uint8)),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.UInt8Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.UInt8Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.UInt8Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=np.uint8)),
    (np.array([True, False]), pd.Series([1, 0], dtype=np.uint8)),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=np.uint8)),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=np.uint8)),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=np.uint8)),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=np.uint8)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype()))
])
def test_boolean_to_uint8_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("uint8")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='uint8') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=pd.UInt8Dtype())),
    (False, pd.Series(0, dtype=pd.UInt8Dtype())),
    ([True, False], pd.Series([1, 0], dtype=pd.UInt8Dtype())),
    ([False, True], pd.Series([0, 1], dtype=pd.UInt8Dtype())),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.UInt8Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.UInt8Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.UInt8Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=pd.UInt8Dtype())),
    (np.array([True, False]), pd.Series([1, 0], dtype=pd.UInt8Dtype())),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.UInt8Dtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=pd.UInt8Dtype())),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=pd.UInt8Dtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.UInt8Dtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.UInt8Dtype()))
])
def test_boolean_to_nullable_uint8_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("nullable[uint8]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='nullable[uint8]') failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


##############################
####    UINT16 SUBTYPE    ####
##############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=np.uint16)),
    (False, pd.Series(0, dtype=np.uint16)),
    ([True, False], pd.Series([1, 0], dtype=np.uint16)),
    ([False, True], pd.Series([0, 1], dtype=np.uint16)),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.UInt16Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.UInt16Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.UInt16Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=np.uint16)),
    (np.array([True, False]), pd.Series([1, 0], dtype=np.uint16)),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=np.uint16)),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=np.uint16)),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=np.uint16)),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=np.uint16)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype()))
])
def test_boolean_to_uint16_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("uint16")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='uint16') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=pd.UInt16Dtype())),
    (False, pd.Series(0, dtype=pd.UInt16Dtype())),
    ([True, False], pd.Series([1, 0], dtype=pd.UInt16Dtype())),
    ([False, True], pd.Series([0, 1], dtype=pd.UInt16Dtype())),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.UInt16Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.UInt16Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.UInt16Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=pd.UInt16Dtype())),
    (np.array([True, False]), pd.Series([1, 0], dtype=pd.UInt16Dtype())),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.UInt16Dtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=pd.UInt16Dtype())),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=pd.UInt16Dtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.UInt16Dtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.UInt16Dtype()))
])
def test_boolean_to_nullable_uint16_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("nullable[uint16]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='nullable[uint16]') failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


##############################
####    UINT32 SUBTYPE    ####
##############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=np.uint32)),
    (False, pd.Series(0, dtype=np.uint32)),
    ([True, False], pd.Series([1, 0], dtype=np.uint32)),
    ([False, True], pd.Series([0, 1], dtype=np.uint32)),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.UInt32Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.UInt32Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.UInt32Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=np.uint32)),
    (np.array([True, False]), pd.Series([1, 0], dtype=np.uint32)),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=np.uint32)),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=np.uint32)),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=np.uint32)),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=np.uint32)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype()))
])
def test_boolean_to_uint32_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("uint32")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='uint32') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=pd.UInt32Dtype())),
    (False, pd.Series(0, dtype=pd.UInt32Dtype())),
    ([True, False], pd.Series([1, 0], dtype=pd.UInt32Dtype())),
    ([False, True], pd.Series([0, 1], dtype=pd.UInt32Dtype())),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.UInt32Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.UInt32Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.UInt32Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=pd.UInt32Dtype())),
    (np.array([True, False]), pd.Series([1, 0], dtype=pd.UInt32Dtype())),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.UInt32Dtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=pd.UInt32Dtype())),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=pd.UInt32Dtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.UInt32Dtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.UInt32Dtype()))
])
def test_boolean_to_nullable_uint32_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("nullable[uint32]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='nullable[uint32]') failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


##############################
####    UINT64 SUBTYPE    ####
##############################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=np.uint64)),
    (False, pd.Series(0, dtype=np.uint64)),
    ([True, False], pd.Series([1, 0], dtype=np.uint64)),
    ([False, True], pd.Series([0, 1], dtype=np.uint64)),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=np.uint64)),
    (np.array([True, False]), pd.Series([1, 0], dtype=np.uint64)),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=np.uint64)),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=np.uint64)),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=np.uint64)),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=np.uint64)),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype()))
])
def test_boolean_to_uint64_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("uint64")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='uint64') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.Series(1, dtype=pd.UInt64Dtype())),
    (False, pd.Series(0, dtype=pd.UInt64Dtype())),
    ([True, False], pd.Series([1, 0], dtype=pd.UInt64Dtype())),
    ([False, True], pd.Series([0, 1], dtype=pd.UInt64Dtype())),
    ([True, False, None], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),
    ([True, False, pd.NA], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),
    ([True, False, np.nan], pd.Series([1, 0, None], dtype=pd.UInt64Dtype())),

    # array
    (np.array(True), pd.Series(1, dtype=pd.UInt64Dtype())),
    (np.array([True, False]), pd.Series([1, 0], dtype=pd.UInt64Dtype())),
    (np.array([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.UInt64Dtype())),
    (np.array([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (np.array([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (np.array([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),

    # series
    (pd.Series(True), pd.Series(1, dtype=pd.UInt64Dtype())),
    (pd.Series([True, False]), pd.Series([1, 0], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False], dtype="O"), pd.Series([1, 0], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, None], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, np.nan], dtype="O"), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype())),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([1, 0, pd.NA], dtype=pd.UInt64Dtype()))
])
def test_boolean_to_nullable_uint64_dtype_accepts_valid_input(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_integer("nullable[uint64]")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_integer(dtype='nullable[uint64]') failed with "
        f"input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )
