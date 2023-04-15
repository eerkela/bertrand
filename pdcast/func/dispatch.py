"""EXPERIMENTAL - NOT CURRENTLY FUNCTIONAL

This module describes an ``@dispatch`` decorator that transforms an
ordinary Python function into one that dispatches to a method attached to the
inferred type of its first argument.
"""
from __future__ import annotations
from functools import update_wrapper
import inspect
from typing import Callable, Iterable

import pdcast.convert as convert

from pdcast.util.type_hints import type_specifier


# TODO: can probably support an ``operator`` argument that takes a string and
# broadcasts to a math operator.

# TODO: cooperative decorators with extension_func and dispatch would involve
# giving DispatchFunc a __getattr__ that delegates to func, and then stacking
# them such that: 


"""Example:


@dispatch
def round(
    series: pdcast.SeriesWrapper,
    decimals: int = 0,
    rule: str = "half_even"
) -> pdcast.SeriesWrapper:
    # this defines a generic implementation, which may not actually be chosen
    # if an overloaded one exists somewhere else.
    raise NotImplementedError(
        f"`round()` could not find a dispatched implementation for data of "
        f"type '{series.element_type}'"
    )


@round.register(types="float")
def round_float(
    series: pdcast.SeriesWrapper,
    decimals: int = 0,
    rule: str = "half_even"
) -> pdcast.SeriesWrapper:
    ...

"""


class DispatchFunc:
    """"""

    def __init__(self, func: Callable):
        # ensure function accepts at least 1 argument
        if len(inspect.signature(func).parameters) < 1:
            raise TypeError("func must accept at least one argument")

        update_wrapper(self, func)
        self.func = func
        self.implementations = {}

    def register(
        self,
        _func: Callable = None,
        *,
        types: type_specifier | None = None,
        operator: str | None = None
    ) -> Callable:
        """"""
        raise NotImplementedError()

    def attach_to(self, _class: type, namespace: str | None = None) -> None:
        raise NotImplementedError()

    def __call__(self, data, *args, **kwargs):
        """Execute the decorated function, dispatching to an overloaded
        implementation if one exists.
        """


        raise NotImplementedError()





def extension_func(func: Callable) -> Callable:
    """""""
    class _ExtensionFunc(ExtensionFunc):
        """A subclass of :class:`ExtensionFunc` that supports dynamic
        assignment of ``@properties`` without affecting other instances.
        """

        def __init__(self, _func: Callable):
            super().__init__(_func)
            update_wrapper(self, _func)

            # store attributes from main thread
            if threading.current_thread() == threading.main_thread():
                main_thread.append({
                    "class": type(self),
                    "_vals": self._vals,
                    "_defaults": self._defaults,
                    "_validators": self._validators
                })

            # copy attributes from main thread
            else:
                main = main_thread[0]
                for k in main["_validators"]:
                    setattr(type(self), k, getattr(main["class"], k))
                self._vals = main["_vals"].copy()
                self._defaults = main["_defaults"].copy()
                self._validators = main["_validators"].copy()

    return _ExtensionFunc(func)









def dispatch(
    _func: Callable = None,
    *,
    namespace: str = None,
    types: type_specifier | Iterable[type_specifier] = None,
    include_subtypes: bool = True
):
    """"""
    types = CompositeType() if types is None else resolve.resolve_type([types])

    def dispatch_decorator(method):
        has_self = validate_dispatch_signature(method)

        @wraps(method)
        def dispatch_wrapper(self, *args, **kwargs):
            if has_self:
                return method(self, *args, **kwargs)
            return method(*args, **kwargs)

        # mark as dispatch method
        dispatch_wrapper._dispatch = True
        dispatch_wrapper._namespace = namespace
        broadcast_to_types(dispatch_wrapper, types, include_subtypes)
        return dispatch_wrapper

    if _method is None:
        return dispatch_decorator
    return dispatch_decorator(_method)


