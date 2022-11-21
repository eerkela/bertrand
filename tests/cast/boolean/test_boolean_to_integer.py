import numpy as np
import pandas as pd
import pytest

from tests.cast.scheme import CastCase, CastParameters, parametrize
from tests.cast.boolean import input_format_data, target_dtype_data

from pdtypes.cast.boolean import BooleanSeries


####################
####    DATA    ####
####################


def downcast_data():
    case = lambda target_dtype, series_type: CastCase(
        {"dtype": target_dtype, "downcast": True},
        pd.Series([True, False]),
        pd.Series([1, 0], dtype=series_type)
    )

    return CastParameters(
        # non-nullable
        case("int", np.int8),
        case("signed", np.int8),
        case("unsigned", np.uint8),
        case("int8", np.int8),
        case("int16", np.int8),
        case("int32", np.int8),
        case("int64", np.int8),
        case("uint8", np.uint8),
        case("uint16", np.uint8),
        case("uint32", np.uint8),
        case("uint64", np.uint8),

        # nullable
        case("nullable[int]", pd.Int8Dtype()),
        case("nullable[signed]", pd.Int8Dtype()),
        case("nullable[unsigned]", pd.UInt8Dtype()),
        case("nullable[int8]", pd.Int8Dtype()),
        case("nullable[int16]", pd.Int8Dtype()),
        case("nullable[int32]", pd.Int8Dtype()),
        case("nullable[int64]", pd.Int8Dtype()),
        case("nullable[uint8]", pd.UInt8Dtype()),
        case("nullable[uint16]", pd.UInt8Dtype()),
        case("nullable[uint32]", pd.UInt8Dtype()),
        case("nullable[uint64]", pd.UInt8Dtype()),
    ).with_na(pd.NA, pd.NA)


#####################
####    TESTS    ####
#####################


@parametrize(input_format_data("integer"))
def test_boolean_to_integer_accepts_valid_input_data(case: CastCase):
    # valid case
    if case.is_valid:
        result = BooleanSeries(case.input).to_integer(**case.kwargs)
        assert result.equals(case.output), (
            f"BooleanSeries.to_integer({case.signature()}) failed with "
            f"input:\n"
            f"{case.input}\n"
            f"expected:\n"
            f"{case.output}\n"
            f"received:\n"
            f"{result}"
        )

    # invalid case
    else:
        with case.output as exc_info:
            BooleanSeries(case.input).to_integer(**case.kwargs)
            pytest.fail(
                f"BooleanSeries.to_integer({case.signature()}) did not reject "
                f"input data:\n"
                f"{case.input}"
            )

        assert exc_info.type is TypeError


@parametrize(target_dtype_data("integer"))
def test_boolean_to_integer_accepts_integer_type_specifiers(case: CastCase):
    # valid
    if case.is_valid:
        result = BooleanSeries(case.input).to_integer(**case.kwargs)
        assert result.equals(case.output), (
            f"BooleanSeries.to_integer({case.signature()}) failed with "
            f"input:\n"
            f"{case.input}\n"
            f"expected:\n"
            f"{case.output}\n"
            f"received:\n"
            f"{result}"
        )

    # invalid
    else:
        with case.output as exc_info:
            BooleanSeries(case.input).to_integer(**case.kwargs)
            pytest.fail(  # called when no exception is encountered
                f"BooleanSeries.to_integer({case.signature('dtype')}) did not "
                f"reject dtype={repr(case.kwargs['dtype'])}"
            )

        assert exc_info.type is TypeError
        assert exc_info.match("`dtype` must be int-like")


@parametrize(downcast_data())
def test_boolean_to_integer_downcasting(case: CastCase):
    result = BooleanSeries(case.input).to_integer(**case.kwargs)
    assert result.equals(case.output), (
        f"BooleanSeries.to_integer({case.signature()}) failed with input:\n"
        f"{case.input}\n"
        f"expected:\n"
        f"{case.output}\n"
        f"received:\n"
        f"{result}"
    )


def test_boolean_to_integer_preserves_index():
    # arrange
    case = CastCase(
        {},
        pd.Series(
            [True, False, pd.NA],
            index=[4, 5, 6],
            dtype=pd.BooleanDtype()
        ),
        pd.Series(
            [1, 0, pd.NA],
            index=[4, 5, 6],
            dtype=pd.Int64Dtype()
        )
    )

    # act
    result = BooleanSeries(case.input).to_integer(**case.kwargs)

    # assert
    assert result.equals(case.output), (
        "BooleanSeries.to_integer() does not preserve index"
    )
