"""This module describes a base class for decorators that allow cooperative
attribute access with nested objects.
"""
from __future__ import annotations
from functools import update_wrapper, WRAPPER_ASSIGNMENTS
import inspect
from types import MappingProxyType
from typing import Any, Callable, Mapping


EMPTY = inspect.Parameter.empty


class NoDefault:
    """Signals that an argument does not have an associated default value."""

    def __repr__(self) -> str:
        return "<no default>"


no_default = NoDefault()


class FunctionDecorator:
    """Base class for cooperative decorators.

    These decorators implement the Gang of Four's `Decorator pattern
    <https://en.wikipedia.org/wiki/Decorator_pattern>`_ to  cooperatively pass
    attribute access down a (possibly nested) decorator stack.

    Parameters
    ----------
    func : Callable
        The function (or other callable) to decorate.  Any attributes that are
        not found on this decorator will be dynamically passed down to this
        object.
    **kwargs : dict
        Keyword arguments to use for multiple inheritance.

    Notes
    -----
    This expects all subclasses to define the following attribute at the class
    level:

        *   ``_reserved``: a set of strings describing attribute names that are
            reserved for this decorator.

    The special string ``"__wrapped__"`` is always added to this set,
    representing the decorated callable itself.  This is automatically assigned
    by :func:`update_wrapper <python:functools.update_wrapper>` during
    ``__init__``.
    """

    _reserved = set(WRAPPER_ASSIGNMENTS) | {"__wrapped__"}

    def __init__(self, func: Callable, **kwargs):
        super().__init__(**kwargs)  # allow multiple inheritance
        update_wrapper(self, func)

    ################################
    ####    ATTRIBUTE ACCESS    ####
    ################################

    def __getattr__(self, name: str) -> Any:
        """Delegate getters to wrapped object."""
        return getattr(self.__wrapped__, name)

    def __setattr__(self, name: str, value: Any) -> None:
        """Delegate setters to wrapped object."""
        if name in self._reserved or hasattr(type(self), name):
            super().__setattr__(name, value)
        else:
            setattr(self.__wrapped__, name, value)

    def __delattr__(self, name: str) -> None:
        """Delegate deleters to wrapped object."""
        # NOTE: hasattr() doesn't respect nested decorators
        try:
            object.__getattribute__(self, name)
            super().__delattr__(name)
        except AttributeError:
            delattr(self.__wrapped__, name)

    ################################
    ####     SPECIAL METHODS    ####
    ################################

    def __dir__(self) -> list:
        """Include attributes of wrapped object."""
        result = dir(type(self))
        result += [k for k in self.__dict__ if k not in result]
        result += [k for k in dir(self.__wrapped__) if k not in result]
        return result

    def __call__(self, *args, **kwargs):
        """Invoke the wrapped object's ``__call__()`` method."""
        return self.__wrapped__(*args, **kwargs)

    def __str__(self) -> str:
        """Pass ``str()`` calls to wrapped object."""
        return str(self.__wrapped__)

    def __repr__(self) -> str:
        """Pass ``repr()`` calls to wrapped object."""
        return repr(self.__wrapped__)

    def __contains__(self, item: Any) -> bool:
        """Pass ``in`` keyword to wrapped object."""
        return item in self.__wrapped__

    def __iter__(self):
        """Pass ``iter()`` calls to wrapped object."""
        return iter(self.__wrapped__)

    def __len__(self) -> int:
        """Pass ``len()`` calls to wrapped object."""
        return len(self.__wrapped__)

    def __getitem__(self, key) -> Any:
        """Pass indexing to wrapped object."""
        return self.__wrapped__.__getitem__(key)

    def __setitem__(self, key, value) -> None:
        """Pass index assignment to wrapped object."""
        return self.__wrapped__.__setitem__(key, value)

    def __delitem__(self, key) -> None:
        return self.__wrapped__.__delitem__(key)


