"""Enforces a strict testing scheme for pdtypes.cast()-related unit tests.

Using these prevents spaghettification of test code.
"""
from __future__ import annotations

import pandas as pd
import pytest

from tests.scheme import Case, Parameters

from pdtypes.types import ElementType, CompositeType


# pytest.raises() type.  This is usually hidden behind a private API
raises_context = type(pytest.raises(Exception))


def parametrize(
    *test_cases: TypeCase | TypeParameters,
    indirect: list | bool = False,
    ids: list[str] | callable = None,
    scope: str = None
):
    """A simplified interface for `pytest.mark.parametrize()` calls that
    forces the use of `TypeParameters` containers.

    When used to decorate a test function, that function must accept exactly
    3 arguments with the following structure:
        #. kwargs: dict - keyword arguments to supply to the method under test.
        #. test_input: pd.Series - input data to SeriesWrapper constructor.
        #. test_output: pd.Series - expected output for the given conversion.
    """
    return pytest.mark.parametrize(
        "case",
        TypeParameters(*test_cases).test_cases,
        indirect=indirect,
        ids=ids,
        scope=scope
    )


class TypeCase(Case):
    """A subclass of `Case` for use in pdtypes.cast() operations.

    Enforces minimal test correctness by requiring that each test case contain
    exactly 3 elements with the following structure:
        #. kwargs: dict - keyword arguments to supply to the method under test.
        #. test_input: pd.Series - input data to SeriesWrapper constructor.
        #. test_output: pd.Series - expected output for the given conversion.

    Note
    ----
    The primary benefit of the `Case` and `Parameters` objects is to enforce a
    strict schema for test construction, which minimizes the chance of
    introducing hard-to-detect bugs in test code.  Using these objects forces
    each test function to adopt a standardized call signature (`kwargs`,
    `test_input`, `test_output`).  The parameters that are supplied to the
    parametrized test are then guaranteed to contain valid input at each index.
    If a malformed test case is encountered, a SyntaxError will be thrown at
    pytest collection time rather than continuing with potentially faulty data.

    Additionally, both objects expose several utility methods that can be
    helpful when dealing with heavily parametrized unit tests.  Marks like
    `pytest.mark.skip()`/`pytest.mark.xfail()` etc. can be easily added to
    both individual cases and to entire containers as needed, and particularly
    pathological test patterns (like duplicating test cases to confirm support
    for missing values) can be automated away entirely.
    """

    def __init__(
        self,
        kwargs: dict,
        test_input,
        test_output: ElementType | CompositeType,
        *,
        name: str = None,
        id: str = None,
        marks: tuple = tuple(),
    ):
        # assert kwargs is a dict
        if not isinstance(kwargs, dict):
            raise SyntaxError(
                f"`kwargs` must be a dictionary holding keyword arguments to "
                f"supply to the method under test, not {type(kwargs)}"
            )

        # assert that test_output is either an ElementType/CompositeType object
        # or a pytest.raises() context manager
        if not isinstance(
            test_output,
            (ElementType, CompositeType, raises_context)
        ):
            raise SyntaxError(
                f"`test_output` must be EITHER an ElementType/CompositeType "
                f"object containing expected output for the given test case "
                f"OR a pytest.raises() context manager specifying an intended "
                f"error state, not {type(test_output)}"
            )

        super().__init__(
            kwargs,
            test_input,
            test_output,
            name=name,
            id=id,
            marks=marks
        )

    @property
    def is_valid(self) -> bool:
        return not isinstance(self.output, raises_context)

    @property
    def kwargs(self) -> dict:
        return self.values[0]

    @property
    def input(self) -> pd.Series:
        return self.values[1]

    @property
    def output(self) -> pd.Series:
        return self.values[2]

    def signature(self, *exclude) -> str:
        return ", ".join(
            f"{k}={repr(v)}" for k, v in self.kwargs.items()
            if k not in exclude
        )


class TypeParameters(Parameters):
    """A container for `TypeCase` objects, for use in parametrized tests.

    Enforces minimal test correctness by requiring that each parameter set
    contains exactly 3 elements with the following structure:
        #. kwargs: dict - keyword arguments to supply to the method under test.
        #. test_input: pd.Series - input data to SeriesWrapper constructor.
        #. test_output: pd.Series - expected output for the given conversion.

    Note
    ----
    The primary benefit of the `Case` and `Parameters` objects is to enforce a
    strict schema for test construction, which minimizes the chance of
    introducing hard-to-detect bugs in test code.  Using these objects forces
    each test function to adopt a standardized call signature (`kwargs`,
    `test_input`, `test_output`).  The parameters that are supplied to the
    parametrized test are then guaranteed to contain valid input at each index.
    If a malformed test case is encountered, a SyntaxError will be thrown at
    pytest collection time rather than continuing with potentially faulty data.

    Additionally, both objects expose several utility methods that can be
    helpful when dealing with heavily parametrized unit tests.  Marks like
    `pytest.mark.skip()`/`pytest.mark.xfail()` etc. can be easily added to
    both individual cases and to entire containers as needed, and particularly
    pathological test patterns (like duplicating test cases to confirm support
    for missing values) can be automated away entirely.
    """

    def __init__(self, *cases, **overrides):
        self.test_cases = []

        for test_case in cases:
            if isinstance(test_case, TypeCase):
                old_kwargs = test_case.kwargs.copy()
                test_case.kwargs.clear()
                test_case.kwargs.update({**overrides, **old_kwargs})
                self.test_cases.append(test_case)

            elif isinstance(test_case, TypeParameters):
                for c in test_case.test_cases:
                    old_kwargs = c.kwargs.copy()
                    c.kwargs.clear()
                    c.kwargs.update({**overrides, **old_kwargs})
                    self.test_cases.append(c)

            else:
                raise TypeError(
                    f"`TypeParameters` objects can only contain explicit "
                    f"`TypeCase` definitions or other `TypeParameters` "
                    f"objects, not {type(test_case)}"
                )
