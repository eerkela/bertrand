from __future__ import annotations
from contextlib import nullcontext
from typing import Any, Iterator
from uuid import uuid4

import pandas as pd
import pytest


def all_subclasses(cls):
    """Recursively find every subclass of a given type."""
    return set(cls.__subclasses__()).union(
        s for c in cls.__subclasses__() for s in all_subclasses(c)
    )


def parametrize(
    *test_cases: _TestCase | Parameters,
    indirect: list | bool = False,
    ids: list[str] | callable = None,
    scope: str = None
):
    """A simplified interface for `pytest.mark.parametrize()` calls that
    forces the use of `Parameters` containers.

    When used to decorate a test function, that function must accept only one
    argument: an individual _TestCase object with the required information, or
    an instance of one of its subtypes, as defined in individual test modules.
    """
    return pytest.mark.parametrize(
        "case",
        Parameters(*test_cases).test_cases,
        indirect=indirect,
        ids=ids,
        scope=scope
    )


class Raises:
    """Base class for exception handler objects.
    
    This is a static wrapper for `pytest.raises()` objects, with some slight
    modifications to make test code more compact and readable.
    """

    def __init__(
        self,
        error_type: type,
        msg: str = None,
        nomatch: str = None
    ):
        self.ctx = pytest.raises(Exception)
        self.error_type = error_type
        self.msg = msg
        self.nomatch = nomatch

    def __enter__(self) -> pytest.ExceptionInfo[Exception]:
        return self.ctx.__enter__()

    def __exit__(self, *tp):
        # preempt nomatch failure and use corresponding message instead
        # NOTE: this behavior has been deprecated in official pytest since 4.1
        if tp[0] is None:
            pytest.fail(self.nomatch)

        # invoke normal pytest.RaisesContext.__exit__() method
        result = self.ctx.__exit__(*tp)

        # cleanup assertions
        assert self.ctx.excinfo.type is self.error_type
        assert self.ctx.excinfo.match(self.msg)

        # return result of pytest.RaisesContext.__exit__()
        return result

    def __repr__(self) -> str:
        return f"{self.error_type.__name__}: ... {self.msg} ..."

    def __str__(self) -> str:
        return repr(self)


