from decimal import Decimal

import pandas as pd
import pytest

from tests.cast.scheme import CastCase, parametrize
from tests.cast.boolean import input_format_data, target_dtype_data

from pdtypes.cast.boolean import BooleanSeries


@parametrize(input_format_data("decimal"))
def test_boolean_to_decimal_accepts_valid_input_data(case: CastCase):
    # valid case
    if case.is_valid:
        result = BooleanSeries(case.input).to_decimal(**case.kwargs)
        assert result.equals(case.output), (
            f"BooleanSeries.to_decimal({case.signature()}) failed with "
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
            BooleanSeries(case.input).to_decimal(**case.kwargs)
            pytest.fail(
                f"BooleanSeries.to_decimal({case.signature()}) did not reject "
                f"input data:\n"
                f"{case.input}"
            )

        assert exc_info.type is TypeError


@parametrize(target_dtype_data("decimal"))
def test_boolean_to_decimal_accepts_decimal_type_specifiers(case: CastCase):
    # valid
    if case.is_valid:
        result = BooleanSeries(case.input).to_decimal(**case.kwargs)
        assert result.equals(case.output), (
            f"BooleanSeries.to_boolean({case.signature()}) failed with "
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
            BooleanSeries(case.input).to_decimal(**case.kwargs)
            pytest.fail(  # called when no exception is encountered
                f"BooleanSeries.to_decimal({case.signature('dtype')}) did not "
                f"reject dtype={repr(case.kwargs['dtype'])}"
            )

        assert exc_info.type is TypeError
        assert exc_info.match("`dtype` must be decimal-like")


def test_boolean_to_decimal_preserves_index():
    # arrange
    case = CastCase(
        {},
        pd.Series(
            [True, False, pd.NA],
            index=[4, 5, 6],
            dtype=pd.BooleanDtype()
        ),
        pd.Series(
            [Decimal(1), Decimal(0), pd.NA],
            index=[4, 5, 6],
            dtype="O"
        )
    )

    # act
    result = BooleanSeries(case.input).to_decimal(**case.kwargs)

    # assert
    assert result.equals(case.output), (
        f"BooleanSeries.to_decimal({case.signature()}) does not preserve "
        f"index"
    )
