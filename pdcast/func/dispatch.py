"""EXPERIMENTAL - NOT CURRENTLY FUNCTIONAL

This module describes an ``@dispatch`` decorator that transforms an
ordinary Python function into one that dispatches to a method attached to the
inferred type of its first argument.
"""
from __future__ import annotations
from functools import update_wrapper
import inspect
from types import MappingProxyType
from typing import Any, Callable, Iterable

import pandas as pd

import pdcast.convert as convert
import pdcast.detect as detect
import pdcast.resolve as resolve
import pdcast.types as base_types

from pdcast.util.type_hints import type_specifier

from .base import Cooperative
from .virtual import Attachable


# TODO: can probably support an ``operator`` argument that takes a string and
# broadcasts to a math operator.


# TODO: currently, the SeriesWrapper __enter__ statement is executed multiple
# times for composite data.  This is expensive and should only occur once.
# -> Maybe apply a different rule if SeriesWrapper is given another
# SeriesWrapper as input?


######################
####    PUBLIC    ####
######################



def dispatch(func: Callable) -> Callable:
    """A decorator that transforms a Python function into a thread-local
    :class:`Dispatch` object.

    Parameters
    ----------
    func : Callable
        A function to decorate.
    arg : str
        The argument to dispatch on.  This must match an argument in the
        function's signature.

    Returns
    -------
    Callable
        A callable :class:`ExtensionFunc` object, which manages default values
        and argument validators for the decorated function.

    Raises
    ------
    TypeError
        If the decorated function does not accept variable-length keyword
        arguments.
    """
    return DispatchFunc(func, wrap_adapters=True)


#######################
####    PRIVATE    ####
#######################


