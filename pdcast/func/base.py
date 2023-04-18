"""This module describes mixins for class-based decorators.
"""
from __future__ import annotations
from functools import update_wrapper, WRAPPER_ASSIGNMENTS
from typing import Any, Callable


class BaseDecorator:
    """A mixin class that allows class-based decorators to pass attribute
    accesses down their decorator stack.

    This expects decorators to define one attribute at the class level:

        *   ``_reserved``: a set of strings describing attribute names that are
            reserved for this decorator.

    The special string ``"_func"`` is always added to this set, representing
    the decorated callable.  Decorators should always use this
    attribute to store the callable they decorate.
    """

    _reserved = set(WRAPPER_ASSIGNMENTS) | {"__wrapped__"}

    def __init__(self, func: Callable, **kwargs):
        super().__init__(**kwargs)
        update_wrapper(self, func)

    ################################
    ####    ATTRIBUTE ACCESS    ####
    ################################

    def __getattr__(self, name: str) -> Any:
        return getattr(self.__wrapped__, name)

    def __setattr__(self, name: str, value: Any) -> None:
        # breakpoint()
        if name in self._reserved or hasattr(self, name):
            super().__setattr__(name, value)
        else:
            setattr(self.__wrapped__, name, value)

    def __delattr__(self, name: str) -> None:
        if hasattr(self, name):
            super().__delattr__(name)
        else:
            delattr(self.__wrapped__, name)

    ################################
    ####     SPECIAL METHODS    ####
    ################################

    def __dir__(self) -> list:
        result = dir(type(self))
        result += [k for k in self.__dict__ if k not in result]
        result += [k for k in dir(self.__wrapped__) if k not in result]
        return result

    def __str__(self) -> str:
        return str(self.__wrapped__)

    def __repr__(self) -> str:
        return repr(self.__wrapped__)

    def __contains__(self, item: Any) -> bool:
        return item in self.__wrapped__

    def __iter__(self):
        return iter(self.__wrapped__)

    def __len__(self):
        return len(self.__wrapped__)

    def __call__(self, *args, **kwargs):
        return self.__wrapped__(*args, **kwargs)
