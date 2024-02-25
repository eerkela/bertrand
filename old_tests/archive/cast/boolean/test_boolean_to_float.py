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
        pd.Series([1.0, 0.0], dtype=series_type)
    )

    return CastParameters(
        case("float", np.float16),
        case("float16", np.float16),
        case("float32", np.float16),
        case("float64", np.float16),
        case("longdouble", np.float16),
    ).with_na(pd.NA, np.nan)


#####################
####    TESTS    ####
#####################


@parametrize(input_format_data("float"))
def test_boolean_to_float_accepts_valid_input_data(case: CastCase):
    # valid case
    if case.is_valid:
        result = BooleanSeries(case.input).to_float(**case.kwargs)
        assert result.equals(case.output), (
            f"BooleanSeries.to_float({case.signature()}) failed with "
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
            BooleanSeries(case.input).to_float(**case.kwargs)
            pytest.fail(
                f"BooleanSeries.to_float({case.signature()}) did not reject "
                f"input data:\n"
                f"{case.input}"
            )

        assert exc_info.type is TypeError


@parametrize(target_dtype_data("float"))
def test_boolean_to_float_accepts_float_type_specifiers(case: CastCase):
    # valid
    if case.is_valid:
        result = BooleanSeries(case.input).to_float(**case.kwargs)
        assert result.equals(case.output), (
            f"BooleanSeries.to_float({case.signature()}) failed with "
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
            BooleanSeries(case.input).to_float(**case.kwargs)
            pytest.fail(  # called when no exception is encountered
                f"BooleanSeries.to_float({case.signature('dtype')}) did not "
                f"reject dtype={repr(case.kwargs['dtype'])}"
            )

        assert exc_info.type is TypeError
        assert exc_info.match("`dtype` must be float-like")


@parametrize(downcast_data())
def test_boolean_to_float_downcasting(case: CastCase):
    result = BooleanSeries(case.input).to_float(**case.kwargs)
    assert result.equals(case.output), (
        f"BooleanSeries.to_float({case.signature()}) failed with input:\n"
        f"{case.input}\n"
        f"expected:\n"
        f"{case.output}\n"
        f"received:\n"
        f"{result}"
    )


def test_boolean_to_float_preserves_index():
    # arrange
    case = CastCase(
        {},
        pd.Series(
            [True, False, pd.NA],
            index=[4, 5, 6],
            dtype=pd.BooleanDtype()
        ),
        pd.Series(
            [1.0, 0.0, np.nan],
            index=[4, 5, 6],
            dtype=np.float64
        )
    )

    # act
    result = BooleanSeries(case.input).to_float(**case.kwargs)

    # assert
    assert result.equals(case.output), (
        f"BooleanSeries.to_float({case.signature()}) does not preserve index"
    )
