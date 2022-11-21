from __future__ import annotations
from uuid import uuid4

import pytest


# TODO: use 'parametrized conditional raising' to combine valid, invalid
# datasets:
# https://docs.pytest.org/en/7.1.x/example/parametrize.html#parametrizing-conditional-raising
# -> define a 'failure' keyword argument for Case objects.  If failure is not
# None, test_output must be None (default) and failure must contain a
# pytest.raises() context manager.  Otherwise, failure must be None and
# test_output must not be None.  In this case, it defaults to
# contextlib.nullcontext.
# -> all signatures become (kwargs, test_input, test_output, failure_ctx).
# Test functions check `if isinstance(failure_ctx, contextlib.nullcontext):`
# before specifying a custom error message.
# -> Case has is_failure + failure_ctx fields


class Case:
    """Base class for individual test case objects.

    This is a static wrapper for `pytest.param()` objects, as used in test
    parametrization.  Subclasses should define the structure of an acceptable
    test case in their __init__() method.
    """

    def __init__(
        self,
        *values,
        name: str = None,
        id: str = None,
        marks: tuple = tuple()
    ):
        # generate unique case ID
        self._name = name or str(uuid4())

        # define pytest.param() object
        self.parameter_set = pytest.param(
            *values,
            id=id,
            marks=(pytest.mark.depends(name=self._name),) + marks
        )

    ##############################
    ####   TEST COMPONENTS    ####
    ##############################

    @property
    def id(self) -> str:
        return self.parameter_set.id

    @property
    def marks(self) -> tuple:
        return self.parameter_set.marks

    @property
    def name(self) -> str:
        return self._name

    @property
    def values(self) -> tuple:
        return self.parameter_set.values

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
        # attach mark, referencing the name of another Case/Parameters
        if not isinstance(on, (str, Case, Parameters)):
            raise SyntaxError(
                f"Case.depends(on={repr(on)}) is invalid: `on` must be a "
                f"string, `Case`, or `Parameters` object, not {type(on)}"
            )

        if isinstance(on, Case):
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
    """Base class for `Case` containers, for use in parametrized tests."""

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def __init__(self, *cases):
        self.test_cases = []
        for test_case in cases:
            if isinstance(test_case, Case):
                self.test_cases.append(test_case)

            elif isinstance(test_case, Parameters):
                for c in test_case.test_cases:
                    self.test_cases.append(c)

            else:
                raise TypeError(
                    f"`Parameters` objects can only contain explicit "
                    f"`Case` definitions or other `Parameters` objects, "
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
        on: str | Case | Parameters
    ) -> Parameters:
        """A hook to easily apply `pytest.mark.depends` marks to a test case.
        """
        if not isinstance(on, (str, Case, Parameters)):
            raise SyntaxError(
                f"Parameters.depends(on={repr(on)}) is invalid: `on` must be "
                f"a string, `Case`, or `Parameters` object, not "
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
        if isinstance(value, str):  # interpret as Case name
            return value in [case.name for case in self.test_cases]
        return value in self.test_cases

    def __getitem__(self, key):
        result = self.test_cases.__getitem__(key)

        # slice
        if isinstance(result, list):
            return type(self)(*result)

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
