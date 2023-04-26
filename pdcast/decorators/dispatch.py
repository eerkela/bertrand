"""EXPERIMENTAL - NOT CURRENTLY FUNCTIONAL

This module describes an ``@dispatch`` decorator that transforms an
ordinary Python function into one that dispatches to a method attached to the
inferred type of its first argument.
"""
from __future__ import annotations
import inspect
from types import MappingProxyType
from typing import Any, Callable, Iterable

import pandas as pd

# import pdcast.convert as convert
import pdcast.detect as detect
import pdcast.resolve as resolve
import pdcast.types as base_types

from pdcast.util import wrapper
from pdcast.util.type_hints import type_specifier

from .base import BaseDecorator


# TODO: can probably support an ``operator`` argument that takes a string and
# broadcasts to a math operator.

# TODO: currently, the SeriesWrapper __enter__ statement is executed multiple
# times for composite data.  This is expensive and should only occur once.
# -> Maybe apply a different rule if SeriesWrapper is given another
# SeriesWrapper as input?


# TODO: check if output is series or serieswrapper and wrap.  otherwise return
# as-is.  Maybe include a replace NA flag in @dispatch itself?  If this is
# disabled, then name and index will be preserved, but NAs will be excluded.


# TODO: maybe @dispatch should take additional arguments, like wrap_adapters,
# remove_na, replace_na, etc.


# TODO: when dispatching to a method, have to account for self argument


# TODO: include optional targetable: bool argument to @dispatch
# this forces the function to accept a second argument, `dtype`, which must be
# a type specifier.  For these functions, implementations can be dispatched
# based on both the first and second arguments, rather than just the first.

# @to_datetime.overload("string", target="datetime[python]")
# @to_datetime.overload("int", target="datetime[pandas]")



# TODO: __getitem__ allows users to check which implementation will be used
# for a given data type
# cast["int"]["datetime"]
# round["int"]

# __getitem__ returns a TypeDict struct that automatically resolves its keys
# during lookup.


######################
####    PUBLIC    ####
######################


def dispatch(
    _func: Callable = None,
    *,
    wrap_adapters: bool = True
) -> Callable:
    """A decorator that allows a Python function to dispatch to multiple
    implementations based on the type of its first argument.

    Parameters
    ----------
    func : Callable
        A Python function or other callable to be decorated.  This serves as a
        generic implementation and is only called if no overloaded
        implementation is found for the dispatched data.

    Returns
    -------
    DispatchFunc
        A cooperative :class:`DispatchFunc` decorator, which manages dispatched
        implementations for the decorated callable.

    Raises
    ------
    TypeError
        If the decorated function does not accept at least one positional
        argument.

    Notes
    -----
    This decorator works just like
    :func:`@functools.singledispatch <python:functools.singledispatch>`,
    except that it is extended to handle vectorized data in the ``pdcast``
    :doc:`type system </content/types/types>`.
    """
    def decorator(func: Callable):
        """Convert a callable into a DispatchFunc object."""
        return DispatchFunc(func, wrap_adapters=wrap_adapters)

    if _func is None:
        return decorator
    return decorator(_func)


def double_dispatch(
    _func: Callable = None,
    *,
    wrap_adapters: bool = True
) -> Callable:
    """A decorator that implements a double dispatch mechanism based on the
    types of its first 2 arguments.
    """


#######################
####    PRIVATE    ####
#######################


