from __future__ import annotations
from uuid import uuid4

import numpy as np
import pandas as pd
import pytest

from .tables import EXTENSION_TYPES


# TODO: upstream the Case and Parameters classes, then subclass them here as
# CastCase and CastParameters.
# -> the base classes include mark utilities, wrapper interface.  Any
# subclasses define required structure + parameter accessors


MISSING_VALUES = {
    "boolean": pd.NA,
    "integer": pd.NA,
    "float": np.nan,
    "complex": complex("nan+nanj"),
}



def skip(
    arg: Case | Parameters,
    reason: str = None
) -> Case | Parameters:
    """Programmatically apply `pytest.mark.skip` marks to test case objects.

    Equivalent to calling `Case.skip()`/`Parameters.skip()`, but allows for
    functional syntax if that is preferred.
    """
    if not isinstance(arg, (Case, Parameters)):
        raise SyntaxError(
            f"`skip()` can only be used on `Case` or `Parameters` objects, "
            f"not {type(arg)}"
        )

    return arg.skip(reason=reason)


def skipif(
    condition: bool | str,
    arg: Case | Parameters,
    reason: str = None
) -> Case | Parameters:
    """Programmatically apply `pytest.mark.skipif` marks to test case objects.

    Equivalent to calling `Case.skipif()`/`Parameters.skipif()`, but allows for
    functional syntax if that is preferred.
    """
    if not isinstance(arg, (Case, Parameters)):
        raise SyntaxError(
            f"`skipif()` can only be used on `Case` or `Parameters` objects, "
            f"not {type(arg)}"
        )

    return arg.skipif(condition=condition, reason=reason)


def xfail(
    arg: Case | Parameters,
    reason: str,
    condition: bool | str = True,
    raises: Exception = None,
    run: bool = True,
    strict: bool = True
) -> Case | Parameters:
    """Programmatically apply `pytest.mark.xfail` marks to test case objects.

    Equivalent to calling `Case.xfail()`/`Parameters.xfail()`, but allows for
    functional syntax if that is preferred.
    """
    if not isinstance(arg, (Case, Parameters)):
        raise SyntaxError(
            f"`xfail()` can only be used on `Case` or `Parameters` objects, "
            f"not {type(arg)}"
        )

    return arg.xfail(
        reason=reason,
        condition=condition, 
        raises=raises,
        run=run,
        strict=strict
    )


def depends(
    arg: Case | Parameters,
    on: str | Case | Parameters
) -> Case | Parameters:
    """Programmatically apply `pytest.mark.depends` marks to test case objects.

    Equivalent to calling `Case.depends()`/`Parameters.depends()`, but allows
    for functional syntax if that is preferred.
    """
    if not isinstance(arg, (Case, Parameters)):
        raise SyntaxError(
            f"`depends()` can only be used on `Case` or `Parameters` objects, "
            f"not {type(arg)}"
        )

    return arg.depends(on=on)


def parametrize(
    *test_cases: Case | Parameters,
    indirect: list | bool = False,
    ids: list[str] | callable = None,
    scope: str = None
):
    """A simplified interface for `pytest.mark.parametrize()` calls that
    forces the use of `Parameters` containers.

    When used to decorate a test function, that function must accept exactly
    3 arguments with the following structure:
        #. kwargs: dict - keyword arguments to supply to the method under test.
        #. test_input: pd.Series - input data to SeriesWrapper constructor.
        #. test_output: pd.Series - expected output for the given conversion.
    """
    return pytest.mark.parametrize(
        "kwargs, test_input, test_output",
        [case.parameter_set for case in Parameters(*test_cases)],
        indirect=indirect,
        ids=ids,
        scope=scope
    )




# TODO: these should probably be static wrappers rather than dynamic


