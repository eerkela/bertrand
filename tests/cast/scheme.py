"""Enforces a strict testing scheme for pdtypes.cast()-related unit tests.

Using these prevents spaghettification of test code.
"""
from __future__ import annotations
import datetime

import dateutil
import numpy as np
import pandas as pd
import pytest
import pytz
import zoneinfo

from tests.cast import EXTENSION_TYPES
from tests.scheme import Case, Parameters


# TODO: reject_nonseries_input -> input_typecheck


def interpret_iso_8601_string(datetime_string, category, tz=None):
    """Use this function to convert an ISO 8601 datetime string into an
    equivalent datetime object from the given category, for testing purposes.
    """
    # pd.Timestamp
    if category in ("datetime", "datetime[pandas]"):
        return pd.Timestamp(datetime_string, tz=tz)

    # datetime.datetime
    if category == "datetime[python]":
        result = datetime.datetime.fromisoformat(datetime_string[:26])
        if tz is None:
            return result
        if isinstance(tz, pytz.BaseTzInfo):
            return tz.localize(result)
        if isinstance(tz, (zoneinfo.ZoneInfo, dateutil.tz.tzfile)):
            return result.replace(tzinfo=tz)
        return pytz.timezone(tz).localize(result)

    # np.datetime64
    if category == "datetime[numpy]":
        result = np.datetime64(datetime_string)
        if tz is None:
            return result
        raise RuntimeError(
            f"numpy.datetime64 objects do not carry timezone information"
        )

    raise RuntimeError(
        f"could not interpret datetime category: {repr(category)}"
    )


def parametrize(
    *test_cases: CastCase | CastParameters,
    indirect: list | bool = False,
    ids: list[str] | callable = None,
    scope: str = None
):
    """A simplified interface for `pytest.mark.parametrize()` calls that
    forces the use of `CastParameters` containers.

    When used to decorate a test function, that function must accept exactly
    3 arguments with the following structure:
        #. kwargs: dict - keyword arguments to supply to the method under test.
        #. test_input: pd.Series - input data to SeriesWrapper constructor.
        #. test_output: pd.Series - expected output for the given conversion.
    """
    return pytest.mark.parametrize(
        "kwargs, test_input, test_output",
        [case.parameter_set for case in CastParameters(*test_cases)],
        indirect=indirect,
        ids=ids,
        scope=scope
    )


class CastCase(Case):
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
        test_input: pd.Series,
        test_output: pd.Series,
        *,
        name: str = None,
        id: str = None,
        marks: tuple = tuple(),
        reject_nonseries_input: bool = True
    ):
        if not isinstance(kwargs, dict):
            raise ValueError(
                f"`kwargs` must be a dictionary holding keyword arguments to "
                f"supply to the method under test, not {type(kwargs)}"
            )

        if reject_nonseries_input and not isinstance(test_input, pd.Series):
            raise ValueError(
                f"`test_input` must be a pd.Series object containing input "
                f"data supplied to the SeriesWrapper constructor under test"
            )

        if not isinstance(test_output, pd.Series):
            raise ValueError(
                f"`test_output` must be a pd.Series object containing the "
                f"expected output data for the given test case"
            )

        self._input_check = reject_nonseries_input
        super().__init__(
            kwargs,
            test_input,
            test_output,
            name=name,
            id=id,
            marks=marks
        )

    @property
    def kwargs(self) -> dict:
        return self.values[0]

    @property
    def test_input(self) -> pd.Series:
        return self.values[1]

    @property
    def test_output(self) -> pd.Series:
        return self.values[2]

    def with_na(self, input_val, output_val) -> CastCase:
        """Return a copy of this test case with missing values added to both
        the input and output data.  Only works for test cases that are created
        with `reject_nonseries_input=True`.
        """
        if not self._input_check:
            raise SyntaxError(
                "`with_na()` is only specified for test cases created with "
                "`reject_nonseries_input=True`"
            )

        # copy current values
        values = list(self.values)

        # check if input/output series is nullable
        not_nullable = lambda series: (
            pd.api.types.is_bool_dtype(series) or
            pd.api.types.is_integer_dtype(series)
        ) and not pd.api.types.is_extension_array_dtype(series)

        # ensure test_input is nullable
        if not_nullable(values[1]):
            values[1] = values[1].astype(EXTENSION_TYPES[values[1].dtype])

        # ensure test_output is nullable
        if not_nullable(self.test_output):
            values[2] = values[2].astype(EXTENSION_TYPES[values[2].dtype])

        # add missing values
        values[1] = pd.concat(
            [values[1], pd.Series([input_val], dtype=values[1].dtype)],
            ignore_index=True
        )
        values[2] = pd.concat(
            [values[2], pd.Series([output_val], dtype=values[2].dtype)],
            ignore_index=True
        )

        # return as a new case object
        return CastCase(*values, marks=self.marks[1:])  # omit uuid mark


class CastParameters(Parameters):
    """A container for `CastCase` objects, for use in parametrized tests.

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
            if isinstance(test_case, CastCase):
                old_kwargs = test_case.kwargs.copy()
                test_case.kwargs.clear()
                test_case.kwargs.update({**overrides, **old_kwargs})
                self.test_cases.append(test_case)

            elif isinstance(test_case, CastParameters):
                for c in test_case.test_cases:
                    old_kwargs = c.kwargs.copy()
                    c.kwargs.clear()
                    c.kwargs.update({**overrides, **old_kwargs})
                    self.test_cases.append(c)

            else:
                raise TypeError(
                    f"`CastParameters` objects can only contain explicit "
                    f"`CastCase` definitions or other `CastParameters` "
                    f"objects, not {type(test_case)}"
                )

    def with_na(self, input_val, output_val) -> CastParameters:
        """Repeat each test case with missing values added to both the input
        and output series.
        """
        self.test_cases.extend([
            case.with_na(input_val, output_val) for case in self.test_cases
        ])
        return self
