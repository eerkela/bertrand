import datetime
import decimal

import numpy as np
import pandas as pd
import pytest

from tests.cast import EXTENSION_TYPES, SERIES_TYPES
from tests.cast.scheme import CastCase, CastParameters


# TODO: this should test against the final to_x conversion functions, rather
# than BooleanSeries directly.  The latter count as private attributes,
# the former are public.  Don't test against .cast() directly, since all it
# is is an interface to the more explicit to_x functions.


# TODO: document these


# True/False/NA representations for each of the recognized type categories
category_vals = {
    "boolean": (True, False, pd.NA),
    "integer": (1, 0, pd.NA),
    "float": (1.0, 0.0, np.nan),
    "complex": (1+0j, 0+0j, complex("nan+nanj")),
    "decimal": (decimal.Decimal(1), decimal.Decimal(0), pd.NA),
    "datetime": {
        "datetime": (pd.Timestamp(1), pd.Timestamp(0), pd.NaT),
        "datetime[pandas]": (pd.Timestamp(1), pd.Timestamp(0), pd.NaT),
        "datetime[python]": (
            datetime.datetime.utcfromtimestamp(0),
            datetime.datetime.utcfromtimestamp(0),
            pd.NaT
        ),
        "datetime[numpy]": (
            np.datetime64(1, "ns"), np.datetime64(0, "ns"), pd.NaT
        ),
    },
    "timedelta": {
        "timedelta": (pd.Timedelta(1), pd.Timedelta(0), pd.NaT),
        "timedelta[pandas]": (pd.Timedelta(1), pd.Timedelta(0), pd.NaT),
        "timedelta[python]": (
            datetime.timedelta(0), datetime.timedelta(0), pd.NaT
        ),
        "timedelta[numpy]": (
            np.timedelta64(1, "ns"), np.timedelta64(0, "ns"), pd.NaT
        ),
    },
    "string": ("True", "False", pd.NA),
    "object": (True, False, pd.NA),
}


##############################
####    DATA FACTORIES    ####
##############################


def input_format_data(category):
    """Build a parametrized list of test cases describing the various input
    data formats that are accepted by BooleanSeries.to_{category}().

    Contains a unique copy of the specified parameters for each subtype within
    the given category.
    """
    case = lambda test_input, test_output: CastCase(
        {"dtype": target_dtype},
        test_input,
        test_output,
        input_typecheck=False
    )

    # NOTE: `pars` specifies test cases.  This pattern looks a little strange,
    # but is necessary to accomodate "datetime" and "timedelta" categories,
    # which have different True/False/NA representations depending on subtype.
    # To call pars(), these values must be specified in the input_data() scope,
    # as well as `target_dtype`, `dtype_without_na`, and `dtype_with_na`.
    # `target_dtype` and `dtype_without_na` can be found by consulting the
    # SERIES_TYPES lookup table.  `dtype_with_na` is usually equal to
    # `dtype_without_na`, except in the case of "boolean" or "integer"
    # categories, which are not nullable by default.
    pars = lambda: CastParameters(
        #####################
        ####    VALID    ####
        #####################

        # scalar
        case(
            True,
            pd.Series([true], dtype=dtype_without_na)
        ),
        case(
            False,
            pd.Series([false], dtype=dtype_without_na)
        ),

        # iterable
        case(
            [True, False],
            pd.Series([true, false], dtype=dtype_without_na)
        ),
        case(
            (False, True),
            pd.Series([false, true], dtype=dtype_without_na)
        ),
        case(
            (x for x in [True, False, None]),
            pd.Series([true, false, na], dtype=dtype_with_na)
        ),
        case(
            {"a": True, "b": False, "c": pd.NA},
            pd.Series([true, false, na], index=["a", "b", "c"], dtype=dtype_with_na)
        ),
        case(
            [True, False, np.nan],
            pd.Series([true, false, na], dtype=dtype_with_na)
        ),

        # numpy array
        case(
            np.array(True),
            pd.Series([true], dtype=dtype_without_na)
        ),
        case(
            np.array(True, dtype="O"),
            pd.Series([true], dtype=dtype_without_na)
        ),
        case(
            np.array([True, False]),
            pd.Series([true, false], dtype=dtype_without_na)
        ),
        case(
            np.array([True, False], dtype="O"),
            pd.Series([true, false], dtype=dtype_without_na)
        ),
        case(
            np.array([True, False, None], dtype="O"),
            pd.Series([true, false, na], dtype=dtype_with_na)
        ),
        case(
            np.array([True, False, pd.NA], dtype="O"),
            pd.Series([true, false, na], dtype=dtype_with_na)
        ),
        case(
            np.array([True, False, np.nan], dtype="O"),
            pd.Series([true, false, na], dtype=dtype_with_na)
        ),

        # pandas Series
        case(
            pd.Series([True, False]),
            pd.Series([true, false], dtype=dtype_without_na)
        ),
        case(
            pd.Series([True, False], dtype="O"),
            pd.Series([true, false], dtype=dtype_without_na)
        ),
        case(
            pd.Series([True, False, None], dtype="O"),
            pd.Series([true, false, na], dtype=dtype_with_na)
        ),
        case(
            pd.Series([True, False, pd.NA], dtype="O"),
            pd.Series([true, false, na], dtype=dtype_with_na)
        ),
        case(
            pd.Series([True, False, np.nan], dtype="O"),
            pd.Series([true, false, na], dtype=dtype_with_na)
        ),
        case(
            pd.Series([True, False, None], dtype=pd.BooleanDtype()),
            pd.Series([true, false, na], dtype=dtype_with_na)
        ),

        #######################
        ####    INVALID    ####
        #######################

        # unordered
        case(
            {True, False},
            pytest.raises(Exception)
        ),
        case(
            frozenset({True, False}),
            pytest.raises(Exception)
        ),
    )

    test_cases = []
    subtypes = SERIES_TYPES[category]
    vals = category_vals[category]
    for target_dtype, dtype_without_na in subtypes.items():
        # get True/False/NA values for the given target_dtype
        if isinstance(vals, dict):
            true, false, na = vals[target_dtype]
        else:
            true, false, na = vals

        # get dtype_with_na
        dtype_with_na = EXTENSION_TYPES.get(dtype_without_na, dtype_without_na)

        # append parameter list
        test_cases.append(pars())

    # concatenate test_cases
    return CastParameters(*test_cases)


def target_dtype_data(category):
    case = lambda target_dtype, output_dtype: CastCase(
        {"dtype": target_dtype},
        pd.Series([True, False]),
        pd.Series([true, false], dtype=output_dtype)
    )

    # valid cases
    test_cases = []
    subtypes = SERIES_TYPES[category]
    vals = category_vals[category]
    for target_dtype, output_dtype in subtypes.items():
        # get True/False/NA representations for the given target_dtype
        if isinstance(vals, dict):
            true, false, na = vals[target_dtype]
        else:
            true, false, na = vals

        # append CastCase
        obj = case(target_dtype, output_dtype)
        test_cases.append(obj)
        test_cases.append(obj.with_na(pd.NA, na))

    # invalid cases
    case = lambda target_dtype: CastCase(
        {"dtype": target_dtype},
        pd.Series([True, False]),
        pytest.raises(Exception)
    )

    for cat, subtypes in SERIES_TYPES.items():
        if cat != category:
            pars = CastParameters(*[
                case(target_dtype) for target_dtype in subtypes
            ])
            test_cases.append(pars)

    return CastParameters(*test_cases)