class Case:
    """A dynamic wrapper for `pytest.param()` objects used in test
    parametrization.

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
        self._name = str(uuid4()) if name is None else name
        self.parameter_set = pytest.param(
            kwargs,
            test_input,
            test_output,
            id=id,
            marks=(pytest.mark.depends(name=self._name),) + marks
        )

    ##############################
    ####   TEST COMPONENTS    ####
    ##############################

    @property
    def kwargs(self) -> dict:
        return self.values[0]

    @property
    def test_input(self) -> pd.Series:
        return self.values[1]

    @property
    def test_output(self) -> pd.Series:
        return self.values[2]

    @property
    def name(self) -> str:
        return self._name

    #########################
    ####    UTILITIES    ####
    #########################

    def skip(
        self,
        reason: str = None
    ) -> Case:
        """A hook to easily apply `pytest.mark.skip` marks to a test case."""
        self.parameter_set = pytest.param(
            *self.parameter_set.values,
            id=self.parameter_set.id,
            marks=self.parameter_set.marks + (pytest.mark.skip(
                reason=reason
            ),)
        )
        return self

    def skipif(
        self,
        condition: bool | str,
        reason: str = None
    ) -> Case:
        """A hook to easily apply `pytest.mark.skipif` marks to a test case."""
        self.parameter_set = pytest.param(
            *self.parameter_set.values,
            id=self.parameter_set.id,
            marks=self.parameter_set.marks + (pytest.mark.skipif(
                condition=condition,
                reason=reason
            ),)
        )
        return self

    def xfail(
        self,
        reason: str,
        condition: bool | str = True,
        raises: Exception = None,
        run: bool = True,
        strict: bool = True
    ) -> Case:
        """A hook to easily apply `pytest.mark.xfail` marks to a test case."""
        self.parameter_set = pytest.param(
            *self.parameter_set.values,
            id=self.parameter_set.id,
            marks=self.parameter_set.marks + (pytest.mark.xfail(
                condition=condition,
                reason=reason,
                raises=raises,
                run=run,
                strict=strict
            ),)
        )
        return self

    def depends(
        self,
        on: str | Case | Parameters
    ) -> Case:
        """A hook to easily apply `pytest.mark.depends` marks to a test case.
        """
        if not isinstance(on, (str, Case, Parameters)):
            raise SyntaxError(
                f"Case cannot depend on object of type {type(on)}, must be an "
                f"instance of `Case` or `Parameters`"
            )

        if isinstance(on, Case):
            on = on.name
        elif isinstance(on, Parameters):
            on = [test_case.name for test_case in on.test_cases]

        self.parameter_set = pytest.param(
            *self.parameter_set.values,
            id=self.parameter_set.id,
            marks = self.parameter_set.marks + (pytest.mark.depends(on=on),)
        )
        return self

    def with_na(self, input_val, output_val) -> Case:
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

        # make input/output series nullable if it is not already
        not_nullable = lambda series: (
            pd.api.types.is_bool_dtype(series) or
            pd.api.types.is_integer_dtype(series)
        ) and not pd.api.types.is_extension_array_dtype(series)

        if not_nullable(values[1]):
            values[1] = values[1].astype(EXTENSION_TYPES[values[1].dtype])
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

        # return a new test case
        return Case(*values, marks=self.marks[1:])  # omit uuid mark

    ###############################
    ####    DYNAMIC WRAPPER    ####
    ###############################

    def __getattr__(self, name):
        return getattr(self.parameter_set, name)

    def __setattr__(self, name, value):
        if name in ("_input_check", "_name", "parameter_set"):
            self.__dict__[name] = value
        else:
            setattr(self.parameter_set, name, value)

    def __delattr__(self, name):
        delattr(self.parameter_set, name)

    def __str__(self):
        fmt_str = ",\n".join(str(y) for y in self.values)
        return f"({fmt_str})"

    def __repr__(self):
        fmt_repr = ",\n".join(repr(y) for y in self.values)
        return f"({fmt_repr})"


class Parameters:
    """A container for `Case` objects, for use in parametrized tests.

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

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def __init__(self, *cases, **overrides):
        self.test_cases = []

        for test_case in cases:
            if isinstance(test_case, Case):
                old_kwargs = test_case.kwargs.copy()
                test_case.kwargs.clear()
                test_case.kwargs.update({**overrides, **old_kwargs})
                self.test_cases.append(test_case)

            elif isinstance(test_case, Parameters):
                for c in test_case.test_cases:
                    old_kwargs = c.kwargs.copy()
                    c.kwargs.clear()
                    c.kwargs.update({**overrides, **old_kwargs})
                    self.test_cases.append(c)

            else:
                raise TypeError(
                    f"`Parameters` objects can only contain explicit `Case` "
                    f"definitions or other Parameters objects, not "
                    f"{type(test_case)}"
                )

    ###############################
    ####    UTILITY METHODS    ####
    ###############################

    def skip(
        self,
        reason: str = None
    ) -> Parameters:
        """A hook to easily apply `pytest.mark.skip` marks to groups of test
        cases.
        """
        self.test_cases = [
            case.skip(reason=reason) for case in self.test_cases
        ]
        return self

    def skipif(
        self,
        condition: bool | str,
        reason: str = None
    ) -> Parameters:
        """A hook to easily apply `pytest.mark.skipif` marks to groups of test
        cases.
        """
        self.test_cases = [
            case.skipif(
                condition=condition,
                reason=reason
            ) for case in self.test_cases
        ]
        return self

    def xfail(
        self,
        reason: str,
        condition: bool | str = True,
        raises: Exception = None,
        run: bool = True,
        strict: bool = True
    ) -> Parameters:
        """A hook to easily apply `pytest.mark.xfail` marks to groups of test
        cases.
        """
        self.test_cases = [
            case.xfail(
                condition=condition,
                reason=reason,
                raises=raises,
                run=run,
                strict=strict
            ) for case in self.test_cases
        ]
        return self

    def depends(
        self,
        on: str | Case | Parameters
    ) -> Parameters:
        """A hook to easily apply `pytest.mark.depends` marks to a test case.
        """
        if not isinstance(on, (str, Case, Parameters)):
            raise SyntaxError(
                f"Case cannot depend on object of type {type(on)}, must be an "
                f"instance of `Case` or `Parameters`"
            )

        self.test_cases = [case.depends(on=on) for case in self.test_cases]
        return self

    def with_na(self, input_val, output_val) -> Parameters:
        """Repeat each test case with missing values added to both the input
        and output series.
        """
        self.test_cases.extend([
            case.with_na(input_val, output_val) for case in self.test_cases
        ])
        return self

    def stop_first(self) -> Parameters:
        """Halt parametrized test execution at the first failure."""
        for i in range(1, len(self.test_cases)):
            self.test_cases[i].depends(self.test_cases[i - 1])

        return self

    ###############################
    ####    DYNAMIC WRAPPER    ####
    ###############################

    def __getattr__(self, name):
        return getattr(self.test_cases, name)

    def __setattr__(self, name, value):
        if name == "test_cases":
            self.__dict__[name] = value
        else:
            setattr(self.test_cases, name, value)

    def __delattr__(self, name):
        delattr(self.test_cases, name)

    def __iter__(self):
        return self.test_cases.__iter__()

    def __next__(self):
        return self.test_cases.__next__()

    def __len__(self):
        return len(self.test_cases)

    def __contains__(self, value):
        return value in self.test_cases

    def __getitem__(self, key):
        result = self.test_cases.__getitem__(key)

        # slice
        if isinstance(result, list):
            return Parameters(*result)

        # index
        return result

    def __setitem__(self, key, value: Case | Parameters):
        if not isinstance(value, (Case, Parameters)):
            raise TypeError(
                f"`Parameters` objects can only contain explicit `Case` "
                f"definitions or other Parameters objects, not {type(value)}"
            )

        # slice
        if isinstance(key, slice):
            if isinstance(value, Parameters):
                value = value.test_cases
            else:
                value = [value]

        # index
        else:
            if isinstance(value, Parameters):
                raise SyntaxError(
                    f"`Parameters` objects must be flat: cannot assign "
                    f"sequence to integer index {key}"
                )

        self.test_cases.__setitem__(key, value)

    def __delitem__(self, key):
        self.test_cases.__delitem__(key)

    def __add__(self, other: Parameters):
        if not isinstance(other, Parameters):
            raise SyntaxError(
                f"`Parameters` can only be concatenated with other "
                f"`Parameters` objects, not {type(other)}"
            )

        return Parameters(*(self.test_cases + other.test_cases))

    def __str__(self) -> str:
        fmt_str = ",\n".join(
            "(" + ",\n".join(str(y) for y in x.values) + ")"
            for x in self.test_cases
        )
        return f"[{fmt_str}]"

    def __repr__(self) -> str:
        fmt_repr = ",\n".join(
            "(" + ",\n".join(repr(y) for y in x.values) + ")"
            for x in self.test_cases
        )
        return f"[{fmt_repr}]"
