"""This module describes a base class for decorators that allow cooperative
attribute access with nested objects.
"""
from __future__ import annotations
from functools import update_wrapper, WRAPPER_ASSIGNMENTS
from typing import Any, Callable


class FunctionDecorator:
    """Base class for cooperative decorators.

    These decorators implement the Gang of Four's `Decorator pattern
    <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_ to 
    cooperatively pass attribute access down a (possibly nested) decorator
    stack.

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


class NoDefault:
    """Signals that an argument does not have an associated default value."""

    def __repr__(self) -> str:
        return "<no default>"


no_default = NoDefault()
