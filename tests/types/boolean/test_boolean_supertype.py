from __future__ import annotations
import itertools

import numpy as np
import pandas as pd
import pytest

from pdtypes.types import BooleanType


####################
####    DATA    ####
####################


def input_permutations():
    return pytest.mark.parametrize(
        "sparse, categorical, nullable",
        itertools.product([True, False], repeat=3)  # cartesian product
    )


#####################
####    TESTS    ####
#####################


@input_permutations()
def test_boolean_supertype_instance_returns_flyweights(
    sparse, categorical, nullable
):
    result_1 = BooleanType.instance(
        sparse=sparse, categorical=categorical, nullable=nullable
    )
    result_2 = BooleanType.instance(
        sparse=sparse, categorical=categorical, nullable=nullable
    )

    assert id(result_1) == id(result_2)


@input_permutations()
def test_boolean_supertype_handles_sparse_categorical_nullable_flags(
    sparse, categorical, nullable
):
    result = BooleanType.instance(
        sparse=sparse, categorical=categorical, nullable=nullable
    )

    assert result.sparse == sparse
    assert result.categorical == categorical
    assert result.nullable == nullable


@input_permutations()
def test_boolean_supertype_has_no_supertype(
    sparse, categorical, nullable
):
    result = BooleanType.instance(
        sparse=sparse, categorical=categorical, nullable=nullable
    )

    assert result.supertype is None


# TODO: subtypes should be a CompositeType, and have different contents based
# on configuration flags
# @input_permutations()
# def test_boolean_supertype_has_no_subtypes(
#     sparse, categorical, nullable
# ):
#     result = BooleanType.instance(
#         sparse=sparse, categorical=categorical, nullable=nullable
#     )

#     assert result.subtypes == frozenset({result})


@input_permutations()
def test_boolean_supertype_has_correct_atomic_numpy_pandas_types(
    sparse, categorical, nullable
):
    result = BooleanType.instance(
        sparse=sparse, categorical=categorical, nullable=nullable
    )

    assert result.atomic_type is bool
    assert result.numpy_type == np.dtype(bool)
    assert result.pandas_type == pd.BooleanDtype()


@input_permutations()
def test_boolean_supertype_has_correct_slug(
    sparse, categorical, nullable
):
    result = BooleanType.instance(
        sparse=sparse, categorical=categorical, nullable=nullable
    )

    expected = "bool"
    if nullable:
        expected = f"nullable[{expected}]"
    if categorical:
        expected = f"categorical[{expected}]"
    if sparse:
        expected = f"sparse[{expected}]"

    assert result.slug == expected
