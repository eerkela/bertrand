"""EXPERIMENTAL - NOT CURRENTLY FUNCTIONAL

This module describes an ``@dispatch`` decorator that transforms an
ordinary Python function into one that dispatches to a method attached to the
inferred type of its first argument.
"""
from __future__ import annotations
from collections import OrderedDict
import inspect
import itertools
from types import MappingProxyType
from typing import Any, Callable, Iterable

import pandas as pd

# import pdcast.convert as convert
import pdcast.detect as detect
import pdcast.resolve as resolve
import pdcast.types as base_types

from pdcast.util import wrapper
from pdcast.util.structs import LRUDict
from pdcast.util.type_hints import type_specifier

from .base import BaseDecorator


# TODO: check if output is series or serieswrapper and wrap.  otherwise return
# as-is.  Maybe include a replace NA flag in @dispatch itself?  If this is
# disabled, then name and index will be preserved, but NAs will be excluded.


# TODO: maybe @dispatch should take additional arguments, remove_na,
# replace_na, etc.


# TODO: when dispatching to a method, have to account for self argument




######################
####    PUBLIC    ####
######################


def dispatch(
    _func: Callable = None,
    *,
    depth: int = 1,
    wrap_adapters: bool = True  # TODO: maybe not necessary, see below
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
        return DispatchFunc(func, depth=depth, wrap_adapters=wrap_adapters)

    if _func is None:
        return decorator
    return decorator(_func)


#######################
####    PRIVATE    ####
#######################


no_default = object()  # dummy for optional arguments in DispatchDict methods


class DispatchDict(OrderedDict):
    """An :class:`OrderedDict <python:collections.OrderedDict>` that stores
    types and their dispatched implementations for :class:`DispatchFunc`
    operations.
    """

    def __init__(
        self,
        mapping: dict | None = None,
        cache_size: int = 64
    ):
        super().__init__()
        if mapping:
            for key, val in mapping.items():
                self[key] = val

        self.reorder()
        self._cache = LRUDict(maxsize=cache_size)

    @classmethod
    def fromkeys(cls, iterable: Iterable, value: Any = None) -> DispatchDict:
        """Implement :meth:`dict.fromkeys` for DispatchDict objects."""
        return DispatchDict(super().fromkeys(iterable, value))

    def get(self, key: type_specifier, default: Any = None) -> Any:
        """Implement :meth:`dict.get` for DispatchDict objects."""
        try:
            return self[key]
        except KeyError:
            return default

    def pop(self, key: type_specifier, default: Any = no_default) -> Any:
        """Implement :meth:`dict.pop` for DispatchDict objects."""
        key = _resolve_key(key)
        try:
            result = self[key]
            del self[key]
            return result
        except KeyError as err:
            if default is no_default:
                raise err
            return default

    def setdefault(self, key: type_specifier, default: Callable = None) -> Any:
        """Implement :meth:`dict.setdefault` for DispatchDict objects."""
        key = _resolve_key(key)

        try:
            return self[key]
        except KeyError:
            self[key] = default
            return default

    def update(self, other) -> None:
        """Implement :meth:`dict.update` for DispatchDict objects."""
        super().update(DispatchDict(other))
        self.reorder()

    def _validate_implementation(self, call: Callable) -> None:
        """Ensure that an overloaded implementation is valid."""
        if not callable(call):
            raise TypeError(
                f"overloaded implementation must be callable: {repr(call)}"
            )

    def reorder(self) -> None:
        """Sort the dictionary into topological order, with most specific
        keys first.
        """
        sigs = list(self)
        edges = {}

        # group edges by first node
        for edge in [(a, b) for a in sigs for b in sigs if _edge(a, b)]:
            edges.setdefault(edge[0], []).append(edge[1])

        # add signatures not contained in edges
        for sig in sigs:
            if sig not in edges:
                edges[sig] = []

        # sort according to edges
        for sig in _sort(edges):
            super().move_to_end(sig)

        # TODO: check for ambiguities?

    def __or__(self, other) -> DispatchDict:
        result = self.copy()
        result.update(other)
        return result

    def __ior__(self, other) -> DispatchDict:
        self.update(other)
        return self

    def __getitem__(self, key: type_specifier | tuple[type_specifier]) -> Any:
        key = _resolve_key(key)

        # trivial case: key has exact match
        if super().__contains__(key):
            return super().__getitem__(key)

        # search for first (sorted) match that fully contains key
        temp = key
        while temp:
            # check for cached result
            if temp in self._cache:
                return self._cache[temp]

            # search for match of same length
            for sig, val in self.items():
                if (
                    len(sig) == len(temp) and  # TODO: no need for length check
                    all(x.contains(y) for x, y in zip(sig, temp))
                ):
                    self._cache[temp] = val
                    return val

            # back off and retry
            temp = temp[:-1]

        # no match found
        raise KeyError(tuple(str(x) for x in key))

    def __setitem__(self, key: type_specifier, value: Callable) -> None:
        key = _resolve_key(key)
        self._validate_implementation(value)
        super().__setitem__(key, value)
        self.reorder()

    def __delitem__(self, key: type_specifier) -> None:
        key = _resolve_key(key)

        # require exact match
        if super().__contains__(key):
            return super().__delitem__(key)

        # no match found
        raise KeyError(tuple(str(x) for x in key))

    def __contains__(self, key: type_specifier):
        try:
            self.__getitem__(key)
            return True
        except KeyError:
            return False


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
        {"_signature", "_dispatched", "_depth", "_wrap_adapters"}
    )

    def __init__(
        self,
        func: Callable,
        depth: int,
        wrap_adapters: bool
    ):
        super().__init__(func=func)
        self._signature = inspect.signature(func)

        # validate depth
        if depth < 1:
            raise ValueError(
                f"@dispatch depth must be >= 1 for function: "
                f"'{func.__qualname__}'"
            )
        if len(self._signature.parameters) < depth:
            err_msg = f"'{func.__qualname__}' must accept at least "
            if depth == 1:  # singular
                err_msg += f"{depth} argument"
            else:
                err_msg += f"{depth} arguments"
            raise TypeError(err_msg)

        self._dispatched = DispatchDict()
        self._wrap_adapters = bool(wrap_adapters)
        self._depth = depth

    @property
    def dispatched(self) -> MappingProxyType:
        """A mapping from :doc:`types </content/types/types>` to their
        dispatched implementations.

        Returns
        -------
        MappingProxyType
            A read-only dictionary mapping types to their associated dispatch
            functions.

        Notes
        -----
        The mapping that is returned by this method is sorted according to the
        order in which implementations are searched when dispatching is
        performed.  If no match is found for any of the 

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
        return MappingProxyType(self._dispatched)

    def overload(self, *args) -> Callable:
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

        def implementation(call: Callable) -> Callable:
            """Attach a dispatched implementation to the DispatchFunc with the
            associated types.
            """
            # ensure at least one arg is given
            if not args:
                raise TypeError(
                    f"'{call.__qualname__}()' must dispatch on at least one "
                    f"argument"
                )

            # broadcast to selected types
            composites = [resolve.resolve_type([spec]) for spec in args]
            paths = list(itertools.product(*composites))
            for path in paths:
                self._dispatched[path] = call

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
        try:
            implementation = self._dispatched[series.element_type]
            result = implementation(series, *args, **kwargs)
        except KeyError:
            result = None

        # TODO: adapters might be handled by their own cast() overloads.

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

        return result.rectify()

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

    def __getitem__(self, key: type_specifier) -> Callable:
        """Get the dispatched implementation for objects of a given type.

        This method searches the implementation space being managed by this
        :class:`DispatchFunc`.  It always returns the same implementation that
        is used when the function is invoked.
        """
        # resolve key
        key_type = resolve.resolve_type(key)  # TODO: should use _resolve_key

        # search for a dispatched implementation
        try:
            return self._dispatched[key_type]
        except KeyError:
            pass

        # TODO: maybe adapter unwrapping should be implemented in generic
        # implementation?  Either that or convert @dispatch wrap_adapters into
        # ignore_adapters.  Or just use **kwargs for contains() operations.

        # unwrap adapters  # TODO: breaks multiple dispatch
        for _ in key_type.adapters:
            return self[key_type.wrapped]  # recur

        # return generic
        return self.__wrapped__

    def __delitem__(self, key: type_specifier) -> None:
        """Remove an implementation from the pool.
        """
        self._dispatched.__delitem__(key)


#######################
####    PRIVATE    ####
#######################


def _resolve_key(key: type_specifier | tuple[type_specifier]) -> tuple:
    """Convert arbitrary type specifiers into a valid key for DispatchDict
    lookups.
    """
    if not isinstance(key, tuple):
        key = (key,)

    key_type = []
    for spec in key:
        spec_type = resolve.resolve_type(spec)
        if isinstance(spec_type, base_types.CompositeType):
            raise TypeError(f"key must not be composite: {repr(spec)}")
        key_type.append(spec_type)

    return tuple(key_type)


def _consistent(sig1: tuple, sig2: tuple) -> bool:
    """Check for an overlap between two signatures.

    If this returns ``True``, then it is possible for a signature to satisfy
    both sig1 and sig2.
    """
    # check for empty signatures
    if not sig1:
        return not sig2
    if not sig2:
        return not sig1

    # lengths match
    if len(sig1) == len(sig2):
        return all(x.contains(y) or y.contains(x) for x, y in zip(sig1, sig2))

    return False  # not considering variadics

    # lengths do not match
    # idx1 = idx2 = 0
    # while idx1 < len(sig1) and idx2 < len(sig2):
    #     type1 = sig1[idx1]
    #     type2 = sig2[idx2]
    #     if not type1.contains(type2) and not type2.contains(type1):
    #         return False
    #     idx1 += 1
    #     idx2 += 1

    # return True


def _supercedes(sig1: tuple, sig2: tuple) -> bool:
    """Check if sig1 is consistent with and strictly more specific than sig2.
    """
    # same length
    if len(sig1) == len(sig2):
        return all(x.contains(y) for x, y in zip(sig2, sig1))

    return False  # not considering variadics

    # # longer
    # idx1 = idx2 = 0
    # while idx1 < len(sig1) and idx2 < len(sig2):
    #     type1 = sig1[idx1]
    #     type2 = sig2[idx2]
    #     if not type2.contains(type1):
    #         return False
    #     idx1 += 1
    #     idx2 += 1

    # return True


def _ambiguous(sig1: tuple, sig2: tuple) -> bool:
    """Signatures are consistent, but neither is strictly more specific.
    """
    return (
        _consistent(sig1, sig2) and
        not (_supercedes(sig1, sig2) or _supercedes(sig2, sig1))
    )


def _edge(sig1: tuple, sig2: tuple, tie_breaker: Callable = hash) -> bool:
    """If ``True``, check sig1 before sig2.

    Ties are broken by the ``tie_breaker``, which defaults to ``hash()``
    (psuedo-random).
    """
    return _supercedes(sig1, sig2) and (
        not _supercedes(sig2, sig1) or tie_breaker(sig1) > tie_breaker(sig2)
    )


def _sort(edges: dict) -> list:
    """Topological sort algorithm by Kahn (1962).

    Parameters
    ----------
    edges : dict
        a dict of the form {A: {B, C}} where B and C depend on A

    Returns
    -------
    list
        an ordered list of nodes that satisfy the dependencies of edges

    Examples
    --------
    >>> _sort({1: (2, 3), 2: (3, )})
    [1, 2, 3]

    References
    ----------    
    Kahn, Arthur B. (1962), "Topological sorting of large networks",
    Communications of the ACM
    """
    # invert edges: {A: {B, C}} -> {B: {A}, C: {A}}
    inverted = OrderedDict()
    for key, val in edges.items():
        for item in val:
            inverted[item] = inverted.get(item, set()) | {key}

    # Proceed with Kahn topological sort algorithm
    S = OrderedDict.fromkeys(v for v in edges if v not in inverted)
    L = []
    while S:
        n, _ = S.popitem()
        L.append(n)
        for m in edges.get(n, ()):
            assert n in inverted[m]
            inverted[m].remove(n)
            if not inverted[m]:
                S[m] = None

    # check for cycles
    if any(inverted.get(v, None) for v in edges):
        cycles = [v for v in edges if inverted.get(v, None)]
        raise ValueError(f"edges are cyclic: {cycles}")

    return L





# TODO: DispatchFunc needs to bind based on inspect signature, up to a given
# depth.
# overload() must always accept ``depth`` arguments.  The special value None
# signifies a wildcard.  This eliminates length checks in __getitem__, etc.


# @cast.overload("sparse", None)
# def densify()
# -> return cast(dense series, dtype)  # recursive


# @cast.overload(None, "sparse")
# def sparsify()
# -> result = cast(series, wrapped dtype)  # recursive
# -> return sparse result


# @cast.overload("int", "sparse")
# def integer_sparsify()
#    overloaded specifically for integers



# TODO: @overload should accept *args, **kwargs, which it binds to the
# signature.  This allows keyword dispatch.

# @cast.overload(series="int", dtype="float")

# These always match the generic implementation's signature.




# def ambiguities(signatures):
#     """ All signature pairs such that A is ambiguous with B """
#     signatures = list(map(tuple, signatures))
#     return set((a, b) for a in signatures for b in signatures
#                if hash(a) < hash(b)
#                and ambiguous(a, b)
#                and not any(supercedes(c, a) and supercedes(c, b)
#                            for c in signatures))




@dispatch
def foo(bar, baz):
    """doc for foo()"""
    print("generic")
    return bar


@foo.overload("int")
def integer_foo(bar, baz):
    print("int")
    return bar


@foo.overload("bool", "bool")
def boolean_foo(bar, baz):
    print("boolean")
    return bar


@foo.overload("int8", "int")
def int8_foo(bar, baz):
    print("int8")
    return bar


@foo.overload("int8[numpy]")
def numpy_int8_foo(bar, baz):
    print("np.int8")
    return bar