class _TestCase:
    """Base class for test case objects.

    This is essentially a static wrapper for `pytest.param()` objects, as used
    in fully-configurable pytest parametrization.  Subclasses should define the
    structure of an acceptable test case in their __init__() method, and may
    or may not add additional convenience methods as needed.
    """

    def __init__(
        self,
        kwargs: dict,
        test_input: Any,
        test_output: Raises | Any,
        input_type: type | tuple[type, ...],
        output_type: type | tuple[type, ...],
        name: str = None,
        id: str = None,
        marks: tuple = tuple()
    ):
        # assert `kwargs` is a dict with only strings as keys
        if (
            not isinstance(kwargs, dict) or
            not all(isinstance(k, str) for k in kwargs)
        ):
            raise SyntaxError(
                f"`kwargs` must be a dictionary holding keyword arguments to "
                f"supply to the function under test, not {type(kwargs)}"
            )

        # assert `test_input` is an instance of `input_type` if it is defined
        if input_type is not None and not isinstance(test_input, input_type):
            raise SyntaxError(
                f"`test_input` must be an instance of {input_type} specifying "
                f"data to supply to the function under test, not "
                f"{type(test_input)}"
            )

        # assert `test_output` is a pytest.raises() context manager or an
        # instance of `output_type`, if it is defined
        if (
            output_type is not None and
            not isinstance(test_output, (Raises, output_type))):
            raise SyntaxError(
                f"`test_output` must be either a pytest.raises() context "
                f"manager or an instance of {output_type} specifying the "
                f"expected output for the function under test, not "
                f"{type(test_output)}"
            )

        # generate unique case name (as seen by `pytest.mark.depends` calls)
        self._name = name or str(uuid4())

        # define pytest.param() object
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
    def error_context(self) -> Iterator[None]:
        """If this test case describes an expected error state (i.e. its output
        field is an instance of `tests.scheme.Raises`), then return a context
        manager that enforces the expected error within its context block.
        Otherwise, proceed as normal.
        """
        if isinstance(self.output, Raises):
            return self.output
        return nullcontext()

    @property
    def id(self) -> str:
        """Get the pytest id of this test case."""
        return self.parameter_set.id

    @property
    def input(self) -> Any:
        """Get the `test_input` portion of this test case."""
        return self.values[1]

    @property
    def kwargs(self) -> dict[str, Any]:
        """Get the `kwargs` portion of this test case."""
        return self.values[0]

    @property
    def marks(self) -> tuple:
        """Get the pytest marks that are currently associated with this test
        case.  The first element is always a `pytest.mark.depends` object
        defining the case name.
        """
        return self.parameter_set.marks

    @property
    def name(self) -> str:
        """Get the case name (as seen by `pytest.mark.depends`) of this test
        case.
        """
        return self._name

    @property
    def output(self) -> Raises | Any:
        """Get the `test_output` portion of this test case."""
        return self.values[2]

    @property
    def values(self) -> tuple:
        """Get a 3-tuple (`test_input`, `kwargs`, `test_output`) representing
        the values of the underlying `pytest.param()` object.
        """
        return self.parameter_set.values

    #########################
    ####    UTILITIES    ####
    #########################

    def signature(self, *exclude) -> str:
        """Return a comma-separated string representing this case's kwargs as
        the signature of a hypothetical function call.

        i.e. if the function under test is `f(x, y)` and `kwargs` is
        `{'x': 1, 'y': 2}`, then the equivalent signature would be
        `f(x=1, y=2)`.
        """
        return ", ".join(
            f"{k}={repr(v)}" for k, v in self.kwargs.items()
            if k not in exclude
        )

    def skip(
        self,
        reason: str = None
    ) -> _TestCase:
        """A hook to easily apply `pytest.mark.skip` marks to an individual
        test case.
        """
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
    ) -> _TestCase:
        """A hook to easily apply `pytest.mark.skipif` marks to an individual
        test case.
        """
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
    ) -> _TestCase:
        """A hook to easily apply `pytest.mark.xfail` marks to an individual
        test case.
        """
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
        on: str | _TestCase | Parameters
    ) -> _TestCase:
        """A hook to easily apply `pytest.mark.depends` marks to an individual
        test case.
        """
        # attach mark, referencing the name of another _TestCase/Parameters obj
        if not isinstance(on, (str, _TestCase, Parameters)):
            raise SyntaxError(
                f"_TestCase.depends(on={repr(on)}) is invalid: `on` must be a "
                f"string, `_TestCase`, or `Parameters` object, not {type(on)}"
            )

        if isinstance(on, _TestCase):
            on = on.name
        elif isinstance(on, Parameters):
            on = [test_case.name for test_case in on.test_cases]

        self.parameter_set = pytest.param(
            *self.parameter_set.values,
            id=self.parameter_set.id,
            marks=self.parameter_set.marks + (pytest.mark.depends(on=on),)
        )

        return self

    ###############################
    ####    DYNAMIC WRAPPER    ####
    ###############################

    def __str__(self):
        fmt_str = ",\n".join(str(y) for y in self.values)
        return f"({fmt_str})"

    def __repr__(self):
        fmt_repr = ",\n".join(repr(y) for y in self.values)
        return f"({fmt_repr})"


class Parameters:
    """Base class for `_TestCase` containers, for use in parametrized tests."""

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def __init__(self, *cases):
        self.test_cases = []
        for test_case in cases:
            if isinstance(test_case, _TestCase):
                self.test_cases.append(test_case)

            elif isinstance(test_case, Parameters):
                for c in test_case.test_cases:
                    self.test_cases.append(c)

            else:
                raise TypeError(
                    f"`Parameters` objects can only contain explicit "
                    f"`_TestCase` definitions or other `Parameters` objects, "
                    f"not {type(test_case)}"
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
        on: str | _TestCase | Parameters
    ) -> Parameters:
        """A hook to easily apply `pytest.mark.depends` marks to a test case.
        """
        if not isinstance(on, (str, _TestCase, Parameters)):
            raise SyntaxError(
                f"Parameters.depends(on={repr(on)}) is invalid: `on` must be "
                f"a string, `_TestCase`, or `Parameters` object, not "
                f"{type(on)}"
            )

        self.test_cases = [case.depends(on=on) for case in self.test_cases]
        return self

    def stop_first(self) -> Parameters:
        """Halt parametrized test execution at the first failure."""
        for i in range(1, len(self.test_cases)):
            self.test_cases[i].depends(self.test_cases[i - 1])

        return self

    ###############################
    ####    DYNAMIC WRAPPER    ####
    ###############################

    def __iter__(self):
        return self.test_cases.__iter__()

    def __next__(self):
        return self.test_cases.__next__()

    def __len__(self):
        return len(self.test_cases)

    def __contains__(self, value):
        if isinstance(value, str):  # interpret as _TestCase name
            return value in [case.name for case in self.test_cases]
        return value in self.test_cases

    def __getitem__(self, key):
        result = self.test_cases.__getitem__(key)

        # slice
        if isinstance(result, list):
            return type(self)(*result)

        # index
        return result

    def __setitem__(self, key, value: _TestCase | Parameters):
        if not isinstance(value, (_TestCase, Parameters)):
            raise TypeError(
                f"`Parameters` objects can only contain explicit `_TestCase` "
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

        return type(self)(*(self.test_cases + other.test_cases))

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
