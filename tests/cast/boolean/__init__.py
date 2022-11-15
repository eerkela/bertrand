import datetime
import decimal

import numpy as np
import pandas as pd

from tests.cast import Parameters, Case
from tests.cast.tables import ELEMENT_TYPES, EXTENSION_TYPES


# TODO: this should test against the final to_x conversion functions, rather
# than BooleanSeries directly.  The latter count as private attributes,
# the former are public.  Don't test against .cast() directly, since all it
# is is an interface to the more explicit to_x functions.


category_vals = {
    # boolean
    "bool": (True, False, pd.NA),
    "nullable[bool]": (True, False, pd.NA),

    # integer
    "int": (1, 0, pd.NA),
    "signed": (1, 0, pd.NA),
    "unsigned": (1, 0, pd.NA),
    "int8": (1, 0, pd.NA),
    "int16": (1, 0, pd.NA),
    "int32": (1, 0, pd.NA),
    "int64": (1, 0, pd.NA),
    "uint8": (1, 0, pd.NA),
    "uint16": (1, 0, pd.NA),
    "uint32": (1, 0, pd.NA),
    "uint64": (1, 0, pd.NA),
    "char": (1, 0, pd.NA),
    "short": (1, 0, pd.NA),
    "intc": (1, 0, pd.NA),
    "long": (1, 0, pd.NA),
    "long long": (1, 0, pd.NA),
    "ssize_t": (1, 0, pd.NA),
    "unsigned char": (1, 0, pd.NA),
    "unsigned short": (1, 0, pd.NA),
    "unsigned intc": (1, 0, pd.NA),
    "unsigned long": (1, 0, pd.NA),
    "unsigned long long": (1, 0, pd.NA),
    "size_t": (1, 0, pd.NA),
    "nullable[int]": (1, 0, pd.NA),
    "nullable[signed]": (1, 0, pd.NA),
    "nullable[unsigned]": (1, 0, pd.NA),
    "nullable[int8]": (1, 0, pd.NA),
    "nullable[int16]": (1, 0, pd.NA),
    "nullable[int32]": (1, 0, pd.NA),
    "nullable[int64]": (1, 0, pd.NA),
    "nullable[uint8]": (1, 0, pd.NA),
    "nullable[uint16]": (1, 0, pd.NA),
    "nullable[uint32]": (1, 0, pd.NA),
    "nullable[uint64]": (1, 0, pd.NA),
    "nullable[char]": (1, 0, pd.NA),
    "nullable[short]": (1, 0, pd.NA),
    "nullable[intc]": (1, 0, pd.NA),
    "nullable[long]": (1, 0, pd.NA),
    "nullable[long long]": (1, 0, pd.NA),
    "nullable[ssize_t]": (1, 0, pd.NA),
    "nullable[unsigned char]": (1, 0, pd.NA),
    "nullable[unsigned short]": (1, 0, pd.NA),
    "nullable[unsigned intc]": (1, 0, pd.NA),
    "nullable[unsigned long]": (1, 0, pd.NA),
    "nullable[unsigned long long]": (1, 0, pd.NA),
    "nullable[size_t]": (1, 0, pd.NA),

    # float
    "float": (1.0, 0.0, np.nan),
    "float16": (1.0, 0.0, np.nan),
    "float32": (.0, 0.0, np.nan),
    "float64": (1.0, 0.0, np.nan),
    "longdouble": (1.0, 0.0, np.nan),

    # complex
    "complex": (1+0j, 0+0j, complex("nan+nanj")),
    "complex64": (1+0j, 0+0j, complex("nan+nanj")),
    "complex128": (1+0j, 0+0j, complex("nan+nanj")),
    "clongdouble": (1+0j, 0+0j, complex("nan+nanj")),

    # decimal
    "decimal": (decimal.Decimal(1), decimal.Decimal(0), pd.NA),

    # datetime
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

    # timedelta
    "timedelta": (pd.Timedelta(1), pd.Timedelta(0), pd.NaT),
    "timedelta[pandas]": (pd.Timedelta(1), pd.Timedelta(0), pd.NaT),
    "timedelta[python]": (
        datetime.timedelta(0), datetime.timedelta(0), pd.NaT
    ),
    "timedelta[numpy]": (
        np.timedelta64(1, "ns"), np.timedelta64(0, "ns"), pd.NaT
    ),

    # string
    "str": ("True", "False", pd.NA),
    "str[python]": ("True", "False", pd.NA),
    "str[pyarrow]": ("True", "False", pd.NA),

    # object
    "object": (True, False, pd.NA),
}


##########################
####    VALID DATA    ####
##########################


def valid_input_data(category):
    case = lambda test_input, test_output: Case(
        {"dtype": category},
        test_input,
        test_output,
        reject_nonseries_input=False
    )

    # gather appropriate True/False/NA values from `category_vals`
    true, false, na = category_vals[category]

    # gather output series dtype from `ELEMENT_TYPES` + `EXTENSION_TYPES`
    dtype_without_na = ELEMENT_TYPES[category]["output_type"]
    if category in (
        "bool", "int", "signed", "unsigned", "int8", "int16", "int32", "int64",
        "uint8", "uint16", "uint32", "uint64", "char", "short", "intc", "long",
        "long long", "unsigned char", "unsigned short", "unsigned intc",
        "unsigned long", "unsigned long long",
    ):
        dtype_with_na = EXTENSION_TYPES[np.dtype(dtype_without_na)]
    else:
        dtype_with_na = dtype_without_na

    return Parameters(
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
    )


def valid_dtype_data(category):
    true, false, na = category_vals[category]

    case = lambda dtype, output_dtype: Case(
        {"dtype": dtype},
        pd.Series([True, False]),
        pd.Series([true, false], dtype=output_dtype)
    )

    type_info = ELEMENT_TYPES[category]
    output_type = type_info["output_type"]
    aliases = type_info["aliases"]

    return Parameters(
        Parameters(*[
            case(typespec, output_type) for typespec in aliases["atomic"]
        ]),
        Parameters(*[
            case(typespec, output_type) for typespec in aliases["dtype"]
        ]),
        Parameters(*[
            case(typespec, output_type) for typespec in aliases["string"]
        ]),
    ).with_na(pd.NA, na)


############################
####    INVALID DATA    ####
############################


def invalid_input_data():
    case = lambda test_input: Case(
        {},
        test_input,
        pd.Series([True, False]),  # not used
        reject_nonseries_input=False
    )

    return Parameters(
        # sets
        case({True, False}),
    )



# TODO: accept *excluding?
# invalid_dtype_data(
#     "bool",
#     "nullable[bool]"
# )

# element type lookup tables must implement a specific category type, rather
# than grouping them together.  Then you specify the types to exclude as
# positional arguments.


def invalid_dtype_data(category):
    case = lambda dtype: Case(
        {"dtype": dtype},
        pd.Series([True, False]),
        pd.Series([True, False])  # NOTE: not used
    )

    return Parameters(
        Parameters(*[
            Parameters(*[case(k) for k in specs])
            for cat, specs in ATOMIC_ELEMENT_TYPES.items() if cat != category
            # if cat not in excluding
        ]),
        Parameters(*[
            Parameters(*[case(k) for k in specs])
            for cat, specs in DTYPE_ELEMENT_TYPES.items() if cat != category
        ]),
        Parameters(*[
            Parameters(*[case(k) for k in specs])
            for cat, specs in STRING_ELEMENT_TYPES.items() if cat != category
        ]),
    )

