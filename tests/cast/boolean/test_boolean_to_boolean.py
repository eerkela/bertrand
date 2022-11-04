import itertools

import numpy as np
import pandas as pd
import pytest

from tests.cast import Parameters

from pdtypes.cast.boolean import BooleanSeries


# TODO: test_boolean_to_boolean_handles_all_type_specifiers
# -> some xfail params in there as well


####################
####    DATA    ####
####################


def valid_input(non_nullable_dtype, nullable_dtype):
    create_record = lambda test_input, test_output: (
        {},
        test_input,
        test_output
    )

    return [
        # scalar
        create_record(
            True,
            pd.Series(True, dtype=non_nullable_dtype)
        ),
        create_record(
            False,
            pd.Series(False, dtype=non_nullable_dtype)
        ),

        # iterable
        create_record(
            [True, False],
            pd.Series([True, False], dtype=non_nullable_dtype)
        ),
        create_record(
            (False, True),
            pd.Series([False, True], dtype=non_nullable_dtype)
        ),
        create_record(
            (x for x in [True, False, None]),
            pd.Series([True, False, pd.NA], dtype=nullable_dtype)
        ),
        create_record(
            [True, False, pd.NA],
            pd.Series([True, False, pd.NA], dtype=nullable_dtype)
        ),
        create_record(
            [True, False, np.nan],
            pd.Series([True, False, pd.NA], dtype=nullable_dtype)
        ),

        # numpy array
        create_record(
            np.array(True),
            pd.Series(True, dtype=non_nullable_dtype)
        ),
        create_record(
            np.array(True, dtype="O"),
            pd.Series(True, dtype=non_nullable_dtype)
        ),
        create_record(
            np.array([True, False]),
            pd.Series([True, False], dtype=non_nullable_dtype)
        ),
        create_record(
            np.array([True, False], dtype="O"),
            pd.Series([True, False], dtype=non_nullable_dtype)
        ),
        create_record(
            np.array([True, False, None], dtype="O"),
            pd.Series([True, False, pd.NA], dtype=nullable_dtype)
        ),
        create_record(
            np.array([True, False, pd.NA], dtype="O"),
            pd.Series([True, False, pd.NA], dtype=nullable_dtype)
        ),
        create_record(
            np.array([True, False, np.nan], dtype="O"),
            pd.Series([True, False, pd.NA], dtype=nullable_dtype)
        ),

        # pandas Series
        create_record(
            pd.Series([True, False]),
            pd.Series([True, False], dtype=non_nullable_dtype)
        ),
        create_record(
            pd.Series([True, False], dtype="O"),
            pd.Series([True, False], dtype=non_nullable_dtype)
        ),
        create_record(
            pd.Series([True, False, None], dtype="O"),
            pd.Series([True, False, pd.NA], dtype=nullable_dtype)
        ),
        create_record(
            pd.Series([True, False, pd.NA], dtype="O"),
            pd.Series([True, False, pd.NA], dtype=nullable_dtype)
        ),
        create_record(
            pd.Series([True, False, np.nan], dtype="O"),
            pd.Series([True, False, pd.NA], dtype=nullable_dtype)
        ),
        create_record(
            pd.Series([True, False, None], dtype=pd.BooleanDtype()),
            pd.Series([True, False, pd.NA], dtype=nullable_dtype)
        )
    ]


#####################
####    TESTS    ####
#####################


@pytest.mark.parametrize(
    "kwargs, test_input, test_output",
    list(itertools.chain(*[
        Parameters.nocheck(
            valid_input(np.dtype(bool), pd.BooleanDtype()),
            dtype="bool"
        ),
        Parameters.nocheck(
            valid_input(pd.BooleanDtype(), pd.BooleanDtype()),
            dtype="nullable[bool]"
        )
    ]))
)
def test_boolean_to_boolean_accepts_all_valid_inputs(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_boolean(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_boolean({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
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