class DispatchFunc(BaseDecorator):
    """A wrapper for the decorated callable that manages its dispatched
    implementations.

    Parameters
    ----------
    func : Callable
        The decorated function or other callable.

    Examples
    --------
    See the docs for :func:`@dispatch <pdcast.dispatch>` for example usage.
    """

    _reserved = (
        BaseDecorator._reserved |
        {"_signature", "_dispatched", "_wrap_adapters"}
    )

    def __init__(
        self,
        func: Callable,
        wrap_adapters: bool
    ):
        super().__init__(func=func)
        self._wrap_adapters = bool(wrap_adapters)

        # validate signature
        if len(inspect.signature(func).parameters) < 1:
            raise TypeError("func must accept at least one argument")

        self._dispatched = {}  # TODO: make this a WeakKeyDictionary

    @property
    def dispatched(self) -> MappingProxyType:
        """A mapping from :doc:`types </content/types/types>` to their
        dispatched implementations.

        Returns
        -------
        MappingProxyType
            A read-only dictionary mapping types to their associated dispatch
            functions.

        Examples
        --------
        .. doctest::

            >>> @dispatch
            ... def foo(bar):
            ...     print("generic implementation")
            ...     return bar

            >>> @foo.overload("int")
            ... def integer_foo(bar):
            ...     print("integer implementation")
            ...     return bar

            >>> foo.dispatched

        .. TODO: check
        """
        return MappingProxyType(self._dispatched.copy())

    def overload(
        self,
        types: type_specifier | Iterable[type_specifier]
    ) -> Callable:
        """A decorator that transforms a naked function into a dispatched
        implementation for this :class:`DispatchFunc`.

        Parameters
        ----------
        types : type_specifier | Iterable[type_specifier] | None, default None
            The type(s) to dispatch to this implementation.  See notes for
            handling of :data:`None <python:None>`.

        Returns
        -------
        Callable
            The original undecorated function.

        Raises
        ------
        TypeError
            If the decorated function is not callable with at least one
            argument.

        Notes
        -----
        This decorator works just like the :meth:`register` method of
        :func:`singledispatch <python:functools.singledispatch>` objects,
        except that it does not interact with type annotations in any way.
        Instead, if a type is not provided as an argument to this method, it
        will be disregarded during dispatch lookups, unless the decorated
        callable is a method of an :class:`AtomicType <pdcast.AtomicType>` or
        :class:`AdapterType <pdcast.AdapterType>` subclass.  In that case, the
        attached type will be automatically bound to the dispatched
        implementation during
        :meth:`__init_subclass__() <AtomicType.__init_subclass__>`.

        Examples
        --------
        See the :func:`dispatch` :ref:`API docs <dispatch.dispatched>` for
        example usage.
        """
        types = resolve.resolve_type([types])

        def implementation(call: Callable) -> Callable:
            """Attach a dispatched implementation to the DispatchFunc with the
            associated types.
            """
            # ensure call is callable
            if not callable(call):
                raise TypeError(
                    f"decorated function must be callable: {repr(call)}"
                )

            # TODO: ensure implementation accepts at least one non-self parameter

            # broadcast to selected types
            for typ in types:
                self._dispatched[typ] = call

            return call

        return implementation

    def generic(
        self,
        *args,
        **kwargs
    ) -> pd.Series | wrapper.SeriesWrapper:
        """A reference to the generic implementation of the decorated function.
        """
        return self.__wrapped__(*args, **kwargs)

    def _dispatch_scalar(
        self,
        series: wrapper.SeriesWrapper,
        *args,
        **kwargs
    ) -> wrapper.SeriesWrapper:
        """Dispatch a homogenous series to the correct implementation."""
        # search for a dispatched implementation
        result = None
        for typ, implementation in self._dispatched.items():
            if typ.contains(series.element_type):
                result = implementation(series, *args, **kwargs)
                break

        # recursively unwrap adapters and retry.
        if result is None:
            # NOTE: This operates like a recursive stack.  Adapters are popped
            # off the stack in FIFO order before recurring, and then each
            # adapter is pushed back onto the stack in the same order.
            for before in getattr(series.element_type, "adapters", ()):
                series = series.element_type.inverse_transform(series)
                series = self._dispatch_scalar(series, *args, **kwargs)
                if (
                    self._wrap_adapters and
                    series.element_type == before.wrapped
                ):
                    series = series.element_type.transform(series)
                return series

        # fall back to generic implementation
        if result is None:
            result = self.__wrapped__(series, *args, **kwargs)

        # ensure result is a SeriesWrapper
        if not isinstance(result, wrapper.SeriesWrapper):
            raise TypeError(
                f"dispatched implementation of {self.__wrapped__.__name__}() "
                f"did not return a SeriesWrapper for type: "
                f"{series.element_type}"
            )

        # ensure final index is a subset of original index
        if not series.index.difference(result.index).empty:
            raise RuntimeError(
                f"index mismatch in {self.__wrapped__.__name__}(): dispatched "
                f"implementation for type {series.element_type} must return "
                f"a series with the same index as the original"
            )

        return result

    def _dispatch_composite(
        self,
        series: wrapper.SeriesWrapper,
        *args,
        **kwargs
    ) -> wrapper.SeriesWrapper:
        """Dispatch a mixed-type series to the appropriate implementation."""
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
            grp = wrapper.SeriesWrapper(
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
        return wrapper.SeriesWrapper(result, hasnans=series.hasnans)

    def __call__(
        self,
        data: Any,
        *args,
        **kwargs
    ) -> pd.Series | wrapper.SeriesWrapper:
        """Execute the decorated function, dispatching to an overloaded
        implementation if one exists.
        """
        # fastpath for pre-wrapped data (internal use)
        if isinstance(data, wrapper.SeriesWrapper):
            if isinstance(data.element_type, base_types.CompositeType):
                return self._dispatch_composite(data, *args, **kwargs)
            return self._dispatch_scalar(data, *args, **kwargs)

        # enter SeriesWrapper context block
        with wrapper.SeriesWrapper(detect.as_series(data)) as series:

            # dispatch based on inferred type
            if isinstance(series.element_type, base_types.CompositeType):
                result = self._dispatch_composite(series, *args, **kwargs)
            else:
                result = self._dispatch_scalar(series, *args, **kwargs)

            # finalize
            series.series = result.series
            series.element_type = result.element_type
            series.hasnans = result.hasnans

        # return as pandas Series
        return series.series

    def __getitem__(self, val: type_specifier) -> Callable:
        val_type = resolve.resolve_type(val)
        if isinstance(val_type, base_types.CompositeType):
            raise KeyError(f"key must not be composite: {val}")

        # search for a dispatched implementation
        for origin, implementation in reversed(self._dispatched.items()):
            if origin.contains(val_type):
                return implementation

        # unwrap adapters
        for _ in val_type.adapters:
            return self[val_type.wrapped]  # recur

        # return generic
        return self.__wrapped__



class DoubleDispatchFunc(DispatchFunc):

    def __init__(
        self,
        func: Callable,
        wrap_adapters: bool
    ):
        super().__init__(func=func, wrap_adapters=wrap_adapters)

        # validate signature
        if len(inspect.signature(func).parameters) < 2:
            raise TypeError(
                "double dispatch function must accept at least 2 arguments"
            )

        # maintain a separate _fallback dict
        self._fallback = {}

        # TODO: maintain separate _double and _single dictionaries.
        # _double contains targets as its first entry
        



    def overload(
        self,
        origin: type_specifier | Iterable[type_specifier] | None,
        target: type_specifier
    ) -> Callable:
        """A decorator that transforms a naked function into a dispatched
        implementation for this :class:`DispatchFunc`.

        Parameters
        ----------
        types : type_specifier | Iterable[type_specifier] | None, default None
            The type(s) to dispatch to this implementation.  See notes for
            handling of :data:`None <python:None>`.

        Returns
        -------
        Callable
            The original undecorated function.

        Raises
        ------
        TypeError
            If the decorated function is not callable with at least one
            argument.

        Notes
        -----
        This decorator works just like the :meth:`register` method of
        :func:`singledispatch <python:functools.singledispatch>` objects,
        except that it does not interact with type annotations in any way.
        Instead, if a type is not provided as an argument to this method, it
        will be disregarded during dispatch lookups, unless the decorated
        callable is a method of an :class:`AtomicType <pdcast.AtomicType>` or
        :class:`AdapterType <pdcast.AdapterType>` subclass.  In that case, the
        attached type will be automatically bound to the dispatched
        implementation during
        :meth:`__init_subclass__() <AtomicType.__init_subclass__>`.

        Examples
        --------
        See the :func:`dispatch` :ref:`API docs <dispatch.dispatched>` for
        example usage.
        """
        
        types = resolve.resolve_type([types])

        # process target
        if target:
            target = resolve.resolve_type(target)
            if isinstance(target, base_types.CompositeType):
                raise TypeError(f"`target` must not be composite: {target}")

        def implementation(call: Callable) -> Callable:
            """Attach a dispatched implementation to the DispatchFunc with the
            associated types.
            """
            # ensure call is callable
            if not callable(call):
                raise TypeError(
                    f"decorated function must be callable: {repr(call)}"
                )

            # TODO: ensure implementation accepts at least one non-self parameter

            # broadcast to selected types
            for typ in types:
                self._dispatched[typ][target] = call

            return call

        return implementation

    def _dispatch_scalar(
        self,
        series: wrapper.SeriesWrapper,
        dtype: base_types.ScalarType,
        *args,
        **kwargs
    ) -> wrapper.SeriesWrapper:
        """Dispatch without targeting any specific data type."""
        # search for a dispatched implementation
        result = None
        for typ, targets in self._dispatched.items():
            if typ.contains(series.element_type):
                # search for target match
                for target, implementation in targets.items():
                    if target and target.contains(dtype):
                        result = implementation(series, *args, **kwargs)
                        break
                else:
                    if None in targets:
                        result = targets[None](series, *args, **kwargs)

                break

        # recursively unwrap adapters and retry.
        if result is None:
            # NOTE: This operates like a recursive stack.  Adapters are popped
            # off the stack in FIFO order before recurring, and then each
            # adapter is pushed back onto the stack in the same order.
            for before in getattr(series.element_type, "adapters", ()):
                series = series.element_type.inverse_transform(series)
                series = self._dispatch_scalar(series, *args, **kwargs)
                if (
                    self._wrap_adapters and
                    series.element_type == before.wrapped
                ):
                    series = series.element_type.transform(series)
                return series

        # fall back to generic implementation
        if result is None:
            result = self.__wrapped__(series, *args, **kwargs)

        # ensure result is a SeriesWrapper
        if not isinstance(result, wrapper.SeriesWrapper):
            raise TypeError(
                f"dispatched implementation of {self.__wrapped__.__name__}() "
                f"did not return a SeriesWrapper for type: "
                f"{series.element_type}"
            )

        # ensure final index is a subset of original index
        if not series.index.difference(result.index).empty:
            raise RuntimeError(
                f"index mismatch in {self.__wrapped__.__name__}(): dispatched "
                f"implementation for type {series.element_type} must return "
                f"a series with the same index as the original"
            )

        return result





# @dispatch
# def foo(bar):
#     """doc for foo()"""
#     print("generic")
#     return bar


# @foo.overload("bool")
# def boolean_foo(bar):
#     print("boolean")
#     return bar


# @foo.overload("int, float")
# def numeric_foo(bar):
#     print("int or float")
#     return bar