class DispatchFunc(Cooperative, Attachable):
    """"""

    _reserved = {"_signature", "_dispatched", "_wrap_adapters"}

    def __init__(self, func: Callable, wrap_adapters: bool):
        self._func = func

        # ensure function accepts at least 1 argument
        self._signature = inspect.signature(func)
        if len(self._signature.parameters) < 1:
            raise TypeError("func must accept at least one argument")

        # initialize
        self._dispatched = {}
        self._wrap_adapters = wrap_adapters
        # update_wrapper(self, func)


    @property
    def dispatched(self) -> MappingProxyType:
        """A mapping from registered type specifiers to their dispatched
        implementations for this callable.
        """
        return MappingProxyType(self._dispatched)

    def register_type(
        self,
        _func: Callable = None,
        *,
        types: type_specifier | Iterable[type_specifier] | None = None
    ) -> Callable:
        """Register a callable as a dispatched implementation of this callable.

        Parameters
        ----------
        _func : Callable
            The function to be registered.
        types : type_specifier | Iterable[type_specifier] | None
            The types to dispatch to this implementation.
        **kwargs : dict
            Keywords to be passed to .contains.
        """
        if types is None:
            types = base_types.CompositeType()
        else:
            types = resolve.resolve_type([types])

        def implementation(target: Callable) -> Callable:
            """Attach a dispatched implementation to the DispatchFunc with the
            associated types.
            """
            # ensure target is callable
            if not callable(target):
                raise TypeError(
                    f"decorated function must be callable: {target}"
                )

            # broadcast to selected types
            if types:
                for typ in types:
                    self._dispatched[typ] = target
            else:
                # NOTE: callable might be a method of an AtomicType/AdapterType
                # subclass.  These can still be discovered without explicit
                # types through __init_subclass__.  We just mark the functions
                # that are requesting it here so we can bind them later.
                _dispatch = getattr(target, "_dispatch", frozenset())
                target._dispatch = _dispatch | {self}

            return target

        if _func is None:
            return implementation
        return implementation(_func)

    def _dispatch_scalar(
        self,
        series: convert.SeriesWrapper,
        *args,
        **kwargs
    ) -> convert.SeriesWrapper:
        """Dispatch a homogenous series
        """
        # search for a dispatched implementation
        result = None
        for typ, implementation in self._dispatched.items():
            if typ.contains(series.element_type):
                result = implementation(series, *args, **kwargs)
                break

        # recursively unwrap adapters and retry.
        # NOTE: This operates like a recursive stack.  Adapters are popped off
        # the stack in FIFO order before recurring, and then each adapter is
        # pushed back onto the stack in the same order.  If no error is
        # encountered in this process, then the result is guaranteed to have
        # the same adapters as the original.
        for _ in getattr(series.element_type, "adapters", ()):  # acts as `if`
            series = series.element_type.inverse_transform(series)
            series = self._dispatch_scalar(series, *args, **kwargs)
            if (
                self._wrap_adapters and
                series.element_type == series.element_type.wrapped
            ):
                series = series.element_type.transform(series)
            return series

        # fall back to generic implementation
        if result is None:
            result = self._func(series, *args, **kwargs)

        # ensure result is a SeriesWrapper
        if not isinstance(result, convert.SeriesWrapper):
            raise TypeError(
                f"dispatched implementation of {self._func.__name__}() did "
                f"not return a SeriesWrapper for type: {series.element_type}"
            )

        # ensure final index is a subset of original index
        if not series.index.difference(result.index).empty:
            raise RuntimeError(
                f"index mismatch in {self._func.__name__}(): dispatched "
                f"implementation for type {series.element_type} must return "
                f"a series with the same index as the original"
            )

        return result

    def _dispatch_composite(
        self,
        series: convert.SeriesWrapper,
        *args,
        **kwargs
    ) -> convert.SeriesWrapper:
        """Dispatch a non-homogenous series
        """
        groups = series.series.groupby(series.element_type.index, sort=False)

        # NOTE: SeriesGroupBy.transform() cannot reconcile mixed int64/uint64
        # arrays, and will attempt to convert them to float.  To avoid this, we
        # keep track of result.dtype.  If it is signed/unsigned and opposite
        # has been observed, we convert the result to dtype=object and
        # reconsider afterwards.
        observed = set()
        check_uint = [False]  # using a list avoids UnboundLocalError
        signed = base_types.SignedIntegerType
        unsigned = base_types.UnsignedIntegerType

        def transform(grp) -> pd.Series:
            """Groupwise transformation."""
            grp = convert.SeriesWrapper(
                grp,
                hasnans=series.hasnans,
                element_type=grp.name
            )
            result = self._dispatch_scalar(grp, *args, **kwargs)

            # check for int64/uint64 conflict
            # NOTE: This is a bit complicated, but it effectively invalidates
            # the check_uint flag if any type other than pure signed/unsigned
            # integers are detected as results.  In these cases, our final
            # result will be dtype: object anyway, so there's no point
            # following through with the check.
            if result.element_type.is_subtype(signed):
                if any(o.is_subtype(unsigned) for o in observed):
                    result.series = result.series.astype(object, copy=False)
                    check_uint[0] = None if check_uint[0] is None else True
            elif result.element_type.is_subtype(unsigned):
                if any(x.is_subtype(signed) for x in observed):
                    result.series = result.series.astype(object, copy=False)
                    check_uint[0] = None if check_uint[0] is None else True
            else:
                check_uint[0] = None

            observed.add(result.element_type)
            return result.series  # transform() expects a Series output

        # apply transformation
        result = groups.transform(transform)

        # resolve signed/unsigned conflict
        if check_uint[0]:
            # attempt conversion to uint64
            target = unsigned.make_nullable() if series.hasnans else unsigned
            try:
                result = convert.to_integer(
                    result,
                    dtype=target,
                    downcast=kwargs.get("downcast", None),
                    errors="raise"
                )
            except OverflowError:
                pass  # keep as dtype: object

        # re-wrap result
        return convert.SeriesWrapper(result, hasnans=series.hasnans)

    def __call__(self, data: Any, *args, **kwargs):
        """Execute the decorated function, dispatching to an overloaded
        implementation if one exists.
        """
        # convert data to series
        if not isinstance(data, (pd.Series, convert.SeriesWrapper)):
            data = detect.as_series(data)

        # enter SeriesWrapper context block
        with convert.SeriesWrapper(data) as series:

            # dispatch based on inferred type
            if isinstance(series.element_type, base_types.CompositeType):
                result = self._dispatch_composite(series, *args, **kwargs)
            else:
                result = self._dispatch_scalar(series, *args, **kwargs)
                result = result.rectify()

            # finalize
            series.series = result.series
            series.element_type = result.element_type
            series.hasnans = result.hasnans

        # return as pandas Series
        return series.series



# TODO: dispatched implementations don't get default values for extension
# arguments.  Need to pass in extension_func arguments before dispatching.
# -> extension_func needs to be **above** dispatch in call stack.  This means
# it needs to be cooperative.  When this was tried, it caused us to be unable
# to change default values.

# Maybe the solution is to just unify ExtensionFunc and DispatchFunc.  Only
# allow the addition of new arguments if the function allows a **kwargs
# argument.


from .extension import *


@extension_func
@dispatch
def foo(bar, baz=2, **kwargs):
    print("generic")
    return bar


@foo.register_arg
def baz(val, others):
    return int(val)



@foo.register_type(types="bool")
def boolean_foo(bar, baz, **kwargs):
    print("boolean")
    return bar


@foo.register_type(types="int, float")
def numeric_foo(bar, baz, **kwargs):
    print("int or float")
    return bar


class MyClass:

    def foo(self, baz, **kwargs):
        print("MyClass.foo")
        return self


foo.attach_to(MyClass)


# @dispatch
# def round(
#     series: pdcast.SeriesWrapper,
#     decimals: int = 0,
#     rule: str = "half_even"
# ) -> pdcast.SeriesWrapper:
#     # this defines a generic implementation, which may not actually be chosen
#     # if an overloaded one exists somewhere else.
#     raise NotImplementedError(
#         f"`round()` could not find a dispatched implementation for data of "
#         f"type '{series.element_type}'"
#     )


# @round.register(types="float")
# def round_float(
#     series: pdcast.SeriesWrapper,
#     decimals: int = 0,
#     rule: str = "half_even"
# ) -> pdcast.SeriesWrapper:
#     ...