class Signature:
    """An extensible wrapper around an
    :class:`inspect.Parameter <python:inspect.Parameter>` object that allows
    easy modification of parameters and annotations.

    Parameters
    ----------
    func : Callable
        A function or other callable to introspect.
    """

    def __init__(self, func: Callable):
        self.func_name = func.__qualname__
        self.signature = inspect.signature(func)
        self.original = self.signature.replace()

    @property
    def parameter_map(self) -> Mapping[str, inspect.Parameter]:
        """A read-only mapping from argument names to their corresponding
        :class:`Parameters <python:inspect.Parameter>`.

        Examples
        --------
        TODO
        """
        return self.signature.parameters

    @property
    def parameters(self) -> tuple[inspect.Parameter, ...]:
        """Access the :class:`Parameter <python:inspect.Parameter>` objects
        that are stored in a signature.

        Returns
        -------
        tuple[inspect.Parameter]
            An ordered tuple containing every parameter in the signature.

        Notes
        -----
        This property can be written to in order to allow parameters to be
        replaced, removed, and/or extended at runtime.

        Examples
        --------
        TODO
        """
        return tuple(self.signature.parameters.values())

    @parameters.setter
    def parameters(self, val: tuple[inspect.Parameter]) -> None:
        self.signature = self.signature.replace(parameters=val)

    @property
    def return_annotation(self) -> Any:
        """Access the return annotation of the signature.

        Returns
        -------
        Any
            The raw return annotation for the signature.

        Notes
        -----
        This property can be written to in order to allow the return annotation
        to be replaced or removed at runtime.

        Examples
        --------
        TODO
        """
        return self.signature.return_annotation

    @return_annotation.setter
    def return_annotation(self, val: Any) -> None:
        self.signature = self.signature.replace(return_annotation=val)

    def reconstruct(self, annotations: bool = True) -> str:
        """Return a complete string representation of the signature.

        Parameters
        ----------
        annotations: bool, default True
            Indicates whether to include type annotations in the resulting
            string.

        Returns
        -------
        str
            A string listing the function name and all its arguments, with or
            without type annotations.

        Examples
        --------
        TODO
        """
        sig = self.signature
        if not annotations:
            empty = inspect.Parameter.empty
            parameters = tuple(
                par.replace(annotation=empty) for par in self.parameters
            )
            sig = sig.replace(
                parameters=parameters,
                return_annotation=empty
            )

        return f"{self.func_name}{sig}"

    def remove_parameter(self, name: str, replace: bool = True) -> None:
        """Remove a parameter from this signature.

        Parameters
        ----------
        name : str
            The name of the parameter to remove.
        replace : bool, default True
            If ``True`` and the named parameter occurs in the function's
            original signature, replace it with its original definition.

        Raises
        ------
        KeyError
            If the named argument is not a parameter of this signature.

        See Also
        --------
        Signature.parameters
            An ordered tuple containing every parameter in the signature.

        Examples
        --------
        TODO
        """
        names = tuple(self.signature.parameters)
        if name not in names:
            raise KeyError(name)

        index = names.index(name)
        result = self.parameters[:index]
        if replace and name in self.original.parameters:
            result += self.original.parameters[name]

        self.parameters = result + self.parameters[index + 1:]

    def __call__(self, *args, **kwargs) -> Arguments:
        """Bind the arguments to the :class:`Signature <pdcast.Signature>`,
        returning a corresponding :class:`Arguments <pdcast.Arguments>` object.

        Parameters
        ----------
        *args, **kwargs
            Arbitrary positional and keyword arguments to bind to this
            signature.

        Returns
        -------
        Arguments
            A :class:`BoundArguments <python:inspect.BoundArguments>`-like
            object with additional context for decorator-related functionality.

        Examples
        --------
        TODO
        """
        raise NotImplementedError()


class Arguments:
    """An extensible wrapper around a
    :class:`BoundArguments <python:inspect.BoundArguments>` object with extra
    context for decorator-related functionality.

    Parameters
    ----------
    bound : inspect.BoundArguments
        The :class:`BoundArguments <python:inspect.BoundArguments>` to wrap.
    signature : Signature
        A reference to the :class:`Signature <pdcast.Signature>` that spawned
        this object.

    Notes
    -----
    These are created by calling a :class:`Signature <pdcast.Signature>` using
    its :meth:`__call__() <pdcast.Signature.__call__>` method.

    Subclasses can add additional attributes to simplify decorator-specific
    operations.
    """

    def __init__(
        self,
        bound: inspect.BoundArguments,
        signature: Signature
    ):
        self.bound = bound
        self.signature = signature

    @property
    def arguments(self) -> dict[str, Any]:
        """A mutable mapping containing the current value of each argument.

        Returns
        -------
        dict[str, Any]
            An ordinary dictionary listing the bound value of each argument.
            Any changes that are made to this dictionary will be treated as
            overrides.

        See Also
        --------
        Arguments.args :
            An ``*args`` tuple that can be used to invoke the original
            function.
        Arguments.kwargs :
            A ``**kwargs`` map that can be used to invoke the original
            function.

        Examples
        --------
        TODO
        """
        return self.bound.arguments

    @property
    def args(self) -> tuple[Any, ...]:
        """Positional arguments that can be supplied to the original function.

        Returns
        -------
        tuple[Any, ...]
            An ``*args`` tuple that can be used to invoke the original
            function.

        See Also
        --------
        Arguments.arguments :
            A mutable dictionary containing the current value of each argument.
        Arguments.kwargs :
            A ``**kwargs`` map that can be used to invoke the original
            function.

        Examples
        --------
        TODO
        """
        return self.bound.args

    @property
    def kwargs(self) -> Mapping[str, Any]:
        """Keyword arguments that can be supplied to the original function.

        Returns
        -------
        Mapping[str, Any]
            A read-only ``**kwargs`` dictionary that can be used to invoke the
            original function.

        See Also
        --------
        Arguments.arguments :
            A mutable dictionary containing the current value of each argument.
        Arguments.args :
            An ``*args`` tuple that can be used to invoked the original
            function.

        Examples
        --------
        TODO
        """
        return MappingProxyType(self.bound.kwargs)

    def apply_defaults(self) -> None:
        """Apply the original signature's default values to the bound
        arguments.

        Examples
        --------
        TODO
        """
        return self.bound.apply_defaults()
