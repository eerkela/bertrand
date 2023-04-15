"""This module describes mixins for class-based decorators.
"""
from __future__ import annotations
from typing import Any


class Cooperative:
    """A mixin class that allows class-based decorators to pass attribute
    accesses down their decorator stack.

    This expects decorators to define one attribute at the class level:

        *   ``_reserved``: a set of strings describing attribute names that are
            reserved for this decorator.

    The special string ``"_func"`` is always added to this set, representing
    the decorated callable.  Decorators should always use this
    attribute to store the callable they decorate.
    """

    ################################
    ####    ATTRIBUTE ACCESS    ####
    ################################

    def __getattr__(self, name: str) -> Any:
        return getattr(self.__dict__["_func"], name)

    def __setattr__(self, name: str, value: Any) -> None:
        if name in {"_func"} | self._reserved:
            self.__dict__[name] = value
        else:
            setattr(self.__dict__["_func"], name, value)

    def __delattr__(self, name: str) -> None:
        delattr(self.__dict__["_func"], name)

    ################################
    ####     SPECIAL METHODS    ####
    ################################

    def __str__(self) -> str:
        return str(self._func)

    def __repr__(self) -> str:
        return repr(self._func)

    def __contains__(self, item: Any) -> bool:
        return item in self._func

    def __iter__(self):
        return iter(self._func)

    def __len__(self):
        return len(self._func)

    def __call__(self, *args, **kwargs):
        return self._func(*args, **kwargs)
